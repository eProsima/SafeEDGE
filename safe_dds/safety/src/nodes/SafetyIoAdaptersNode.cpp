#include <safe_edge/safety_domain/nodes/SafetyIoAdaptersNode.hpp>

#include <safe_edge/safety_domain/common/HeaderFactory.hpp>
#include <safe_edge/safety_domain/common/TopicNames.hpp>
#include <safe_edge/safety_domain/policy/IdlAdapters.hpp>

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
namespace safety_domain {
namespace nodes {

namespace {

constexpr eprosima::safedds::execution::TimePeriod TIMEOUT = {5, 0}; 

// Gateway health thresholds (ms since last advisory)
constexpr uint64_t GATEWAY_OK_THRESHOLD_MS      = 5000U;
constexpr uint64_t GATEWAY_DEGRADED_THRESHOLD_MS = 15000U;

template<typename TypeSupportT>
bool register_type(
        eprosima::safedds::dds::DomainParticipant& participant,
        TypeSupportT& type_support,
        const char* label)
{
    if (eprosima::safedds::dds::ReturnCode::OK != type_support.register_type(participant, type_support.get_type_name()))
    {
        std::cerr << "[safety_io_adapters] Failed to register type: " << label << std::endl;
        return false;
    }

    return true;
}

template<typename TypeSupportT>
eprosima::safedds::dds::Topic* create_topic(
        eprosima::safedds::dds::DomainParticipant& participant,
        eprosima::safedds::memory::container::StaticString256& safe_topic_name,
        TypeSupportT& type_support)
{
    eprosima::safedds::dds::TopicQos topic_qos{};
    return participant.create_topic(
        safe_topic_name,
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

SafetyIoAdaptersNode::ParticipantListener::ParticipantListener(
        SafetyIoAdaptersNode& owner)
    : owner_(owner)
{
}

void SafetyIoAdaptersNode::ParticipantListener::on_subscription_matched(
        eprosima::safedds::dds::DataReader& reader,
        const eprosima::safedds::dds::SubscriptionMatchedStatus& info) noexcept
{
    owner_.log_subscription_match(reader.get_topicdescription().get_name().const_string_data(), info.total_count);
}

void SafetyIoAdaptersNode::ParticipantListener::on_publication_matched(
        eprosima::safedds::dds::DataWriter& writer,
        const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept
{
    owner_.log_publication_match(writer.get_topic().get_name().const_string_data(), info.total_count);
}

SafetyIoAdaptersNode::PolicyDecisionListener::PolicyDecisionListener(
        SafetyIoAdaptersNode& owner)
    : owner_(owner)
{
}

void SafetyIoAdaptersNode::PolicyDecisionListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::internal::PolicyDecisionTypeSupport>::downcast(
        reader);

    if (nullptr == typed_reader)
    {
        std::cerr << "[safety_io_adapters] Failed to downcast policy decision reader" << std::endl;
        return;
    }

    safe_edge::internal::PolicyDecision sample{};
    eprosima::safedds::dds::SampleInfo info{};

    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            owner_.on_policy_decision_received(sample);
        }
    }
}

SafetyIoAdaptersNode::EnergyAdvisoryListener::EnergyAdvisoryListener(
        SafetyIoAdaptersNode& owner)
    : owner_(owner)
{
}

void SafetyIoAdaptersNode::EnergyAdvisoryListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::edge::EnergyAdvisoryTypeSupport>::downcast(
        reader);

    if (nullptr == typed_reader)
    {
        std::cerr << "[safety_io_adapters] Failed to downcast energy advisory reader" << std::endl;
        return;
    }

    safe_edge::edge::EnergyAdvisory sample{};
    eprosima::safedds::dds::SampleInfo info{};

    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            owner_.on_energy_advisory_received(sample);
        }
    }
}

SafetyIoAdaptersNode::HeartbeatListener::HeartbeatListener(
        SafetyIoAdaptersNode& owner)
    : owner_(owner)
{
}

