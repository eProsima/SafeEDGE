#include <safe_edge/server/nodes/ServerNode.hpp>
#include <safe_edge/server/common/PilotServerPayloadParser.hpp>
#include <safe_edge/server/common/PilotServerPublishHelper.hpp>
#include <safe_edge/server/common/TopicNames.hpp>

#include <commonPubSubTypes.hpp>
#include <pilot_serverPubSubTypes.hpp>

#include <fastdds/dds/core/status/StatusMask.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/subscriber/qos/SubscriberQos.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/qos/TopicQos.hpp>
#include <fastdds/rtps/common/Locator.hpp>
#include <fastdds/utils/IPLocator.hpp>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace safe_edge {
namespace server {
namespace nodes {

namespace {

static std::vector<safe_edge::pilot_server::ChargerLocation> build_charger_locations()
{
    std::vector<safe_edge::pilot_server::ChargerLocation> locations;

    safe_edge::pilot_server::ChargerLocation loc1;
    loc1.id(1);
    loc1.name("Madrid North Hub");
    safe_edge::common::GeoPoint pos1;
    pos1.latitude(40.4637F);
    pos1.longitude(-3.7492F);
    loc1.position(pos1);
    locations.push_back(loc1);

    safe_edge::pilot_server::ChargerLocation loc2;
    loc2.id(2);
    loc2.name("Barcelona Port Station");
    safe_edge::common::GeoPoint pos2;
    pos2.latitude(41.3851F);
    pos2.longitude(2.1734F);
    loc2.position(pos2);
    locations.push_back(loc2);

    safe_edge::pilot_server::ChargerLocation loc3;
    loc3.id(3);
    loc3.name("Valencia Depot");
    safe_edge::common::GeoPoint pos3;
    pos3.latitude(39.4699F);
    pos3.longitude(-0.3763F);
    loc3.position(pos3);
    locations.push_back(loc3);

    return locations;
}

} // namespace

ServerNode::ParticipantListener::ParticipantListener(
        ServerNode& owner)
    : owner_(owner)
{
}

void ServerNode::ParticipantListener::on_subscription_matched(
        eprosima::fastdds::dds::DataReader* reader,
        const eprosima::fastdds::dds::SubscriptionMatchedStatus& info) noexcept
{
    owner_.log_subscription_match(
        reader->get_topicdescription()->get_name().c_str(), info.total_count);
}

void ServerNode::ParticipantListener::on_publication_matched(
        eprosima::fastdds::dds::DataWriter* writer,
        const eprosima::fastdds::dds::PublicationMatchedStatus& info) noexcept
{
    owner_.log_publication_match(
        writer->get_topic()->get_name().c_str(), info.total_count);

    if (writer == owner_.charger_locations_datawriter_ && info.total_count_change > 0)
    {
        owner_.request_pilot_server_data(
            safe_edge::pilot_server::RequestedDataType::CHARGER_LOCATION);
    }
}

ServerNode::ServerQueryListener::ServerQueryListener(
        ServerNode& owner)
    : owner_(owner)
{
}

void ServerNode::ServerQueryListener::on_data_available(
        eprosima::fastdds::dds::DataReader* reader) noexcept
{
    safe_edge::pilot_server::ServerQuery sample{};
    eprosima::fastdds::dds::SampleInfo info{};
    while (reader->take_next_sample(&sample, &info) == eprosima::fastdds::dds::RETCODE_OK)
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
    , next_heartbeat_fire_(std::chrono::steady_clock::now() + std::chrono::milliseconds(100))
    , next_refresh_fire_(std::chrono::steady_clock::now() + std::chrono::seconds(30))
    , next_server_liveliness_fire_(std::chrono::steady_clock::now() + std::chrono::milliseconds(2500))
    , next_uptime_fire_(std::chrono::steady_clock::now() + std::chrono::seconds(300))
    , start_time_(std::chrono::steady_clock::now())
    , charger_location_type_support_(new safe_edge::pilot_server::ChargerLocationPubSubType())
    , server_query_type_support_(new safe_edge::pilot_server::ServerQueryPubSubType())
    , service_heartbeat_type_support_(new safe_edge::common::ServiceHeartbeatPubSubType())
{
}

int ServerNode::run()
{
    if (!initialize())
    {
        return 1;
    }

    std::cout << "[server] [START] Running with participant port "
              << runtime_config_.participant_port << std::endl;
    std::cout << "[server] PilotServer base_url=" << runtime_config_.pilot_server_base_url
              << " api_key=***" << std::endl;

    while (true)
    {
        const auto next = std::min(
            {next_heartbeat_fire_, next_refresh_fire_, next_server_liveliness_fire_, next_uptime_fire_});
        std::this_thread::sleep_until(next);

        const auto now = std::chrono::steady_clock::now();

        if (now >= next_server_liveliness_fire_)
        {
            check_pilot_server_liveliness();
            next_server_liveliness_fire_ += std::chrono::milliseconds(2500);
        }
        if (now >= next_heartbeat_fire_ && pilot_server_available_)
        {
            publish_heartbeat();
            next_heartbeat_fire_ += std::chrono::milliseconds(100);
        }
        if (now >= next_refresh_fire_ && pilot_server_available_)
        {
            periodic_refresh_all_resources();
            next_refresh_fire_ += std::chrono::seconds(30);
        }
        if (now >= next_uptime_fire_)
        {
            log_uptime();
            next_uptime_fire_ += std::chrono::seconds(300);
        }
    }

    return 0;
}

bool ServerNode::initialize()
{
    return create_participant() &&
           register_types() &&
           create_topics() &&
           create_endpoints() &&
           enable_entities();
}

bool ServerNode::create_participant()
{
    eprosima::fastdds::dds::DomainParticipantQos participant_qos{};
    participant_qos.name(runtime_config_.participant_name);

    eprosima::fastdds::rtps::Locator_t announced_locator;
    eprosima::fastdds::rtps::IPLocator::setIPv4(announced_locator, runtime_config_.own_ip);
    announced_locator.port = runtime_config_.participant_port;
    participant_qos.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(announced_locator);
    announced_locator.port = 0;
    participant_qos.wire_protocol().default_unicast_locator_list.push_back(announced_locator);
    participant_qos.wire_protocol().builtin.avoid_builtin_multicast = true;

    if (!runtime_config_.initial_peers.empty())
    {
        for (const auto& p : runtime_config_.initial_peers)
        {
            eprosima::fastdds::rtps::Locator_t peer;
            eprosima::fastdds::rtps::IPLocator::setIPv4(peer, p.first);
            peer.port = p.second;
            participant_qos.wire_protocol().builtin.initialPeersList.push_back(peer);
        }
    }
    else
    {
        {
            eprosima::fastdds::rtps::Locator_t peer;
            eprosima::fastdds::rtps::IPLocator::setIPv4(peer, runtime_config_.non_safety_ip);
            peer.port = 8011U;
            participant_qos.wire_protocol().builtin.initialPeersList.push_back(peer);
        }
        {
            eprosima::fastdds::rtps::Locator_t peer;
            eprosima::fastdds::rtps::IPLocator::setIPv4(peer, runtime_config_.own_ip);
            peer.port = 8030U;
            participant_qos.wire_protocol().builtin.initialPeersList.push_back(peer);
        }
    }

    eprosima::fastdds::dds::StatusMask participant_mask =
        eprosima::fastdds::dds::StatusMask::publication_matched();
    participant_mask |= eprosima::fastdds::dds::StatusMask::subscription_matched();

    participant_ = eprosima::fastdds::dds::DomainParticipantFactory::get_instance()
        ->create_participant(runtime_config_.domain_id, participant_qos,
            &participant_listener_, participant_mask);

    if (nullptr == participant_)
    {
        std::cerr << "[server] Failed to create participant" << std::endl;
        return false;
    }

    return true;
}

bool ServerNode::register_types()
{
    if (eprosima::fastdds::dds::RETCODE_OK != charger_location_type_support_.register_type(participant_))
    {
        std::cerr << "[server] Failed to register type: ChargerLocation" << std::endl;
        return false;
    }
    if (eprosima::fastdds::dds::RETCODE_OK != server_query_type_support_.register_type(participant_))
    {
        std::cerr << "[server] Failed to register type: ServerQuery" << std::endl;
        return false;
    }
    if (eprosima::fastdds::dds::RETCODE_OK != service_heartbeat_type_support_.register_type(participant_))
    {
        std::cerr << "[server] Failed to register type: ServiceHeartbeat" << std::endl;
        return false;
    }
    return true;
}

bool ServerNode::create_topics()
{
    charger_locations_topic_name_ = common::topic_names::charger_locations();
    charger_locations_topic_ = participant_->create_topic(
        charger_locations_topic_name_,
        charger_location_type_support_.get_type_name(),
        eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
    if (nullptr == charger_locations_topic_)
    {
        std::cerr << "[server] Failed to create topic: charger_locations" << std::endl;
        return false;
    }

    server_query_topic_name_ = common::topic_names::server_query();
    server_query_topic_ = participant_->create_topic(
        server_query_topic_name_,
        server_query_type_support_.get_type_name(),
        eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
    if (nullptr == server_query_topic_)
    {
        std::cerr << "[server] Failed to create topic: server_query" << std::endl;
        return false;
    }

    service_heartbeat_topic_name_ = common::topic_names::service_heartbeat();
    service_heartbeat_topic_ = participant_->create_topic(
        service_heartbeat_topic_name_,
        service_heartbeat_type_support_.get_type_name(),
        eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
    if (nullptr == service_heartbeat_topic_)
    {
        std::cerr << "[server] Failed to create topic: service_heartbeat" << std::endl;
        return false;
    }

    return true;
}

bool ServerNode::create_endpoints()
{
    publisher_ = participant_->create_publisher(
        eprosima::fastdds::dds::PUBLISHER_QOS_DEFAULT,
        nullptr,
        eprosima::fastdds::dds::StatusMask::none());

    subscriber_ = participant_->create_subscriber(
        eprosima::fastdds::dds::SUBSCRIBER_QOS_DEFAULT,
        nullptr,
        eprosima::fastdds::dds::StatusMask::none());

    if (nullptr == publisher_ || nullptr == subscriber_)
    {
        std::cerr << "[server] Failed to create publisher or subscriber" << std::endl;
        return false;
    }

    eprosima::fastdds::dds::DataWriterQos writer_qos = eprosima::fastdds::dds::DATAWRITER_QOS_DEFAULT;
    writer_qos.reliability().kind = eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;

    charger_locations_datawriter_ = publisher_->create_datawriter(
        charger_locations_topic_, writer_qos, nullptr,
        eprosima::fastdds::dds::StatusMask::none());

    service_heartbeat_datawriter_ = publisher_->create_datawriter(
        service_heartbeat_topic_, writer_qos, nullptr,
        eprosima::fastdds::dds::StatusMask::none());

    eprosima::fastdds::dds::DataReaderQos reader_qos = eprosima::fastdds::dds::DATAREADER_QOS_DEFAULT;
    reader_qos.reliability().kind = eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;

    server_query_datareader_ = subscriber_->create_datareader(
        server_query_topic_, reader_qos,
        &server_query_listener_,
        eprosima::fastdds::dds::StatusMask::data_available());

    if (nullptr == charger_locations_datawriter_ ||
        nullptr == service_heartbeat_datawriter_ ||
        nullptr == server_query_datareader_)
    {
        std::cerr << "[server] Failed to create endpoints" << std::endl;
        return false;
    }

    return true;
}

bool ServerNode::enable_entities()
{
    bool enabled = true;
    enabled = enabled && (eprosima::fastdds::dds::RETCODE_OK == publisher_->enable());
    enabled = enabled && (eprosima::fastdds::dds::RETCODE_OK == charger_locations_datawriter_->enable());
    enabled = enabled && (eprosima::fastdds::dds::RETCODE_OK == service_heartbeat_datawriter_->enable());
    enabled = enabled && (eprosima::fastdds::dds::RETCODE_OK == subscriber_->enable());
    enabled = enabled && (eprosima::fastdds::dds::RETCODE_OK == server_query_datareader_->enable());
    enabled = enabled && (eprosima::fastdds::dds::RETCODE_OK == participant_->enable());

    if (!enabled)
    {
        std::cerr << "[server] Failed to enable DDS entities" << std::endl;
    }

    return enabled;
}

void ServerNode::on_server_query_received(
        const safe_edge::pilot_server::ServerQuery& query)
{
    std::cout << "[server] DDS query from=" << query.requested_by()
              << " type=" << static_cast<int>(query.requested_data_type()) << std::endl;
    request_pilot_server_data(query.requested_data_type());
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
             && nullptr != charger_locations_datawriter_)
    {
        const auto parsed =
            safe_edge::server::common::PilotServerPayloadParser::parse_charger_locations(body);
        safe_edge::server::common::PilotServerPublishHelper::publish_charger_locations(
            *charger_locations_datawriter_, parsed);
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

    // Stub: preserve charger_locations publication until HTTP response is confirmed correct
    if (resource == safe_edge::pilot_server::RequestedDataType::CHARGER_LOCATION
            && nullptr != charger_locations_datawriter_)
    {
        const auto locations = build_charger_locations();
        for (const auto& loc : locations)
        {
            charger_locations_datawriter_->write(
                const_cast<safe_edge::pilot_server::ChargerLocation*>(&loc),
                eprosima::fastdds::dds::HANDLE_NIL);
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

void ServerNode::check_pilot_server_liveliness() noexcept
{
    const bool reachable = pilot_client_.is_pilot_server_available();
    if (pilot_server_available_ != reachable)
    {
        std::cout << "[server] Pilot server availability changed: available="
                  << (reachable ? "true" : "false") << std::endl;
    }
    pilot_server_available_ = reachable;
}

void ServerNode::publish_heartbeat()
{
    safe_edge::common::ServiceHeartbeat heartbeat{};
    safe_edge::common::Header hdr;
    hdr.source("server");
    heartbeat.header_st(hdr);
    heartbeat.service_name("server");
    heartbeat.status(safe_edge::common::HealthStatus::HEALTH_OK);
    heartbeat.detail("running");

    if (eprosima::fastdds::dds::RETCODE_OK !=
            service_heartbeat_datawriter_->write(&heartbeat, eprosima::fastdds::dds::HANDLE_NIL))
    {
        std::cerr << "[server] Failed to publish ServiceHeartbeat" << std::endl;
        return;
    }

    std::cout << "[server] Published ServiceHeartbeat" << std::endl;
}

} // namespace nodes
} // namespace server
} // namespace safe_edge
