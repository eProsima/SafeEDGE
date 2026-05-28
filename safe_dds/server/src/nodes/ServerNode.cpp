#include <safe_edge/server/nodes/ServerNode.hpp>

#include <safe_edge/server/common/PilotServerPayloadParser.hpp>
#include <safe_edge/server/common/PilotServerPublishHelper.hpp>
#include <safe_edge/server/common/TopicNames.hpp>

#include <safedds/dds/Publisher.hpp>
#include <safedds/dds/ReturnCode.hpp>
#include <safedds/dds/SampleInfo.hpp>
#include <safedds/dds/Subscriber.hpp>
#include <safedds/dds/qos/DataReaderQos.hpp>
#include <safedds/dds/qos/DataWriterQos.hpp>
#include <safedds/dds/qos/DomainParticipantQos.hpp>
#include <safedds/dds/qos/PublisherQos.hpp>
#include <safedds/dds/qos/SubscriberQos.hpp>
#include <safedds/dds/qos/TopicQos.hpp>
#include <safedds/execution/TimePoint.hpp>
#include <safedds/memory/container/StaticList.hpp>
#include <safedds/transport.hpp>

#include <iostream>

namespace safe_edge {
namespace server {
namespace nodes {

namespace {

static safe_edge::pilot_server::ChargerLocationSeq build_charger_locations()
{
    safe_edge::pilot_server::ChargerLocationSeq locations;

    safe_edge::pilot_server::ChargerLocation loc1;
    loc1.id = 1;
    loc1.name = "Madrid North Hub";
    loc1.position.latitude = 40.4637F;
    loc1.position.longitude = -3.7492F;
    locations.push_back(loc1);

    safe_edge::pilot_server::ChargerLocation loc2;
    loc2.id = 2;
    loc2.name = "Barcelona Port Station";
    loc2.position.latitude = 41.3851F;
    loc2.position.longitude = 2.1734F;
    locations.push_back(loc2);

    safe_edge::pilot_server::ChargerLocation loc3;
    loc3.id = 3;
    loc3.name = "Valencia Depot";
    loc3.position.latitude = 39.4699F;
    loc3.position.longitude = -0.3763F;
    locations.push_back(loc3);

    return locations;
}

static eprosima::safedds::memory::container::StaticList<
    eprosima::safedds::transport::Locator, 2U> SERVER_INITIAL_PEERS;

static bool init_server_peers()
{
    static constexpr uint16_t ports[] = { 8011U, 8030U };
    for (uint16_t port : ports)
    {
        SERVER_INITIAL_PEERS.add(
            eprosima::safedds::transport::Locator::from_ipv4({127, 0, 0, 1}, port));
    }
    return true;
}

static const bool SERVER_PEERS_INITIALIZED = init_server_peers();

template<typename TypeSupportT>
bool register_type(
        eprosima::safedds::dds::DomainParticipant& participant,
        TypeSupportT& type_support,
        const char* label)
{
    if (eprosima::safedds::dds::ReturnCode::OK != type_support.register_type(participant, type_support.get_type_name()))
    {
        std::cerr << "[server] Failed to register type: " << label << std::endl;
        return false;
    }

    return true;
}

template<typename TypeSupportT>
eprosima::safedds::dds::Topic* create_topic(
        eprosima::safedds::dds::DomainParticipant& participant,
        eprosima::safedds::memory::container::StaticString256& topic_name,
        TypeSupportT& type_support)
{
    return participant.create_topic(
        topic_name,
        type_support.get_type_name(),
        eprosima::safedds::dds::TopicQos{},
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
}

template<typename TypeSupportT>
eprosima::safedds::dds::TypedDataWriter<TypeSupportT>* downcast_writer(
        eprosima::safedds::dds::DataWriter* writer)
{
    if (nullptr == writer)
    {
        return nullptr;
    }

    return eprosima::safedds::dds::TypedDataWriter<TypeSupportT>::downcast(*writer);
}

template<typename TypeSupportT>
eprosima::safedds::dds::TypedDataReader<TypeSupportT>* downcast_reader(
        eprosima::safedds::dds::DataReader* reader)
{
    if (nullptr == reader)
    {
        return nullptr;
    }

    return eprosima::safedds::dds::TypedDataReader<TypeSupportT>::downcast(*reader);
}

} // namespace

ServerNode::ParticipantListener::ParticipantListener(
        ServerNode& owner)
    : owner_(owner)
{
}

void ServerNode::ParticipantListener::on_subscription_matched(
        eprosima::safedds::dds::DataReader& reader,
        const eprosima::safedds::dds::SubscriptionMatchedStatus& info) noexcept
{
    owner_.log_subscription_match(reader.get_topicdescription().get_name().const_string_data(), info.total_count);
}

void ServerNode::ParticipantListener::on_publication_matched(
        eprosima::safedds::dds::DataWriter& writer,
        const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept
{
    owner_.log_publication_match(writer.get_topic().get_name().const_string_data(), info.total_count);

    if (&writer == owner_.charger_locations_datawriter_ && info.total_count_change > 0)
    {
        owner_.request_pilot_server_data(safe_edge::pilot_server::RequestedDataType::CHARGER_LOCATION);
    }
}

ServerNode::ServerQueryListener::ServerQueryListener(
        ServerNode& owner)
    : owner_(owner)
{
}

void ServerNode::ServerQueryListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::ServerQueryTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[server] Failed to downcast server_query reader" << std::endl;
        return;
    }

