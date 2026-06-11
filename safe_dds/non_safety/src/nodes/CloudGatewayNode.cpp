#include <safe_edge/non_safety_domain/nodes/CloudGatewayNode.hpp>

#include <safe_edge/non_safety_domain/common/TopicNames.hpp>

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
#include <safedds/transport.hpp>

#include <iostream>

namespace safe_edge {
namespace non_safety_domain {
namespace nodes {

namespace {

constexpr eprosima::safedds::execution::TimePeriod TIMEOUT = {5, 0};

template<typename TypeSupportT>
bool register_type(
        eprosima::safedds::dds::DomainParticipant& participant,
        TypeSupportT& type_support,
        const char* label)
{
    if (eprosima::safedds::dds::ReturnCode::OK != type_support.register_type(participant, type_support.get_type_name()))
    {
        std::cerr << "[cloud_gateway] Failed to register type: " << label << std::endl;
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
eprosima::safedds::dds::TypedDataReader<TypeSupportT>* downcast_reader(
        eprosima::safedds::dds::DataReader* reader)
{
    if (nullptr == reader)
    {
        return nullptr;
    }

    return eprosima::safedds::dds::TypedDataReader<TypeSupportT>::downcast(*reader);
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

} // namespace

CloudGatewayNode::ParticipantListener::ParticipantListener(
        CloudGatewayNode& owner)
    : owner_(owner)
{
}

void CloudGatewayNode::ParticipantListener::on_subscription_matched(
        eprosima::safedds::dds::DataReader& reader,
        const eprosima::safedds::dds::SubscriptionMatchedStatus& info) noexcept
{
    owner_.log_subscription_match(reader.get_topicdescription().get_name().const_string_data(), info.total_count);
}

void CloudGatewayNode::ParticipantListener::on_publication_matched(
        eprosima::safedds::dds::DataWriter& writer,
        const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept
{
    owner_.log_publication_match(writer.get_topic().get_name().const_string_data(), info.total_count);
}

CloudGatewayNode::ChargerLocationsListener::ChargerLocationsListener(
        CloudGatewayNode& owner)
    : owner_(owner)
{
}

void CloudGatewayNode::ChargerLocationsListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::ChargerLocationTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[cloud_gateway] Failed to downcast charger_locations reader" << std::endl;
        return;
    }

    safe_edge::pilot_server::ChargerLocation sample{};
    safe_edge::pilot_server::ChargerLocationSeq batch{};
    eprosima::safedds::dds::SampleInfo info{};
    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            batch.push_back(sample);
        }
    }
    if (!batch.empty())
    {
        owner_.on_charger_locations_received(batch);
    }
}

CloudGatewayNode::ChargerTypesListener::ChargerTypesListener(
        CloudGatewayNode& owner)
    : owner_(owner)
{
}

void CloudGatewayNode::ChargerTypesListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::ChargerTypeTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[cloud_gateway] Failed to downcast charger_types reader" << std::endl;
        return;
    }

    safe_edge::pilot_server::ChargerType sample{};
    safe_edge::pilot_server::ChargerTypeSeq batch{};
    eprosima::safedds::dds::SampleInfo info{};
    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            batch.push_back(sample);
        }
    }
    if (!batch.empty())
    {
        owner_.on_charger_types_received(batch);
    }
}

CloudGatewayNode::ChargingSessionsListener::ChargingSessionsListener(
        CloudGatewayNode& owner)
    : owner_(owner)
{
}

void CloudGatewayNode::ChargingSessionsListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::ChargingSessionTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[cloud_gateway] Failed to downcast charging_sessions reader" << std::endl;
        return;
    }

    safe_edge::pilot_server::ChargingSession sample{};
    safe_edge::pilot_server::ChargingSessionSeq batch{};
    eprosima::safedds::dds::SampleInfo info{};
    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            batch.push_back(sample);
        }
    }
    if (!batch.empty())
    {
        owner_.on_charging_sessions_received(batch);
    }
}

CloudGatewayNode::TransitHealthListener::TransitHealthListener(
        CloudGatewayNode& owner)
    : owner_(owner)
{
}

void CloudGatewayNode::TransitHealthListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::TransitHealthTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[cloud_gateway] Failed to downcast transit_health reader" << std::endl;
        return;
    }

    safe_edge::pilot_server::TransitHealth sample{};
    eprosima::safedds::dds::SampleInfo info{};
    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            owner_.on_transit_health_received(sample);
        }
    }
}