void SafetyIoAdaptersNode::HeartbeatListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::common::ServiceHeartbeatTypeSupport>::downcast(
        reader);

    if (nullptr == typed_reader)
    {
        std::cerr << "[safety_io_adapters] Failed to downcast heartbeat reader" << std::endl;
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

SafetyIoAdaptersNode::SafetyInputFrameListener::SafetyInputFrameListener(
        SafetyIoAdaptersNode& owner)
    : owner_(owner)
{
}

void SafetyIoAdaptersNode::SafetyInputFrameListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::internal::SafetyInputFrameTypeSupport>::downcast(
        reader);

    if (nullptr == typed_reader)
    {
        std::cerr << "[safety_io_adapters] Failed to downcast safety_input_frame reader" << std::endl;
        return;
    }

    safe_edge::internal::SafetyInputFrame sample{};
    eprosima::safedds::dds::SampleInfo info{};

    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            owner_.on_safety_input_frame_received(sample);
        }
    }
}

SafetyIoAdaptersNode::SafetyIoAdaptersNode(
        const common::RuntimeConfig& runtime_config)
    : runtime_config_(runtime_config)
    , header_factory_(runtime_config.source_name)
    , participant_listener_(*this)
    , policy_decision_listener_(*this)
    , energy_advisory_listener_(*this)
    , heartbeat_listener_(*this)
    , safety_input_frame_listener_(*this)
    , edge_gateway_status_timer_(TIMEOUT)
    , heartbeat_timer_(TIMEOUT)
{
}

int SafetyIoAdaptersNode::run()
{
    if (!initialize())
    {
        return 1;
    }

    start_timers();
    std::cout << "[safety_io_adapters] [START] Running with participant port " << runtime_config_.participant_port << std::endl;

    while (true)
    {
        while (executor_->has_pending_work())
        {
            executor_->spin(eprosima::safedds::execution::TIME_ZERO);
        }

        if (edge_gateway_status_timer_.is_triggered_and_reset())
        {
            publish_edge_gateway_status();
        }

        if (heartbeat_timer_.is_triggered_and_reset())
        {
            publish_heartbeat();
        }

        if (pending_vehicle_edge_summary_publish_)
        {
            publish_vehicle_edge_summary();
        }

        executor_->spin(next_wakeup_time());
    }

    return 0;
}

bool SafetyIoAdaptersNode::initialize()
{
    return create_participant() &&
           register_types() &&
           create_topics() &&
           create_endpoints() &&
           enable_entities() &&
           create_executor();
}

bool SafetyIoAdaptersNode::create_participant()
{
    eprosima::safedds::dds::DomainParticipantQos participant_qos{};
    eprosima::safedds::memory::container::StaticString256 participant_name(runtime_config_.participant_name.c_str());
    participant_qos.participant_name() = participant_name;
    participant_qos.wire_protocol_qos().announced_locator = eprosima::safedds::transport::Locator::from_ipv4(
        {127, 0, 0, 1},
        runtime_config_.participant_port);
    participant_qos.wire_protocol_qos().use_multicast_discovery = false;

    initial_peers_.add(eprosima::safedds::transport::Locator::from_ipv4({127, 0, 0, 1}, runtime_config_.initial_peer_port));
    participant_qos.wire_protocol_qos().initial_peers = &initial_peers_;

    participant_ = factory_.create_participant(
        runtime_config_.domain_id,
        participant_qos,
        &participant_listener_,
        eprosima::safedds::dds::PUBLICATION_MATCHED_STATUS |
        eprosima::safedds::dds::SUBSCRIPTION_MATCHED_STATUS);

    if (nullptr == participant_)
    {
        std::cerr << "[safety_io_adapters] Failed to create participant" << std::endl;
        return false;
    }

    return true;
}

bool SafetyIoAdaptersNode::register_types()
{
    return register_type(*participant_, safety_input_frame_type_support_, "SafetyInputFrame") &&
           register_type(*participant_, energy_advisory_type_support_, "EnergyAdvisory") &&
           register_type(*participant_, edge_gateway_status_type_support_, "EdgeGatewayStatus") &&
           register_type(*participant_, vehicle_edge_summary_type_support_, "VehicleEdgeSummary") &&
           register_type(*participant_, policy_decision_type_support_, "PolicyDecision") &&
           register_type(*participant_, service_heartbeat_type_support_, "ServiceHeartbeat");
}