    safe_edge::pilot_server::ServerQuery sample{};
    eprosima::safedds::dds::SampleInfo info{};
    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            owner_.on_server_query_received(sample);
        }
    }
}

ServerNode::ServerNode(
        const common::RuntimeConfig& runtime_config)
    : runtime_config_(runtime_config)
    , pilot_client_(runtime_config.pilot_server_base_url, "/etc/safe-edge/server.ini")
    , participant_listener_(*this)
    , server_query_listener_(*this)
    , heartbeat_timer_({5, 0})
    , refresh_timer_({30, 0})
    , uptime_timer_({300, 0})
    , start_time_(std::chrono::steady_clock::now())
{
}

void ServerNode::configure_participant_qos(
        eprosima::safedds::dds::DomainParticipantQos& qos) noexcept
{
    qos.wire_protocol_qos().use_multicast_discovery = false;
    qos.wire_protocol_qos().initial_peers = &SERVER_INITIAL_PEERS;
}

int ServerNode::run()
{
    if (!initialize())
    {
        return 1;
    }

    heartbeat_timer_.start();
    refresh_timer_.start();
    uptime_timer_.start();

    std::cout << "[server] [START] Running with participant port " << runtime_config_.participant_port << std::endl;
    std::cout << "[server] PilotServer base_url=" << runtime_config_.pilot_server_base_url
              << " api_key=***" << std::endl;

    while (true)
    {
        while (executor_->has_pending_work())
        {
            executor_->spin(eprosima::safedds::execution::TIME_ZERO);
        }

        if (heartbeat_timer_.is_triggered_and_reset())
        {
            publish_heartbeat();
        }

        if (refresh_timer_.is_triggered_and_reset())
        {
            periodic_refresh_all_resources();
        }

        if (uptime_timer_.is_triggered_and_reset())
        {
            log_uptime();
        }

        executor_->spin(next_wakeup_time());
    }

    return 0;
}

bool ServerNode::initialize()
{
    return create_participant() &&
           register_types() &&
           create_topics() &&
           create_endpoints() &&
           enable_entities() &&
           create_executor();
}

bool ServerNode::create_participant()
{
    eprosima::safedds::dds::DomainParticipantQos participant_qos{};
    eprosima::safedds::memory::container::StaticString256 participant_name(runtime_config_.participant_name.c_str());
    participant_qos.participant_name() = participant_name;
    participant_qos.wire_protocol_qos().announced_locator = eprosima::safedds::transport::Locator::from_ipv4(
        {127, 0, 0, 1},
        runtime_config_.participant_port);

    configure_participant_qos(participant_qos);

    participant_ = factory_.create_participant(
        runtime_config_.domain_id,
        participant_qos,
        &participant_listener_,
        eprosima::safedds::dds::PUBLICATION_MATCHED_STATUS |
        eprosima::safedds::dds::SUBSCRIPTION_MATCHED_STATUS);

    if (nullptr == participant_)
    {
        std::cerr << "[server] Failed to create participant" << std::endl;
        return false;
    }

    return true;
}

bool ServerNode::register_types()
{
    return register_type(*participant_, charger_locations_type_support_, "ChargerLocation") &&
           register_type(*participant_, server_query_type_support_, "ServerQuery") &&
           register_type(*participant_, service_heartbeat_type_support_, "ServiceHeartbeat");
}

bool ServerNode::create_topics()
{
    charger_locations_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::charger_locations());
    charger_locations_topic_ = create_topic(*participant_, charger_locations_topic_name_, charger_locations_type_support_);
    if (nullptr == charger_locations_topic_) { std::cerr << "[server] Failed to create topic: charger_locations" << std::endl; return false; }

