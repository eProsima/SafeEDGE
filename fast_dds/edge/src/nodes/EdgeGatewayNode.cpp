#include <safe_edge/edge_module/nodes/EdgeGatewayNode.hpp>

#include <safe_edge/edge_module/common/HeaderFactory.hpp>
#include <safe_edge/edge_module/common/TopicNames.hpp>
#include <safe_edge/edge_module/logic/EdgeAdvisor.hpp>

#include <pilot_server.hpp>

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

#include <commonPubSubTypes.hpp>
#include <edgePubSubTypes.hpp>
#include <pilot_serverPubSubTypes.hpp>

#include <chrono>
#include <iostream>
#include <thread>

namespace safe_edge {
namespace edge_module {
namespace nodes {

namespace {

bool register_type(
        eprosima::fastdds::dds::DomainParticipant* participant,
        eprosima::fastdds::dds::TypeSupport& type_support,
        const char* label)
{
    if (eprosima::fastdds::dds::RETCODE_OK != type_support.register_type(participant))
    {
        std::cerr << "[edge_gateway] Failed to register type: " << label << std::endl;
        return false;
    }
    return true;
}

eprosima::fastdds::dds::Topic* create_topic(
        eprosima::fastdds::dds::DomainParticipant* participant,
        const std::string& topic_name,
        eprosima::fastdds::dds::TypeSupport& type_support)
{
    eprosima::fastdds::dds::TopicQos topic_qos{};
    return participant->create_topic(
        topic_name,
        type_support.get_type_name(),
        topic_qos,
        nullptr,
        eprosima::fastdds::dds::StatusMask::none());
}

} // namespace

EdgeGatewayNode::ParticipantListener::ParticipantListener(EdgeGatewayNode& owner)
    : owner_(owner)
{
}

void EdgeGatewayNode::ParticipantListener::on_subscription_matched(
        eprosima::fastdds::dds::DataReader* reader,
        const eprosima::fastdds::dds::SubscriptionMatchedStatus& info) noexcept
{
    owner_.log_subscription_match(
        reader->get_topicdescription()->get_name().c_str(), info.total_count);
}

void EdgeGatewayNode::ParticipantListener::on_publication_matched(
        eprosima::fastdds::dds::DataWriter* writer,
        const eprosima::fastdds::dds::PublicationMatchedStatus& info) noexcept
{
    owner_.log_publication_match(
        writer->get_topic()->get_name().c_str(), info.total_count);
}

EdgeGatewayNode::VehicleEdgeSummaryListener::VehicleEdgeSummaryListener(EdgeGatewayNode& owner)
    : owner_(owner)
{
}

void EdgeGatewayNode::VehicleEdgeSummaryListener::on_data_available(
        eprosima::fastdds::dds::DataReader* reader) noexcept
{
    safe_edge::edge::VehicleEdgeSummary sample{};
    eprosima::fastdds::dds::SampleInfo info{};

    while (eprosima::fastdds::dds::RETCODE_OK == reader->take_next_sample(&sample, &info))
    {
        if (info.valid_data)
        {
            owner_.on_vehicle_edge_summary_received(sample);
        }
    }
}

EdgeGatewayNode::ChargerLocationListener::ChargerLocationListener(EdgeGatewayNode& owner)
    : owner_(owner)
{
}

void EdgeGatewayNode::ChargerLocationListener::on_data_available(
        eprosima::fastdds::dds::DataReader* reader) noexcept
{
    safe_edge::pilot_server::ChargerLocation sample{};
    eprosima::fastdds::dds::SampleInfo info{};

    while (eprosima::fastdds::dds::RETCODE_OK == reader->take_next_sample(&sample, &info))
    {
        if (info.valid_data)
        {
            owner_.on_charger_location_received(sample);
        }
    }
}

EdgeGatewayNode::HeartbeatListener::HeartbeatListener(EdgeGatewayNode& owner)
    : owner_(owner)
{
}

void EdgeGatewayNode::HeartbeatListener::on_data_available(
        eprosima::fastdds::dds::DataReader* reader) noexcept
{
    safe_edge::common::ServiceHeartbeat sample{};
    eprosima::fastdds::dds::SampleInfo info{};

    while (eprosima::fastdds::dds::RETCODE_OK == reader->take_next_sample(&sample, &info))
    {
        if (info.valid_data)
        {
            owner_.on_server_heartbeat_received(sample);
        }
    }
}

EdgeGatewayNode::EdgeGatewayNode(const common::RuntimeConfig& runtime_config)
    : runtime_config_(runtime_config)
    , header_factory_(runtime_config.source_name)
    , participant_listener_(*this)
    , vehicle_edge_summary_listener_(*this)
    , charger_location_listener_(*this)
    , heartbeat_listener_(*this)
    , vehicle_edge_summary_type_support_(new safe_edge::edge::VehicleEdgeSummaryPubSubType())
    , energy_advisory_type_support_(new safe_edge::edge::EnergyAdvisoryPubSubType())
    , edge_gateway_status_type_support_(new safe_edge::edge::EdgeGatewayStatusPubSubType())
    , charger_location_type_support_(new safe_edge::pilot_server::ChargerLocationPubSubType())
    , service_heartbeat_type_support_(new safe_edge::common::ServiceHeartbeatPubSubType())
{
}

int EdgeGatewayNode::run()
{
    if (!initialize())
    {
        return 1;
    }

    next_status_fire_ = std::chrono::steady_clock::now() +
        std::chrono::seconds(runtime_config_.status_interval_sec);

    std::cout << "[edge_gateway] [START] Running with participant port "
              << runtime_config_.participant_port << std::endl;

    while (true)
    {
        std::this_thread::sleep_until(next_status_fire_);
        publish_edge_gateway_status();
        next_status_fire_ += std::chrono::seconds(runtime_config_.status_interval_sec);
    }

    return 0;
}

bool EdgeGatewayNode::initialize()
{
    return create_participant() &&
           register_types() &&
           create_topics() &&
           create_endpoints() &&
           enable_entities();
}

bool EdgeGatewayNode::create_participant()
{
    eprosima::fastdds::dds::DomainParticipantQos participant_qos{};
    participant_qos.name(runtime_config_.participant_name);

    eprosima::fastdds::rtps::Locator_t locator;
    eprosima::fastdds::rtps::IPLocator::setIPv4(locator, "127.0.0.1");
    locator.port = runtime_config_.participant_port;
    participant_qos.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(locator);

    eprosima::fastdds::dds::StatusMask participant_mask =
        eprosima::fastdds::dds::StatusMask::publication_matched();
    participant_mask |= eprosima::fastdds::dds::StatusMask::subscription_matched();

    participant_ = eprosima::fastdds::dds::DomainParticipantFactory::get_instance()
        ->create_participant(
            runtime_config_.domain_id,
            participant_qos,
            &participant_listener_,
            participant_mask);

    if (nullptr == participant_)
    {
        std::cerr << "[edge_gateway] Failed to create participant" << std::endl;
        return false;
    }

    return true;
}

bool EdgeGatewayNode::register_types()
{
    return register_type(participant_, vehicle_edge_summary_type_support_, "VehicleEdgeSummary") &&
           register_type(participant_, energy_advisory_type_support_, "EnergyAdvisory") &&
           register_type(participant_, edge_gateway_status_type_support_, "EdgeGatewayStatus") &&
           register_type(participant_, charger_location_type_support_, "ChargerLocation") &&
           register_type(participant_, service_heartbeat_type_support_, "ServiceHeartbeat");
}

bool EdgeGatewayNode::create_topics()
{
    vehicle_edge_summary_topic_name_ = common::topic_names::vehicle_edge_summary();
    vehicle_edge_summary_topic_ = create_topic(
        participant_, vehicle_edge_summary_topic_name_, vehicle_edge_summary_type_support_);
    if (nullptr == vehicle_edge_summary_topic_)
    {
        std::cerr << "[edge_gateway] Failed to create topic: vehicle_edge_summary" << std::endl;
        return false;
    }

    energy_advisory_topic_name_ = common::topic_names::energy_advisory();
    energy_advisory_topic_ = create_topic(
        participant_, energy_advisory_topic_name_, energy_advisory_type_support_);
    if (nullptr == energy_advisory_topic_)
    {
        std::cerr << "[edge_gateway] Failed to create topic: energy_advisory" << std::endl;
        return false;
    }

    edge_gateway_status_topic_name_ = common::topic_names::edge_gateway_status();
    edge_gateway_status_topic_ = create_topic(
        participant_, edge_gateway_status_topic_name_, edge_gateway_status_type_support_);
    if (nullptr == edge_gateway_status_topic_)
    {
        std::cerr << "[edge_gateway] Failed to create topic: edge_gateway_status" << std::endl;
        return false;
    }

    charger_location_topic_name_ = common::topic_names::charger_locations();
    charger_location_topic_ = create_topic(
        participant_, charger_location_topic_name_, charger_location_type_support_);
    if (nullptr == charger_location_topic_)
    {
        std::cerr << "[edge_gateway] Failed to create topic: charger_locations" << std::endl;
        return false;
    }

    service_heartbeat_topic_name_ = common::topic_names::service_heartbeat();
    service_heartbeat_topic_ = create_topic(
        participant_, service_heartbeat_topic_name_, service_heartbeat_type_support_);
    if (nullptr == service_heartbeat_topic_)
    {
        std::cerr << "[edge_gateway] Failed to create topic: service_heartbeat" << std::endl;
        return false;
    }

    return true;
}

bool EdgeGatewayNode::create_endpoints()
{
    eprosima::fastdds::dds::PublisherQos publisher_qos{};
    publisher_ = participant_->create_publisher(
        publisher_qos,
        nullptr,
        eprosima::fastdds::dds::StatusMask::none());

    eprosima::fastdds::dds::SubscriberQos subscriber_qos{};
    subscriber_ = participant_->create_subscriber(
        subscriber_qos,
        nullptr,
        eprosima::fastdds::dds::StatusMask::none());

    if (nullptr == publisher_ || nullptr == subscriber_)
    {
        std::cerr << "[edge_gateway] Failed to create publisher or subscriber" << std::endl;
        return false;
    }

    eprosima::fastdds::dds::DataWriterQos writer_qos{};
    writer_qos.reliability().kind = eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;

    energy_advisory_datawriter_ = publisher_->create_datawriter(
        energy_advisory_topic_,
        writer_qos,
        nullptr,
        eprosima::fastdds::dds::StatusMask::none());
    edge_gateway_status_datawriter_ = publisher_->create_datawriter(
        edge_gateway_status_topic_,
        writer_qos,
        nullptr,
        eprosima::fastdds::dds::StatusMask::none());

    eprosima::fastdds::dds::DataReaderQos reader_qos{};
    reader_qos.reliability().kind = eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;

    vehicle_edge_summary_datareader_ = subscriber_->create_datareader(
        vehicle_edge_summary_topic_,
        reader_qos,
        &vehicle_edge_summary_listener_,
        eprosima::fastdds::dds::StatusMask::data_available());
    charger_location_datareader_ = subscriber_->create_datareader(
        charger_location_topic_,
        reader_qos,
        &charger_location_listener_,
        eprosima::fastdds::dds::StatusMask::data_available());
    service_heartbeat_datareader_ = subscriber_->create_datareader(
        service_heartbeat_topic_,
        reader_qos,
        &heartbeat_listener_,
        eprosima::fastdds::dds::StatusMask::data_available());

    const bool success =
        nullptr != energy_advisory_datawriter_ &&
        nullptr != edge_gateway_status_datawriter_ &&
        nullptr != vehicle_edge_summary_datareader_ &&
        nullptr != charger_location_datareader_ &&
        nullptr != service_heartbeat_datareader_;

    if (!success)
    {
        std::cerr << "[edge_gateway] Failed to create endpoints" << std::endl;
        return false;
    }

    return true;
}

bool EdgeGatewayNode::enable_entities()
{
    bool enabled = true;
    enabled = enabled && (eprosima::fastdds::dds::RETCODE_OK == publisher_->enable());
    enabled = enabled && (eprosima::fastdds::dds::RETCODE_OK == energy_advisory_datawriter_->enable());
    enabled = enabled && (eprosima::fastdds::dds::RETCODE_OK == edge_gateway_status_datawriter_->enable());
    enabled = enabled && (eprosima::fastdds::dds::RETCODE_OK == subscriber_->enable());
    enabled = enabled && (eprosima::fastdds::dds::RETCODE_OK == vehicle_edge_summary_datareader_->enable());
    enabled = enabled && (eprosima::fastdds::dds::RETCODE_OK == charger_location_datareader_->enable());
    enabled = enabled && (eprosima::fastdds::dds::RETCODE_OK == service_heartbeat_datareader_->enable());
    enabled = enabled && (eprosima::fastdds::dds::RETCODE_OK == participant_->enable());

    if (!enabled)
    {
        std::cerr << "[edge_gateway] Failed to enable DDS entities" << std::endl;
    }

    return enabled;
}

void EdgeGatewayNode::on_vehicle_edge_summary_received(
        const safe_edge::edge::VehicleEdgeSummary& summary)
{
    std::cout << "[edge_gateway] Received VehicleEdgeSummary soc=" << summary.soc_pct()
              << " v2g_ready=" << summary.v2g_ready()
              << " mode=" << static_cast<int32_t>(summary.current_mode()) << std::endl;

    safe_edge::edge::EnergyAdvisory advisory = logic::EdgeAdvisor::evaluate(
        summary, cached_chargers_, cached_charger_count_);
    advisory.header(header_factory_.make_header("energy_advisory"));
    publish_energy_advisory(advisory);
}

void EdgeGatewayNode::on_charger_location_received(
        const safe_edge::pilot_server::ChargerLocation& location)
{
    if (cached_charger_count_ < 3)
    {
        cached_chargers_[cached_charger_count_++] = location;
    }
    last_server_sync_ms_ = common::HeaderFactory::now_ms();

    std::cout << "[edge_gateway] Received ChargerLocation id=" << location.id()
              << " name=" << location.name() << std::endl;
}

void EdgeGatewayNode::publish_energy_advisory(const safe_edge::edge::EnergyAdvisory& advisory)
{
    if (eprosima::fastdds::dds::RETCODE_OK !=
            energy_advisory_datawriter_->write(
                const_cast<safe_edge::edge::EnergyAdvisory*>(&advisory),
                eprosima::fastdds::dds::HANDLE_NIL))
    {
        std::cerr << "[edge_gateway] Failed to publish EnergyAdvisory" << std::endl;
        return;
    }

    std::cout << "[edge_gateway] Published EnergyAdvisory mode="
              << static_cast<int32_t>(advisory.suggested_mode())
              << " reason=" << advisory.advisory_reason() << std::endl;
}

void EdgeGatewayNode::on_server_heartbeat_received(
        const safe_edge::common::ServiceHeartbeat& heartbeat)
{
    if (heartbeat.service_name() != "server")
    {
        return;
    }
    last_server_hb_ms_ = common::HeaderFactory::now_ms();
    server_available_ = true;
}

void EdgeGatewayNode::publish_edge_gateway_status()
{
    constexpr uint64_t SERVER_HB_TIMEOUT_MS = 10000U;
    if (last_server_hb_ms_ > 0U &&
        (common::HeaderFactory::now_ms() - last_server_hb_ms_) > SERVER_HB_TIMEOUT_MS)
    {
        server_available_ = false;
    }

    safe_edge::edge::EdgeGatewayStatus status;
    status.header(header_factory_.make_header("edge_gateway_status"));
    status.status(server_available_
        ? safe_edge::common::HealthStatus::HEALTH_OK
        : safe_edge::common::HealthStatus::HEALTH_DEGRADED);
    status.last_server_sync_ms(last_server_sync_ms_);
    status.detail(server_available_
        ? "edge connected, server synced"
        : "edge connected, server_down");

    if (eprosima::fastdds::dds::RETCODE_OK !=
            edge_gateway_status_datawriter_->write(&status, eprosima::fastdds::dds::HANDLE_NIL))
    {
        std::cerr << "[edge_gateway] Failed to publish EdgeGatewayStatus" << std::endl;
        return;
    }

    std::cout << "[edge_gateway] Published EdgeGatewayStatus status="
              << (server_available_ ? "OK" : "DEGRADED") << std::endl;
}

void EdgeGatewayNode::log_subscription_match(const char* topic_name, int32_t total_count) const
{
    std::cout << "[edge_gateway] Subscription matched on " << topic_name
              << " total_count=" << total_count << std::endl;
}

void EdgeGatewayNode::log_publication_match(const char* topic_name, int32_t total_count) const
{
    std::cout << "[edge_gateway] Publication matched on " << topic_name
              << " total_count=" << total_count << std::endl;
}

} // namespace nodes
} // namespace edge_module
} // namespace safe_edge