bool SafetyIoAdaptersNode::create_topics()
{
    safety_input_frame_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::safety_input_frame());
    safety_input_frame_topic_ = create_topic(*participant_, safety_input_frame_topic_name_, safety_input_frame_type_support_);
    if (nullptr == safety_input_frame_topic_) { std::cerr << "[safety_io_adapters] Failed to create topic: safety_input_frame" << std::endl; return false; }

    energy_advisory_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::energy_advisory());
    energy_advisory_topic_ = create_topic(*participant_, energy_advisory_topic_name_, energy_advisory_type_support_);
    if (nullptr == energy_advisory_topic_) { std::cerr << "[safety_io_adapters] Failed to create topic: energy_advisory" << std::endl; return false; }

    edge_gateway_status_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::edge_gateway_status());
    edge_gateway_status_topic_ = create_topic(*participant_, edge_gateway_status_topic_name_, edge_gateway_status_type_support_);
    if (nullptr == edge_gateway_status_topic_) { std::cerr << "[safety_io_adapters] Failed to create topic: edge_gateway_status" << std::endl; return false; }

    vehicle_edge_summary_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::vehicle_edge_summary());
    vehicle_edge_summary_topic_ = create_topic(*participant_, vehicle_edge_summary_topic_name_, vehicle_edge_summary_type_support_);
    if (nullptr == vehicle_edge_summary_topic_) { std::cerr << "[safety_io_adapters] Failed to create topic: vehicle_edge_summary" << std::endl; return false; }

    policy_decision_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::policy_decision());
    policy_decision_topic_ = create_topic(*participant_, policy_decision_topic_name_, policy_decision_type_support_);
    if (nullptr == policy_decision_topic_) { std::cerr << "[safety_io_adapters] Failed to create topic: policy_decision" << std::endl; return false; }

    service_heartbeat_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::service_heartbeat());
    service_heartbeat_topic_ = create_topic(*participant_, service_heartbeat_topic_name_, service_heartbeat_type_support_);
    if (nullptr == service_heartbeat_topic_) { std::cerr << "[safety_io_adapters] Failed to create topic: service_heartbeat" << std::endl; return false; }

    return true;
}

