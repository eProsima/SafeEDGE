#include <safe_edge/edge_module/nodes/EdgeGatewayNode.hpp>

#include <safe_edge/edge_module/common/HeaderFactory.hpp>
#include <safe_edge/edge_module/common/TopicNames.hpp>
#include <safe_edge/edge_module/logic/EdgeAdvisor.hpp>

#include <pilot_server.hpp>

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
namespace edge_module {
namespace nodes {

namespace {

constexpr eprosima::safedds::execution::TimePeriod STATUS_INTERVAL = {5, 0};

template<typename TypeSupportT>
bool register_type(
        eprosima::safedds::dds::DomainParticipant& participant,
        TypeSupportT& type_support,
        const char* label)
{
    if (eprosima::safedds::dds::ReturnCode::OK != type_support.register_type(participant, type_support.get_type_name()))
    {
        std::cerr << "[edge_gateway] Failed to register type: " << label << std::endl;
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
    eprosima::safedds::dds::TopicQos topic_qos{};
    return participant.create_topic(
        topic_name,
        type_support.get_type_name(),
        topic_qos,
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

EdgeGatewayNode::ParticipantListener::ParticipantListener(EdgeGatewayNode& owner)
    : owner_(owner)
{
}

void EdgeGatewayNode::ParticipantListener::on_subscription_matched(
        eprosima::safedds::dds::DataReader& reader,
        const eprosima::safedds::dds::SubscriptionMatchedStatus& info) noexcept
{
    owner_.log_subscription_match(reader.get_topicdescription().get_name().const_string_data(), info.total_count);
}

void EdgeGatewayNode::ParticipantListener::on_publication_matched(
        eprosima::safedds::dds::DataWriter& writer,
        const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept
{
    owner_.log_publication_match(writer.get_topic().get_name().const_string_data(), info.total_count);
}

EdgeGatewayNode::VehicleEdgeSummaryListener::VehicleEdgeSummaryListener(EdgeGatewayNode& owner)
    : owner_(owner)
{
}

void EdgeGatewayNode::VehicleEdgeSummaryListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader =
        eprosima::safedds::dds::TypedDataReader<safe_edge::edge::VehicleEdgeSummaryTypeSupport>::downcast(reader);

    if (nullptr == typed_reader)
    {
        std::cerr << "[edge_gateway] Failed to downcast vehicle edge summary reader" << std::endl;
        return;
    }

    safe_edge::edge::VehicleEdgeSummary sample{};
    eprosima::safedds::dds::SampleInfo info{};

    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
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
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader =
        eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::ChargerLocationTypeSupport>::downcast(reader);

    if (nullptr == typed_reader)
    {
        std::cerr << "[edge_gateway] Failed to downcast charger location reader" << std::endl;
        return;
    }

    safe_edge::pilot_server::ChargerLocation sample{};
    eprosima::safedds::dds::SampleInfo info{};

    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
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
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader =
        eprosima::safedds::dds::TypedDataReader<safe_edge::common::ServiceHeartbeatTypeSupport>::downcast(reader);

    if (nullptr == typed_reader)
    {
        std::cerr << "[edge_gateway] Failed to downcast heartbeat reader" << std::endl;
        return;
    }

    safe_edge::common::ServiceHeartbeat sample{};
    eprosima::safedds::dds::SampleInfo info{};

    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
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
    , status_timer_(STATUS_INTERVAL)
{
}

int EdgeGatewayNode::run()
{
    if (!initialize())
    {
        return 1;
    }

    start_timers();
    std::cout << "[edge_gateway] [START] Running with participant port " << runtime_config_.participant_port << std::endl;

    while (true)
    {
        while (executor_->has_pending_work())
        {
            executor_->spin(eprosima::safedds::execution::TIME_ZERO);
        }

        if (status_timer_.is_triggered_and_reset())
        {
            publish_edge_gateway_status();
        }

        executor_->spin(next_wakeup_time());
    }

    return 0;
}

bool EdgeGatewayNode::initialize()
{
    return create_participant() &&
           register_types() &&
           create_topics() &&
           create_endpoints() &&
           enable_entities() &&
           create_executor();
}

bool EdgeGatewayNode::create_participant()
{
    eprosima::safedds::dds::DomainParticipantQos participant_qos{};
    eprosima::safedds::memory::container::StaticString256 participant_name(runtime_config_.participant_name.c_str());
    participant_qos.participant_name() = participant_name;
    participant_qos.wire_protocol_qos().announced_locator = eprosima::safedds::transport::Locator::from_ipv4(
        {127, 0, 0, 1},
        runtime_config_.participant_port);

    participant_ = factory_.create_participant(
        runtime_config_.domain_id,
        participant_qos,
        &participant_listener_,
        eprosima::safedds::dds::PUBLICATION_MATCHED_STATUS |
        eprosima::safedds::dds::SUBSCRIPTION_MATCHED_STATUS);

    if (nullptr == participant_)
    {
        std::cerr << "[edge_gateway] Failed to create participant" << std::endl;
        return false;
    }

    return true;
}

bool EdgeGatewayNode::register_types()
{
    return register_type(*participant_, vehicle_edge_summary_type_support_, "VehicleEdgeSummary") &&
           register_type(*participant_, energy_advisory_type_support_, "EnergyAdvisory") &&
           register_type(*participant_, edge_gateway_status_type_support_, "EdgeGatewayStatus") &&
           register_type(*participant_, charger_location_type_support_, "ChargerLocation") &&
           register_type(*participant_, service_heartbeat_type_support_, "ServiceHeartbeat");
}

bool EdgeGatewayNode::create_topics()
{
    vehicle_edge_summary_topic_name_ = eprosima::safedds::memory::container::StaticString256(
        common::topic_names::vehicle_edge_summary());
    vehicle_edge_summary_topic_ = create_topic(
        *participant_, vehicle_edge_summary_topic_name_, vehicle_edge_summary_type_support_);
    if (nullptr == vehicle_edge_summary_topic_)
    {
        std::cerr << "[edge_gateway] Failed to create topic: vehicle_edge_summary" << std::endl;
        return false;
    }

    energy_advisory_topic_name_ = eprosima::safedds::memory::container::StaticString256(
        common::topic_names::energy_advisory());
    energy_advisory_topic_ = create_topic(
        *participant_, energy_advisory_topic_name_, energy_advisory_type_support_);
    if (nullptr == energy_advisory_topic_)
    {
        std::cerr << "[edge_gateway] Failed to create topic: energy_advisory" << std::endl;
        return false;
    }

    edge_gateway_status_topic_name_ = eprosima::safedds::memory::container::StaticString256(
        common::topic_names::edge_gateway_status());
    edge_gateway_status_topic_ = create_topic(
        *participant_, edge_gateway_status_topic_name_, edge_gateway_status_type_support_);
    if (nullptr == edge_gateway_status_topic_)
    {
        std::cerr << "[edge_gateway] Failed to create topic: edge_gateway_status" << std::endl;
        return false;
    }

    charger_location_topic_name_ = eprosima::safedds::memory::container::StaticString256(
        common::topic_names::charger_locations());
    charger_location_topic_ = create_topic(
        *participant_, charger_location_topic_name_, charger_location_type_support_);
    if (nullptr == charger_location_topic_)
    {
        std::cerr << "[edge_gateway] Failed to create topic: charger_locations" << std::endl;
        return false;
    }

    service_heartbeat_topic_name_ = eprosima::safedds::memory::container::StaticString256(
        common::topic_names::service_heartbeat());
    service_heartbeat_topic_ = create_topic(
        *participant_, service_heartbeat_topic_name_, service_heartbeat_type_support_);
    if (nullptr == service_heartbeat_topic_)
    {
        std::cerr << "[edge_gateway] Failed to create topic: service_heartbeat" << std::endl;
        return false;
    }

    return true;
}

bool EdgeGatewayNode::create_endpoints()
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
        std::cerr << "[edge_gateway] Failed to create publisher or subscriber" << std::endl;
        return false;
    }

    eprosima::safedds::dds::DataWriterQos writer_qos{};
    writer_qos.reliability().kind = eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    energy_advisory_datawriter_ = publisher_->create_datawriter(
        *energy_advisory_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    edge_gateway_status_datawriter_ = publisher_->create_datawriter(
        *edge_gateway_status_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);

    energy_advisory_writer_ = downcast_writer<safe_edge::edge::EnergyAdvisoryTypeSupport>(
        energy_advisory_datawriter_);
    edge_gateway_status_writer_ = downcast_writer<safe_edge::edge::EdgeGatewayStatusTypeSupport>(
        edge_gateway_status_datawriter_);

    eprosima::safedds::dds::DataReaderQos reader_qos{};
    reader_qos.reliability().kind = eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    vehicle_edge_summary_datareader_ = subscriber_->create_datareader(
        *vehicle_edge_summary_topic_,
        reader_qos,
        &vehicle_edge_summary_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    vehicle_edge_summary_reader_ = downcast_reader<safe_edge::edge::VehicleEdgeSummaryTypeSupport>(
        vehicle_edge_summary_datareader_);

    charger_location_datareader_ = subscriber_->create_datareader(
        *charger_location_topic_,
        reader_qos,
        &charger_location_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    charger_location_reader_ = downcast_reader<safe_edge::pilot_server::ChargerLocationTypeSupport>(
        charger_location_datareader_);

    service_heartbeat_datareader_ = subscriber_->create_datareader(
        *service_heartbeat_topic_,
        reader_qos,
        &heartbeat_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    heartbeat_reader_ = downcast_reader<safe_edge::common::ServiceHeartbeatTypeSupport>(
        service_heartbeat_datareader_);

    const bool success = nullptr != energy_advisory_writer_ &&
            nullptr != edge_gateway_status_writer_ &&
            nullptr != vehicle_edge_summary_reader_ &&
            nullptr != charger_location_reader_ &&
            nullptr != heartbeat_reader_;

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
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == publisher_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == energy_advisory_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == edge_gateway_status_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == subscriber_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == vehicle_edge_summary_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == charger_location_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == service_heartbeat_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == participant_->enable());

    if (!enabled)
    {
        std::cerr << "[edge_gateway] Failed to enable DDS entities" << std::endl;
    }

    return enabled;
}

bool EdgeGatewayNode::create_executor()
{
    executor_ = factory_.create_default_executor();

    if (nullptr == executor_)
    {
        std::cerr << "[edge_gateway] Failed to create executor" << std::endl;
        return false;
    }

    return true;
}

void EdgeGatewayNode::start_timers() noexcept
{
    status_timer_.start();
}

void EdgeGatewayNode::on_vehicle_edge_summary_received(
        const safe_edge::edge::VehicleEdgeSummary& summary)
{
    std::cout << "[edge_gateway] Received VehicleEdgeSummary soc=" << summary.soc_pct
              << " v2g_ready=" << summary.v2g_ready
              << " mode=" << static_cast<int32_t>(summary.current_mode) << std::endl;

    safe_edge::edge::EnergyAdvisory advisory = logic::EdgeAdvisor::evaluate(
        summary, cached_chargers_, cached_charger_count_);
    advisory.header = header_factory_.make_header("energy_advisory");
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

    std::cout << "[edge_gateway] Received ChargerLocation id=" << location.id
              << " name=" << location.name << std::endl;
}

void EdgeGatewayNode::publish_energy_advisory(const safe_edge::edge::EnergyAdvisory& advisory)
{
    if (eprosima::safedds::dds::ReturnCode::OK !=
            energy_advisory_writer_->write(advisory, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[edge_gateway] Failed to publish EnergyAdvisory" << std::endl;
        return;
    }

    std::cout << "[edge_gateway] Published EnergyAdvisory mode=" << static_cast<int32_t>(advisory.suggested_mode)
              << " reason=" << advisory.advisory_reason << std::endl;
}

void EdgeGatewayNode::on_server_heartbeat_received(
        const safe_edge::common::ServiceHeartbeat& heartbeat)
{
    if (heartbeat.service_name != "server")
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
    status.header = header_factory_.make_header("edge_gateway_status");
    status.status = server_available_
        ? safe_edge::common::HealthStatus::HEALTH_OK
        : safe_edge::common::HealthStatus::HEALTH_DEGRADED;
    status.last_server_sync_ms = last_server_sync_ms_;
    status.detail = server_available_
        ? "edge connected, server synced"
        : "edge connected, server_down";

    if (eprosima::safedds::dds::ReturnCode::OK !=
            edge_gateway_status_writer_->write(status, eprosima::safedds::dds::HANDLE_NIL))
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

eprosima::safedds::execution::TimePoint EdgeGatewayNode::next_wakeup_time() const noexcept
{
    eprosima::safedds::execution::TimePoint next = executor_->get_next_work_timepoint();
    next = eprosima::safedds::execution::TimePoint::min(next, status_timer_.next_trigger());
    return next;
}

} // namespace nodes
} // namespace edge_module
} // namespace safe_edge