CloudGatewayNode::RouteMetricsListener::RouteMetricsListener(
        CloudGatewayNode& owner)
    : owner_(owner)
{
}

void CloudGatewayNode::RouteMetricsListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::RouteMetricTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[cloud_gateway] Failed to downcast route_metrics reader" << std::endl;
        return;
    }

    safe_edge::pilot_server::RouteMetric sample{};
    safe_edge::pilot_server::RouteMetricSeq batch{};
    eprosima::safedds::dds::SampleInfo info{};
    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            batch.push_back(sample);
        }
    }
    if (!batch.empty())
    {
        owner_.on_route_metrics_received(batch);
    }
}

CloudGatewayNode::TransitMetricsListener::TransitMetricsListener(
        CloudGatewayNode& owner)
    : owner_(owner)
{
}

void CloudGatewayNode::TransitMetricsListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::TransitMetricsTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[cloud_gateway] Failed to downcast transit_metrics reader" << std::endl;
        return;
    }

    safe_edge::pilot_server::TransitMetrics sample{};
    eprosima::safedds::dds::SampleInfo info{};
    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            owner_.on_transit_metrics_received(sample);
        }
    }
}

CloudGatewayNode::HeartbeatListener::HeartbeatListener(
        CloudGatewayNode& owner)
    : owner_(owner)
{
}

void CloudGatewayNode::HeartbeatListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::common::ServiceHeartbeatTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[cloud_gateway] Failed to downcast heartbeat reader" << std::endl;
        return;
    }

    safe_edge::common::ServiceHeartbeat sample{};
    eprosima::safedds::dds::SampleInfo info{};
    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            owner_.on_peer_heartbeat_received(sample);
        }
    }
}

CloudGatewayNode::ChargingQueryListener::ChargingQueryListener(
        CloudGatewayNode& owner)
    : owner_(owner)
{
}

void CloudGatewayNode::ChargingQueryListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::internal::ChargingQueryTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[cloud_gateway] Failed to downcast charging_query reader" << std::endl;
        return;
    }

    safe_edge::internal::ChargingQuery sample{};
    eprosima::safedds::dds::SampleInfo info{};
    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            owner_.on_charging_query_received(sample);
        }
    }
}

CloudGatewayNode::CloudGatewayNode(
        const common::RuntimeConfig& runtime_config)
    : runtime_config_(runtime_config)
    , header_factory_(runtime_config.source_name)
    , participant_listener_(*this)
    , charger_locations_listener_(*this)
    , charger_types_listener_(*this)
    , charging_sessions_listener_(*this)
    , transit_health_listener_(*this)
    , route_metrics_listener_(*this)
    , transit_metrics_listener_(*this)
    , heartbeat_listener_(*this)
    , charging_query_listener_(*this)
    , heartbeat_timer_(TIMEOUT)
{
}

int CloudGatewayNode::run()
{
    if (!initialize())
    {
        return 1;
    }

    start_timers();
    std::cout << "[cloud_gateway] [START] Running with participant port " << runtime_config_.participant_port << std::endl;

    while (true)
    {
        while (executor_->has_pending_work())
        {
            executor_->spin(eprosima::safedds::execution::TIME_ZERO);
        }

        if (heartbeat_timer_.is_triggered_and_reset())
        {
            publish_heartbeat();

            constexpr uint64_t SERVER_HB_TIMEOUT_MS = 10000U;
            if (last_server_hb_ms_ > 0U &&
                (common::HeaderFactory::now_ms() - last_server_hb_ms_) > SERVER_HB_TIMEOUT_MS)
            {
                if (server_available_)
                {
                    server_available_ = false;
                    publish_server_availability_status(false, "server_lost");
                }
            }
        }

        executor_->spin(next_wakeup_time());
    }

    return 0;
}

bool CloudGatewayNode::initialize()
{
    return create_participant() &&
           register_types() &&
           create_topics() &&
           create_endpoints() &&
           enable_entities() &&
           create_executor();
}