bool SafetyIoAdaptersNode::create_endpoints()
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
        std::cerr << "[safety_io_adapters] Failed to create publisher or subscriber" << std::endl;
        return false;
    }

    eprosima::safedds::dds::DataWriterQos writer_qos{};
    writer_qos.reliability().kind = eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    edge_gateway_status_datawriter_ = publisher_->create_datawriter(
        *edge_gateway_status_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    vehicle_edge_summary_datawriter_ = publisher_->create_datawriter(
        *vehicle_edge_summary_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    service_heartbeat_datawriter_ = publisher_->create_datawriter(
        *service_heartbeat_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);

    edge_gateway_status_writer_ = downcast_writer<safe_edge::edge::EdgeGatewayStatusTypeSupport>(edge_gateway_status_datawriter_);
    vehicle_edge_summary_writer_ = downcast_writer<safe_edge::edge::VehicleEdgeSummaryTypeSupport>(vehicle_edge_summary_datawriter_);
    service_heartbeat_writer_ = downcast_writer<safe_edge::common::ServiceHeartbeatTypeSupport>(service_heartbeat_datawriter_);

    eprosima::safedds::dds::DataReaderQos reader_qos{};
    reader_qos.reliability().kind = eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    safety_input_frame_datareader_ = subscriber_->create_datareader(
        *safety_input_frame_topic_,
        reader_qos,
        &safety_input_frame_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    policy_decision_datareader_ = subscriber_->create_datareader(
        *policy_decision_topic_,
        reader_qos,
        &policy_decision_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    energy_advisory_datareader_ = subscriber_->create_datareader(
        *energy_advisory_topic_,
        reader_qos,
        &energy_advisory_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    heartbeat_datareader_ = subscriber_->create_datareader(
        *service_heartbeat_topic_,
        reader_qos,
        &heartbeat_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);

    safety_input_frame_reader_ = downcast_reader<safe_edge::internal::SafetyInputFrameTypeSupport>(safety_input_frame_datareader_);
    policy_decision_reader_ = downcast_reader<safe_edge::internal::PolicyDecisionTypeSupport>(policy_decision_datareader_);
    energy_advisory_reader_ = downcast_reader<safe_edge::edge::EnergyAdvisoryTypeSupport>(energy_advisory_datareader_);
    heartbeat_reader_ = downcast_reader<safe_edge::common::ServiceHeartbeatTypeSupport>(heartbeat_datareader_);

    const bool success = nullptr != edge_gateway_status_writer_ &&
            nullptr != vehicle_edge_summary_writer_ &&
            nullptr != service_heartbeat_writer_ &&
            nullptr != safety_input_frame_reader_ &&
            nullptr != policy_decision_reader_ &&
            nullptr != energy_advisory_reader_ &&
            nullptr != heartbeat_reader_;

    if (!success)
    {
        std::cerr << "[safety_io_adapters] Failed to create endpoints" << std::endl;
        return false;
    }

    return true;
}

bool SafetyIoAdaptersNode::enable_entities()
{
    bool enabled = true;
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == publisher_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == edge_gateway_status_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == vehicle_edge_summary_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == service_heartbeat_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == subscriber_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == safety_input_frame_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == policy_decision_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == energy_advisory_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == heartbeat_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == participant_->enable());

    if (!enabled)
    {
        std::cerr << "[safety_io_adapters] Failed to enable DDS entities" << std::endl;
    }

    return enabled;
}

bool SafetyIoAdaptersNode::create_executor()
{
     executor_ = factory_.create_default_executor();

    if (nullptr == executor_)
    {
        std::cerr << "[safety_io_adapters] Failed to create executor" << std::endl;
        return false;
    }

    return true;
}

void SafetyIoAdaptersNode::start_timers() noexcept
{
    edge_gateway_status_timer_.start();
    heartbeat_timer_.start();
}

void SafetyIoAdaptersNode::on_safety_input_frame_received(
        const safe_edge::internal::SafetyInputFrame& frame)
{
    latest_safety_input_frame_ = frame;
    have_safety_input_frame_ = true;
    pending_vehicle_edge_summary_publish_ = true;
}

void SafetyIoAdaptersNode::publish_edge_gateway_status()
{
    const uint64_t now_ms = common::HeaderFactory::now_ms();
    const safe_edge::common::Header header = header_factory_.make_header("edge_gateway_status");

    safe_edge::edge::EdgeGatewayStatus status;
    status.header = header;
    status.last_server_sync_ms = last_advisory_received_ms_;

    if (last_advisory_received_ms_ == 0U)
    {
        status.status = safe_edge::common::HealthStatus::HEALTH_ERROR;
        status.detail = "cloud_advisory_age_ms=never";
    }
    else
    {
        const uint64_t age_ms = now_ms - last_advisory_received_ms_;
        if (age_ms < GATEWAY_OK_THRESHOLD_MS)
        {
            status.status = safe_edge::common::HealthStatus::HEALTH_OK;
        }
        else if (age_ms < GATEWAY_DEGRADED_THRESHOLD_MS)
        {
            status.status = safe_edge::common::HealthStatus::HEALTH_DEGRADED;
        }
        else
        {
            status.status = safe_edge::common::HealthStatus::HEALTH_ERROR;
        }

        char detail_buf[64];
        static_cast<void>(snprintf(detail_buf, sizeof(detail_buf), "cloud_advisory_age_ms=%llu",
                static_cast<unsigned long long>(age_ms)));
        status.detail = detail_buf;
    }

    if (safe_edge::common::HealthStatus::HEALTH_ERROR == status.status)
    {
        return;
    }

    if (eprosima::safedds::dds::ReturnCode::OK != edge_gateway_status_writer_->write(status, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[safety_io_adapters] Failed to publish EdgeGatewayStatus" << std::endl;
        return;
    }

    std::cout << "[safety_io_adapters] Published EdgeGatewayStatus status="
              << static_cast<int32_t>(status.status)
              << " " << status.detail << std::endl;
}

void SafetyIoAdaptersNode::publish_heartbeat()
{
    safe_edge::common::ServiceHeartbeat heartbeat;
    heartbeat.header_st = header_factory_.make_header("service_heartbeat");
    heartbeat.service_name = runtime_config_.service_name;
    heartbeat.status = safe_edge::common::HealthStatus::HEALTH_OK;
    heartbeat.detail = "running";

    if (eprosima::safedds::dds::ReturnCode::OK != service_heartbeat_writer_->write(heartbeat, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[safety_io_adapters] Failed to publish ServiceHeartbeat" << std::endl;
        return;
    }

    std::cout << "[safety_io_adapters] Published ServiceHeartbeat" << std::endl;
}

void SafetyIoAdaptersNode::publish_vehicle_edge_summary()
{
    pending_vehicle_edge_summary_publish_ = false;

    if (!have_safety_input_frame_ || !have_policy_decision_)
    {
        return;
    }

    const safe_edge::common::Header header = header_factory_.make_header("vehicle_edge_summary");
    safe_edge::edge::VehicleEdgeSummary summary = policy::to_vehicle_edge_summary(
        latest_safety_input_frame_,
        latest_policy_decision_,
        header);

    if (has_published_vehicle_edge_summary_ &&
        summary.current_mode == last_published_vehicle_edge_summary_.current_mode &&
        summary.soc_pct == last_published_vehicle_edge_summary_.soc_pct &&
        summary.vehicle_health == last_published_vehicle_edge_summary_.vehicle_health &&
        summary.v2g_ready == last_published_vehicle_edge_summary_.v2g_ready)
    {
        return;
    }

    if (eprosima::safedds::dds::ReturnCode::OK != vehicle_edge_summary_writer_->write(summary, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[safety_io_adapters] Failed to publish VehicleEdgeSummary" << std::endl;
        return;
    }

    last_published_vehicle_edge_summary_ = summary;
    has_published_vehicle_edge_summary_ = true;

    std::cout << "[safety_io_adapters] Published VehicleEdgeSummary mode="
              << static_cast<int32_t>(summary.current_mode) << std::endl;
}

void SafetyIoAdaptersNode::on_policy_decision_received(
        const safe_edge::internal::PolicyDecision& decision)
{
    latest_policy_decision_ = decision;
    have_policy_decision_ = true;
    pending_vehicle_edge_summary_publish_ = true;

    std::cout << "[safety_io_adapters] Received PolicyDecision mode=" << static_cast<int32_t>(decision.mode)
              << " allow_non_safety=" << decision.allow_non_safety
              << " allow_ota=" << decision.allow_ota
              << " reason=" << decision.reason << std::endl;
}

void SafetyIoAdaptersNode::on_energy_advisory_received(
        const safe_edge::edge::EnergyAdvisory& advisory)
{
    last_advisory_received_ms_ = common::HeaderFactory::now_ms();

    std::cout << "[safety_io_adapters] Received EnergyAdvisory mode="
              << static_cast<int32_t>(advisory.suggested_mode)
              << " reason=" << advisory.advisory_reason << std::endl;
}

void SafetyIoAdaptersNode::on_peer_heartbeat_received(
        const safe_edge::common::ServiceHeartbeat& heartbeat)
{
    static_cast<void>(heartbeat);
}

void SafetyIoAdaptersNode::log_subscription_match(
        const char* topic_name,
        int32_t total_count) const
{
    std::cout << "[safety_io_adapters] Subscription matched on " << topic_name
              << " total_count=" << total_count << std::endl;
}

void SafetyIoAdaptersNode::log_publication_match(
        const char* topic_name,
        int32_t total_count) const
{
    std::cout << "[safety_io_adapters] Publication matched on " << topic_name
              << " total_count=" << total_count << std::endl;
}

eprosima::safedds::execution::TimePoint SafetyIoAdaptersNode::next_wakeup_time() const noexcept
{
    eprosima::safedds::execution::TimePoint next = executor_->get_next_work_timepoint();
    next = eprosima::safedds::execution::TimePoint::min(next, edge_gateway_status_timer_.next_trigger());
    next = eprosima::safedds::execution::TimePoint::min(next, heartbeat_timer_.next_trigger());
    return next;
}

} // namespace nodes
} // namespace safety_domain
} // namespace safe_edge