    server_query_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::server_query());
    server_query_topic_ = create_topic(*participant_, server_query_topic_name_, server_query_type_support_);
    if (nullptr == server_query_topic_) { std::cerr << "[server] Failed to create topic: server_query" << std::endl; return false; }

    service_heartbeat_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::service_heartbeat());
    service_heartbeat_topic_ = create_topic(*participant_, service_heartbeat_topic_name_, service_heartbeat_type_support_);
    if (nullptr == service_heartbeat_topic_) { std::cerr << "[server] Failed to create topic: service_heartbeat" << std::endl; return false; }

    return true;
}

bool ServerNode::create_endpoints()
{
    eprosima::safedds::dds::PublisherQos publisher_qos{};
    publisher_ = participant_->create_publisher(
        publisher_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);

    eprosima::safedds::dds::SubscriberQos subscriber_qos{};
    subscriber_ = participant_->create_subscriber(
        subscriber_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);

    if (nullptr == publisher_ || nullptr == subscriber_)
    {
        std::cerr << "[server] Failed to create publisher or subscriber" << std::endl;
        return false;
    }

    eprosima::safedds::dds::DataWriterQos writer_qos{};
    writer_qos.reliability().kind = eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    charger_locations_datawriter_ = publisher_->create_datawriter(*charger_locations_topic_, writer_qos, nullptr, eprosima::safedds::dds::NONE_STATUS_MASK);
    charger_locations_writer_ = downcast_writer<safe_edge::pilot_server::ChargerLocationTypeSupport>(charger_locations_datawriter_);

    service_heartbeat_datawriter_ = publisher_->create_datawriter(*service_heartbeat_topic_, writer_qos, nullptr, eprosima::safedds::dds::NONE_STATUS_MASK);
    service_heartbeat_writer_ = downcast_writer<safe_edge::common::ServiceHeartbeatTypeSupport>(service_heartbeat_datawriter_);

    eprosima::safedds::dds::DataReaderQos reader_qos{};
    reader_qos.reliability().kind = eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    server_query_datareader_ = subscriber_->create_datareader(
        *server_query_topic_,
        reader_qos,
        &server_query_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    server_query_reader_ = downcast_reader<safe_edge::pilot_server::ServerQueryTypeSupport>(server_query_datareader_);

    const bool success =
            nullptr != charger_locations_writer_ &&
            nullptr != service_heartbeat_writer_ &&
            nullptr != server_query_reader_;

    if (!success)
    {
        std::cerr << "[server] Failed to create endpoints" << std::endl;
    }

    return success;
}

bool ServerNode::enable_entities()
{
    bool enabled = true;
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == publisher_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == charger_locations_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == service_heartbeat_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == subscriber_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == server_query_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == participant_->enable());

    if (!enabled)
    {
        std::cerr << "[server] Failed to enable DDS entities" << std::endl;
    }

    return enabled;
}

bool ServerNode::create_executor()
{
    executor_ = factory_.create_default_executor();

    if (nullptr == executor_)
    {
        std::cerr << "[server] Failed to create executor" << std::endl;
        return false;
    }

    return true;
}

void ServerNode::on_server_query_received(
        const safe_edge::pilot_server::ServerQuery& query)
{
    std::cout << "[server] DDS query from=" << query.requested_by
              << " type=" << static_cast<int>(query.requested_data_type) << std::endl;
    request_pilot_server_data(query.requested_data_type);
}

void ServerNode::log_subscription_match(
        const char* topic_name,
        int32_t total_count) const
{
    std::cout << "[server] Subscription matched on " << topic_name
              << " total_count=" << total_count << std::endl;
}

void ServerNode::log_publication_match(
        const char* topic_name,
        int32_t total_count) const
{
    std::cout << "[server] Publication matched on " << topic_name
              << " total_count=" << total_count << std::endl;
}

const char* ServerNode::resolve_endpoint(
        safe_edge::pilot_server::RequestedDataType resource) const noexcept
{
    switch (resource)
    {
        case safe_edge::pilot_server::RequestedDataType::CHARGER_LOCATION:
            return runtime_config_.charger_locations_endpoint.c_str();
        case safe_edge::pilot_server::RequestedDataType::CHARGER_TYPE:
            return runtime_config_.charger_types_endpoint.c_str();
        case safe_edge::pilot_server::RequestedDataType::CHARGING_SESSION:
            return runtime_config_.charging_sessions_endpoint.c_str();
        case safe_edge::pilot_server::RequestedDataType::TRANSIT_HEALTH:
            return runtime_config_.transit_health_endpoint.c_str();
        case safe_edge::pilot_server::RequestedDataType::TRANSIT_METRICS:
            return runtime_config_.transit_metrics_endpoint.c_str();
        default:
            return nullptr;
    }
}