bool CloudGatewayNode::create_participant()
{
    eprosima::safedds::dds::DomainParticipantQos participant_qos{};
    eprosima::safedds::memory::container::StaticString256 participant_name(runtime_config_.participant_name.c_str());
    participant_qos.participant_name() = participant_name;
    participant_qos.wire_protocol_qos().announced_locator = eprosima::safedds::transport::Locator::from_ipv4(
        runtime_config_.own_ip,
        runtime_config_.participant_port);
    participant_qos.wire_protocol_qos().use_multicast_discovery = false;
    for (std::size_t i = 0U; i < runtime_config_.initial_peer_count; ++i)
    {
        const auto& peer_ip =
                (runtime_config_.initial_peer_ports[i] >= 8011U && runtime_config_.initial_peer_ports[i] <= 8013U) ?
                runtime_config_.own_ip :
                runtime_config_.cross_domain_peer_ip;
        initial_peers_.add(eprosima::safedds::transport::Locator::from_ipv4(peer_ip, runtime_config_.initial_peer_ports[i]));
    }
    participant_qos.wire_protocol_qos().initial_peers = &initial_peers_;

    participant_ = factory_.create_participant(
        runtime_config_.domain_id,
        participant_qos,
        &participant_listener_,
        eprosima::safedds::dds::PUBLICATION_MATCHED_STATUS |
        eprosima::safedds::dds::SUBSCRIPTION_MATCHED_STATUS);

    if (nullptr == participant_)
    {
        std::cerr << "[cloud_gateway] Failed to create participant" << std::endl;
        return false;
    }

    return true;
}

bool CloudGatewayNode::register_types()
{
    return register_type(*participant_, charger_locations_type_support_, "ChargerLocationSeq") &&
           register_type(*participant_, charger_types_type_support_, "ChargerTypeSeq") &&
           register_type(*participant_, charging_sessions_type_support_, "ChargingSessionSeq") &&
           register_type(*participant_, transit_health_type_support_, "TransitHealth") &&
           register_type(*participant_, route_metrics_type_support_, "RouteMetricSeq") &&
           register_type(*participant_, transit_metrics_type_support_, "TransitMetrics") &&
           register_type(*participant_, service_heartbeat_type_support_, "ServiceHeartbeat") &&
           register_type(*participant_, charging_query_type_support_, "ChargingQuery") &&
           register_type(*participant_, charging_response_type_support_, "ChargingResponse") &&
           register_type(*participant_, server_query_type_support_, "ServerQuery") &&
           register_type(*participant_, server_availability_status_type_support_, "ServerAvailabilityStatus");
}

bool CloudGatewayNode::create_topics()
{
    charger_locations_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::charger_locations());
    charger_locations_topic_ = create_topic(*participant_, charger_locations_topic_name_, charger_locations_type_support_);
    if (nullptr == charger_locations_topic_) { std::cerr << "[cloud_gateway] Failed to create topic: charger_locations" << std::endl; return false; }

    charger_types_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::charger_types());
    charger_types_topic_ = create_topic(*participant_, charger_types_topic_name_, charger_types_type_support_);
    if (nullptr == charger_types_topic_) { std::cerr << "[cloud_gateway] Failed to create topic: charger_types" << std::endl; return false; }

    charging_sessions_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::charging_sessions());
    charging_sessions_topic_ = create_topic(*participant_, charging_sessions_topic_name_, charging_sessions_type_support_);
    if (nullptr == charging_sessions_topic_) { std::cerr << "[cloud_gateway] Failed to create topic: charging_sessions" << std::endl; return false; }

    transit_health_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::transit_health());
    transit_health_topic_ = create_topic(*participant_, transit_health_topic_name_, transit_health_type_support_);
    if (nullptr == transit_health_topic_) { std::cerr << "[cloud_gateway] Failed to create topic: transit_health" << std::endl; return false; }

    route_metrics_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::route_metrics());
    route_metrics_topic_ = create_topic(*participant_, route_metrics_topic_name_, route_metrics_type_support_);
    if (nullptr == route_metrics_topic_) { std::cerr << "[cloud_gateway] Failed to create topic: route_metrics" << std::endl; return false; }

    transit_metrics_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::transit_metrics());
    transit_metrics_topic_ = create_topic(*participant_, transit_metrics_topic_name_, transit_metrics_type_support_);
    if (nullptr == transit_metrics_topic_) { std::cerr << "[cloud_gateway] Failed to create topic: transit_metrics" << std::endl; return false; }

    service_heartbeat_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::service_heartbeat());
    service_heartbeat_topic_ = create_topic(*participant_, service_heartbeat_topic_name_, service_heartbeat_type_support_);
    if (nullptr == service_heartbeat_topic_) { std::cerr << "[cloud_gateway] Failed to create topic: service_heartbeat" << std::endl; return false; }

    charging_query_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::charging_query());
    charging_query_topic_ = create_topic(*participant_, charging_query_topic_name_, charging_query_type_support_);
    if (nullptr == charging_query_topic_) { std::cerr << "[cloud_gateway] Failed to create topic: charging_query" << std::endl; return false; }

    charging_response_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::charging_response());
    charging_response_topic_ = create_topic(*participant_, charging_response_topic_name_, charging_response_type_support_);
    if (nullptr == charging_response_topic_) { std::cerr << "[cloud_gateway] Failed to create topic: charging_response" << std::endl; return false; }

    server_query_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::server_query());
    server_query_topic_ = create_topic(*participant_, server_query_topic_name_, server_query_type_support_);
    if (nullptr == server_query_topic_) { std::cerr << "[cloud_gateway] Failed to create topic: server_query" << std::endl; return false; }

    server_availability_status_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::server_availability_status());
    server_availability_status_topic_ = create_topic(*participant_, server_availability_status_topic_name_, server_availability_status_type_support_);
    if (nullptr == server_availability_status_topic_) { std::cerr << "[cloud_gateway] Failed to create topic: server_availability_status" << std::endl; return false; }

    return true;
}

bool CloudGatewayNode::create_endpoints()
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
        std::cerr << "[cloud_gateway] Failed to create publisher or subscriber" << std::endl;
        return false;
    }

    eprosima::safedds::dds::DataWriterQos writer_qos{};
    writer_qos.reliability().kind = eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    service_heartbeat_datawriter_ = publisher_->create_datawriter(
        *service_heartbeat_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    service_heartbeat_writer_ = downcast_writer<safe_edge::common::ServiceHeartbeatTypeSupport>(service_heartbeat_datawriter_);

    charging_response_datawriter_ = publisher_->create_datawriter(
        *charging_response_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    charging_response_writer_ = downcast_writer<safe_edge::internal::ChargingResponseTypeSupport>(charging_response_datawriter_);

    server_query_datawriter_ = publisher_->create_datawriter(
        *server_query_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    server_query_writer_ = downcast_writer<safe_edge::pilot_server::ServerQueryTypeSupport>(server_query_datawriter_);

    server_availability_status_datawriter_ = publisher_->create_datawriter(
        *server_availability_status_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    server_availability_status_writer_ = downcast_writer<safe_edge::internal::ServerAvailabilityStatusTypeSupport>(server_availability_status_datawriter_);

    eprosima::safedds::dds::DataReaderQos reader_qos{};
    reader_qos.reliability().kind = eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    charger_locations_datareader_ = subscriber_->create_datareader(*charger_locations_topic_, reader_qos, &charger_locations_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    charger_types_datareader_ = subscriber_->create_datareader(*charger_types_topic_, reader_qos, &charger_types_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    charging_sessions_datareader_ = subscriber_->create_datareader(*charging_sessions_topic_, reader_qos, &charging_sessions_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    transit_health_datareader_ = subscriber_->create_datareader(*transit_health_topic_, reader_qos, &transit_health_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    route_metrics_datareader_ = subscriber_->create_datareader(*route_metrics_topic_, reader_qos, &route_metrics_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    transit_metrics_datareader_ = subscriber_->create_datareader(*transit_metrics_topic_, reader_qos, &transit_metrics_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    heartbeat_datareader_ = subscriber_->create_datareader(*service_heartbeat_topic_, reader_qos, &heartbeat_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    charging_query_datareader_ = subscriber_->create_datareader(*charging_query_topic_, reader_qos, &charging_query_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);

    charger_locations_reader_ = downcast_reader<safe_edge::pilot_server::ChargerLocationTypeSupport>(charger_locations_datareader_);
    charger_types_reader_ = downcast_reader<safe_edge::pilot_server::ChargerTypeTypeSupport>(charger_types_datareader_);
    charging_sessions_reader_ = downcast_reader<safe_edge::pilot_server::ChargingSessionTypeSupport>(charging_sessions_datareader_);
    transit_health_reader_ = downcast_reader<safe_edge::pilot_server::TransitHealthTypeSupport>(transit_health_datareader_);
    route_metrics_reader_ = downcast_reader<safe_edge::pilot_server::RouteMetricTypeSupport>(route_metrics_datareader_);
    transit_metrics_reader_ = downcast_reader<safe_edge::pilot_server::TransitMetricsTypeSupport>(transit_metrics_datareader_);
    heartbeat_reader_ = downcast_reader<safe_edge::common::ServiceHeartbeatTypeSupport>(heartbeat_datareader_);
    charging_query_reader_ = downcast_reader<safe_edge::internal::ChargingQueryTypeSupport>(charging_query_datareader_);

    const bool success = nullptr != service_heartbeat_writer_ &&
            nullptr != charging_response_writer_ &&
            nullptr != server_query_writer_ &&
            nullptr != server_availability_status_writer_ &&
            nullptr != charger_locations_reader_ &&
            nullptr != charger_types_reader_ &&
            nullptr != charging_sessions_reader_ &&
            nullptr != transit_health_reader_ &&
            nullptr != route_metrics_reader_ &&
            nullptr != transit_metrics_reader_ &&
            nullptr != heartbeat_reader_ &&
            nullptr != charging_query_reader_;

    if (!success)
    {
        std::cerr << "[cloud_gateway] Failed to create endpoints" << std::endl;
        return false;
    }

    return true;
}

bool CloudGatewayNode::enable_entities()
{
    bool enabled = true;
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == publisher_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == service_heartbeat_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == charging_response_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == server_query_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == server_availability_status_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == subscriber_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == charger_locations_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == charger_types_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == charging_sessions_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == transit_health_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == route_metrics_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == transit_metrics_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == heartbeat_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == charging_query_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == participant_->enable());

    if (!enabled)
    {
        std::cerr << "[cloud_gateway] Failed to enable DDS entities" << std::endl;
    }

    return enabled;
}

bool CloudGatewayNode::create_executor()
{
    executor_ = factory_.create_default_executor();

    if (nullptr == executor_)
    {
        std::cerr << "[cloud_gateway] Failed to create executor" << std::endl;
        return false;
    }

    return true;
}

void CloudGatewayNode::start_timers() noexcept
{
    heartbeat_timer_.start();
}

void CloudGatewayNode::on_charger_locations_received(
        const safe_edge::pilot_server::ChargerLocationSeq& locations)
{
    cached_charger_count_ = 0;
    for (uint32_t i = 0; i < locations.size() && cached_charger_count_ < 3; ++i)
    {
        cached_chargers_[cached_charger_count_++] = locations[i];
    }
    std::cout << "[cloud_gateway] Received ChargerLocationSeq count=" << locations.size() << std::endl;

    if (charger_query_pending_)
    {
        charger_query_pending_ = false;
        publish_charging_response();
    }
}

void CloudGatewayNode::on_charger_types_received(
        const safe_edge::pilot_server::ChargerTypeSeq& types)
{
    std::cout << "[cloud_gateway] Received ChargerTypeSeq count=" << types.size() << std::endl;
}

void CloudGatewayNode::on_charging_sessions_received(
        const safe_edge::pilot_server::ChargingSessionSeq& sessions)
{
    std::cout << "[cloud_gateway] Received ChargingSessionSeq count=" << sessions.size() << std::endl;
}

void CloudGatewayNode::on_transit_health_received(
        const safe_edge::pilot_server::TransitHealth& health)
{
    std::cout << "[cloud_gateway] Received TransitHealth status=" << health.status
              << " last_fetch_ts=" << health.last_fetch_ts << std::endl;
}

void CloudGatewayNode::on_route_metrics_received(
        const safe_edge::pilot_server::RouteMetricSeq& metrics)
{
    std::cout << "[cloud_gateway] Received RouteMetricSeq count=" << metrics.size() << std::endl;
}

void CloudGatewayNode::on_transit_metrics_received(
        const safe_edge::pilot_server::TransitMetrics& metrics)
{
    std::cout << "[cloud_gateway] Received TransitMetrics routes=" << metrics.by_route.size()
              << " vehicles_seen=" << metrics.vehicles_seen << std::endl;
}

void CloudGatewayNode::on_charging_query_received(
        const safe_edge::internal::ChargingQuery& query)
{
    std::cout << "[cloud_gateway] Received ChargingQuery soc_pct=" << query.soc_pct << std::endl;

    charger_query_pending_ = true;

    safe_edge::pilot_server::ServerQuery server_request{};
    server_request.requested_by = runtime_config_.source_name;
    server_request.requested_data_type = safe_edge::pilot_server::RequestedDataType::CHARGER_LOCATION;

    std::cout << "[cloud_gateway] Forwarding query to server" << std::endl;

    if (eprosima::safedds::dds::ReturnCode::OK !=
            server_query_writer_->write(server_request, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[cloud_gateway] Failed to publish ServerQuery" << std::endl;
        charger_query_pending_ = false;
    }
}

void CloudGatewayNode::publish_charging_response()
{
    safe_edge::internal::ChargingResponse response{};
    response.header = header_factory_.make_header("charging_response");

    if (cached_charger_count_ > 0)
    {
        response.preferred_charger_id = cached_chargers_[0].id;
        response.preferred_charger_name = cached_chargers_[0].name;
        response.has_charger = true;
        std::cout << "[cloud_gateway] Responding with charger id=" << response.preferred_charger_id
                  << " name=" << response.preferred_charger_name << std::endl;
    }
    else
    {
        response.preferred_charger_id = 0;
        response.has_charger = false;
        std::cout << "[cloud_gateway] No cached chargers, responding with has_charger=false" << std::endl;
    }

    if (eprosima::safedds::dds::ReturnCode::OK != charging_response_writer_->write(response, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[cloud_gateway] Failed to publish ChargingResponse" << std::endl;
        return;
    }

    std::cout << "[cloud_gateway] Published ChargingResponse" << std::endl;
}

void CloudGatewayNode::publish_heartbeat()
{
    safe_edge::common::ServiceHeartbeat heartbeat;
    heartbeat.header_st = header_factory_.make_header("service_heartbeat");
    heartbeat.service_name = runtime_config_.service_name;
    heartbeat.status = safe_edge::common::HealthStatus::HEALTH_OK;
    heartbeat.detail = "running";

    if (eprosima::safedds::dds::ReturnCode::OK != service_heartbeat_writer_->write(heartbeat, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[cloud_gateway] Failed to publish ServiceHeartbeat" << std::endl;
        return;
    }

    std::cout << "[cloud_gateway] Published ServiceHeartbeat" << std::endl;
}

void CloudGatewayNode::on_peer_heartbeat_received(
        const safe_edge::common::ServiceHeartbeat& heartbeat)
{
    if (heartbeat.service_name == runtime_config_.service_name)
    {
        return;
    }

    if (heartbeat.service_name == "server")
    {
        const bool was_available = server_available_;
        last_server_hb_ms_ = common::HeaderFactory::now_ms();
        server_available_ = true;
        if (!was_available)
        {
            publish_server_availability_status(true, "server_up");
        }
        return;
    }

    static_cast<void>(heartbeat);
}

void CloudGatewayNode::publish_server_availability_status(
        bool server_available,
        const char* detail)
{
    safe_edge::internal::ServerAvailabilityStatus msg{};
    msg.header = header_factory_.make_header("server_availability_status");
    msg.server_available = server_available;
    msg.detail = detail;

    if (eprosima::safedds::dds::ReturnCode::OK !=
            server_availability_status_writer_->write(msg, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[cloud_gateway] Failed to publish ServerAvailabilityStatus" << std::endl;
        return;
    }

    std::cout << "[cloud_gateway] Published ServerAvailabilityStatus server_available="
              << (server_available ? "true" : "false")
              << " detail=" << detail << std::endl;
}

void CloudGatewayNode::log_subscription_match(
        const char* topic_name,
        int32_t total_count) const
{
    std::cout << "[cloud_gateway] Subscription matched on " << topic_name
              << " total_count=" << total_count << std::endl;
}

void CloudGatewayNode::log_publication_match(
        const char* topic_name,
        int32_t total_count) const
{
    std::cout << "[cloud_gateway] Publication matched on " << topic_name
              << " total_count=" << total_count << std::endl;
}

eprosima::safedds::execution::TimePoint CloudGatewayNode::next_wakeup_time() const noexcept
{
    eprosima::safedds::execution::TimePoint next = executor_->get_next_work_timepoint();
    next = eprosima::safedds::execution::TimePoint::min(next, heartbeat_timer_.next_trigger());
    return next;
}

} // namespace nodes
} // namespace non_safety_domain
} // namespace safe_edge