void ServerNode::request_pilot_server_data(
        safe_edge::pilot_server::RequestedDataType resource) noexcept
{
    const char* endpoint = resolve_endpoint(resource);
    if (nullptr == endpoint)
    {
        std::cerr << "[server] Unsupported resource=" << static_cast<int>(resource) << std::endl;
        return;
    }

    std::cout << "[server] Request resource=" << static_cast<int>(resource)
              << " endpoint=" << endpoint << std::endl;

    const std::string body = pilot_client_.fetch(endpoint);
    if (body.empty())
    {
        std::cerr << "[server] HTTP fetch failed endpoint=" << endpoint << std::endl;
    }
    else if (resource == safe_edge::pilot_server::RequestedDataType::CHARGER_LOCATION
             && nullptr != charger_locations_writer_)
    {
        const auto parsed =
            safe_edge::server::common::PilotServerPayloadParser::parse_charger_locations(body);
        safe_edge::server::common::PilotServerPublishHelper::publish_charger_locations(
            *charger_locations_writer_, parsed);
        std::cout << "[server] Published charger_locations count=" << parsed.size()
                  << " (from HTTP)" << std::endl;
    }
    else if (!body.empty())
    {
        // TODO: add writers and parsers for other resource types
        std::cout << "[server] HTTP response received resource="
                  << static_cast<int>(resource)
                  << " bytes=" << body.size()
                  << " (parse not yet implemented)" << std::endl;
    }

    // Stub: preserve existing charger_locations publication until HTTP response is confirmed correct
    if (resource == safe_edge::pilot_server::RequestedDataType::CHARGER_LOCATION
            && nullptr != charger_locations_writer_)
    {
        const auto locations = build_charger_locations();
        for (size_t i = 0; i < locations.size(); ++i)
        {
            charger_locations_writer_->write(locations[i], eprosima::safedds::dds::HANDLE_NIL);
        }
        std::cout << "[server] Published charger_locations count=" << locations.size()
                  << " (stub)" << std::endl;
    }
}

void ServerNode::periodic_refresh_all_resources() noexcept
{
    static constexpr safe_edge::pilot_server::RequestedDataType all_resources[] = {
        safe_edge::pilot_server::RequestedDataType::CHARGER_LOCATION,
        safe_edge::pilot_server::RequestedDataType::CHARGER_TYPE,
        safe_edge::pilot_server::RequestedDataType::CHARGING_SESSION,
        safe_edge::pilot_server::RequestedDataType::TRANSIT_HEALTH,
        safe_edge::pilot_server::RequestedDataType::TRANSIT_METRICS,
    };

    for (const auto resource : all_resources)
    {
        std::cout << "[server] Periodic refresh resource=" << static_cast<int>(resource) << std::endl;
        request_pilot_server_data(resource);
    }
}

void ServerNode::log_uptime() const noexcept
{
    const auto elapsed = std::chrono::steady_clock::now() - start_time_;
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    std::cout << "[server] Uptime=" << seconds << "s" << std::endl;
}

void ServerNode::publish_heartbeat()
{
    safe_edge::common::ServiceHeartbeat heartbeat{};
    heartbeat.header_st.source = "server";
    heartbeat.service_name = "server";
    heartbeat.status = safe_edge::common::HealthStatus::HEALTH_OK;
    heartbeat.detail = "running";

    if (eprosima::safedds::dds::ReturnCode::OK !=
            service_heartbeat_writer_->write(heartbeat, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[server] Failed to publish ServiceHeartbeat" << std::endl;
        return;
    }

    std::cout << "[server] Published ServiceHeartbeat" << std::endl;
}

eprosima::safedds::execution::TimePoint ServerNode::next_wakeup_time() const noexcept
{
    eprosima::safedds::execution::TimePoint next = executor_->get_next_work_timepoint();
    next = eprosima::safedds::execution::TimePoint::min(next, heartbeat_timer_.next_trigger());
    next = eprosima::safedds::execution::TimePoint::min(next, refresh_timer_.next_trigger());
    next = eprosima::safedds::execution::TimePoint::min(next, uptime_timer_.next_trigger());
    return next;
}

} // namespace nodes
} // namespace server
} // namespace safe_edge
