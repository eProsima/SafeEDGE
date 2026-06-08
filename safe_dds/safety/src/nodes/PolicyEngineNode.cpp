#include <safe_edge/safety_domain/nodes/PolicyEngineNode.hpp>

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
<<<<<<< Updated upstream
=======
#include <safedds/platform.hpp>
>>>>>>> Stashed changes
#include <safedds/transport.hpp>

#include <iostream>

namespace safe_edge {
namespace safety_domain {
namespace nodes {

namespace {

constexpr eprosima::safedds::execution::TimePeriod TIMEOUT = {5, 0};
constexpr uint64_t EDGE_STATUS_TIMEOUT_MS = 10000U;

template<typename TypeSupportT>
bool register_type(
        eprosima::safedds::dds::DomainParticipant& participant,
        TypeSupportT& type_support,
        const char* label)
{
    if (eprosima::safedds::dds::ReturnCode::OK != type_support.register_type(participant, type_support.get_type_name()))
    {
        std::cerr << "[policy_engine] Failed to register type: " << label << std::endl;
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

PolicyEngineNode::ParticipantListener::ParticipantListener(
        PolicyEngineNode& owner)
    : owner_(owner)
{
}

void PolicyEngineNode::ParticipantListener::on_subscription_matched(
        eprosima::safedds::dds::DataReader& reader,
        const eprosima::safedds::dds::SubscriptionMatchedStatus& info) noexcept
{
    owner_.log_subscription_match(reader.get_topicdescription().get_name().const_string_data(), info.total_count);
}

void PolicyEngineNode::ParticipantListener::on_publication_matched(
        eprosima::safedds::dds::DataWriter& writer,
        const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept
{
    owner_.log_publication_match(writer.get_topic().get_name().const_string_data(), info.total_count);
}

PolicyEngineNode::SafetyInputFrameListener::SafetyInputFrameListener(
        PolicyEngineNode& owner)
    : owner_(owner)
{
}

void PolicyEngineNode::SafetyInputFrameListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::internal::SafetyInputFrameTypeSupport>::downcast(
        reader);

    if (nullptr == typed_reader)
    {
        std::cerr << "[policy_engine] Failed to downcast safety input reader" << std::endl;
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

PolicyEngineNode::EnergyAdvisoryListener::EnergyAdvisoryListener(
        PolicyEngineNode& owner)
    : owner_(owner)
{
}

void PolicyEngineNode::EnergyAdvisoryListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::edge::EnergyAdvisoryTypeSupport>::downcast(
        reader);

    if (nullptr == typed_reader)
    {
        std::cerr << "[policy_engine] Failed to downcast energy advisory reader" << std::endl;
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

PolicyEngineNode::EdgeGatewayStatusListener::EdgeGatewayStatusListener(
        PolicyEngineNode& owner)
    : owner_(owner)
{
}

void PolicyEngineNode::EdgeGatewayStatusListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::edge::EdgeGatewayStatusTypeSupport>::downcast(
        reader);

    if (nullptr == typed_reader)
    {
        std::cerr << "[policy_engine] Failed to downcast gateway status reader" << std::endl;
        return;
    }

    safe_edge::edge::EdgeGatewayStatus sample{};
    eprosima::safedds::dds::SampleInfo info{};

    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            owner_.on_edge_gateway_status_received(sample);
        }
    }
}

PolicyEngineNode::HeartbeatListener::HeartbeatListener(
        PolicyEngineNode& owner)
    : owner_(owner)
{
}

void PolicyEngineNode::HeartbeatListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::common::ServiceHeartbeatTypeSupport>::downcast(
        reader);

    if (nullptr == typed_reader)
    {
        std::cerr << "[policy_engine] Failed to downcast heartbeat reader" << std::endl;
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

PolicyEngineNode::ChargingResponseListener::ChargingResponseListener(
        PolicyEngineNode& owner)
    : owner_(owner)
{
}

void PolicyEngineNode::ChargingResponseListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::internal::ChargingResponseTypeSupport>::downcast(
        reader);

    if (nullptr == typed_reader)
    {
        std::cerr << "[policy_engine] Failed to downcast charging response reader" << std::endl;
        return;
    }

    safe_edge::internal::ChargingResponse sample{};
    eprosima::safedds::dds::SampleInfo info{};

    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            owner_.on_charging_response_received(sample);
        }
    }
}

PolicyEngineNode::ServerAvailabilityStatusListener::ServerAvailabilityStatusListener(
        PolicyEngineNode& owner)
    : owner_(owner)
{
}

void PolicyEngineNode::ServerAvailabilityStatusListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::internal::ServerAvailabilityStatusTypeSupport>::downcast(
        reader);

    if (nullptr == typed_reader)
    {
        std::cerr << "[policy_engine] Failed to downcast server_availability_status reader" << std::endl;
        return;
    }

    safe_edge::internal::ServerAvailabilityStatus sample{};
    eprosima::safedds::dds::SampleInfo info{};

    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            owner_.on_server_availability_status_received(sample);
        }
    }
}

PolicyEngineNode::EdgeChargerResponseListener::EdgeChargerResponseListener(
        PolicyEngineNode& owner)
    : owner_(owner)
{
}

void PolicyEngineNode::EdgeChargerResponseListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::internal::ChargingResponseTypeSupport>::downcast(
        reader);

    if (nullptr == typed_reader)
    {
        std::cerr << "[policy_engine] Failed to downcast edge_charger_response reader" << std::endl;
        return;
    }

    safe_edge::internal::ChargingResponse sample{};
    eprosima::safedds::dds::SampleInfo info{};

    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            owner_.on_edge_charger_response_received(sample);
        }
    }
}

PolicyEngineNode::PolicyEngineNode(
        const common::RuntimeConfig& runtime_config)
    : runtime_config_(runtime_config)
    , header_factory_(runtime_config.source_name)
    , participant_listener_(*this)
    , safety_input_frame_listener_(*this)
    , energy_advisory_listener_(*this)
    , edge_gateway_status_listener_(*this)
    , heartbeat_listener_(*this)
    , charging_response_listener_(*this)
    , server_availability_status_listener_(*this)
    , edge_charger_response_listener_(*this)
    , heartbeat_timer_(TIMEOUT)
{
}

int PolicyEngineNode::run()
{
    if (!initialize())
    {
        return 1;
    }

    start_timers();
    std::cout << "[policy_engine] [START] Running with participant port " << runtime_config_.participant_port << std::endl;

    while (true)
    {
        while (executor_->has_pending_work())
        {
            executor_->spin(eprosima::safedds::execution::TIME_ZERO);
        }

        if (pending_policy_publish_)
        {
            publish_policy_decision();
        }

        if (heartbeat_timer_.is_triggered_and_reset())
        {
            publish_heartbeat();
            evaluate_and_publish();
        }

        executor_->spin(next_wakeup_time());
    }

    return 0;
}

bool PolicyEngineNode::initialize()
{
    return create_participant() &&
           register_types() &&
           create_topics() &&
           create_endpoints() &&
           enable_entities() &&
           create_executor();
}

bool PolicyEngineNode::create_participant()
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
        std::cerr << "[policy_engine] Failed to create participant" << std::endl;
        return false;
    }

    return true;
}

bool PolicyEngineNode::register_types()
{
    return register_type(*participant_, safety_input_frame_type_support_, "SafetyInputFrame") &&
           register_type(*participant_, energy_advisory_type_support_, "EnergyAdvisory") &&
           register_type(*participant_, edge_gateway_status_type_support_, "EdgeGatewayStatus") &&
           register_type(*participant_, policy_decision_type_support_, "PolicyDecision") &&
           register_type(*participant_, service_heartbeat_type_support_, "ServiceHeartbeat") &&
           register_type(*participant_, charging_query_type_support_, "ChargingQuery") &&
           register_type(*participant_, charging_response_type_support_, "ChargingResponse") &&
           register_type(*participant_, server_availability_status_type_support_, "ServerAvailabilityStatus");
}

bool PolicyEngineNode::create_topics()
{
    safety_input_frame_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::safety_input_frame());
    safety_input_frame_topic_ = create_topic(*participant_, safety_input_frame_topic_name_, safety_input_frame_type_support_);
    if (nullptr == safety_input_frame_topic_) { std::cerr << "[policy_engine] Failed to create topic: safety_input_frame" << std::endl; return false; }

    energy_advisory_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::energy_advisory());
    energy_advisory_topic_ = create_topic(*participant_, energy_advisory_topic_name_, energy_advisory_type_support_);
    if (nullptr == energy_advisory_topic_) { std::cerr << "[policy_engine] Failed to create topic: energy_advisory" << std::endl; return false; }

    edge_gateway_status_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::edge_gateway_status());
    edge_gateway_status_topic_ = create_topic(*participant_, edge_gateway_status_topic_name_, edge_gateway_status_type_support_);
    if (nullptr == edge_gateway_status_topic_) { std::cerr << "[policy_engine] Failed to create topic: edge_gateway_status" << std::endl; return false; }

    policy_decision_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::policy_decision());
    policy_decision_topic_ = create_topic(*participant_, policy_decision_topic_name_, policy_decision_type_support_);
    if (nullptr == policy_decision_topic_) { std::cerr << "[policy_engine] Failed to create topic: policy_decision" << std::endl; return false; }

    service_heartbeat_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::service_heartbeat());
    service_heartbeat_topic_ = create_topic(*participant_, service_heartbeat_topic_name_, service_heartbeat_type_support_);
    if (nullptr == service_heartbeat_topic_) { std::cerr << "[policy_engine] Failed to create topic: service_heartbeat" << std::endl; return false; }

    charging_query_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::charging_query());
    charging_query_topic_ = create_topic(*participant_, charging_query_topic_name_, charging_query_type_support_);
    if (nullptr == charging_query_topic_) { std::cerr << "[policy_engine] Failed to create topic: charging_query" << std::endl; return false; }

    charging_response_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::charging_response());
    charging_response_topic_ = create_topic(*participant_, charging_response_topic_name_, charging_response_type_support_);
    if (nullptr == charging_response_topic_) { std::cerr << "[policy_engine] Failed to create topic: charging_response" << std::endl; return false; }

    server_availability_status_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::server_availability_status());
    server_availability_status_topic_ = create_topic(*participant_, server_availability_status_topic_name_, server_availability_status_type_support_);
    if (nullptr == server_availability_status_topic_) { std::cerr << "[policy_engine] Failed to create topic: server_availability_status" << std::endl; return false; }

    edge_charger_query_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::edge_charger_query());
    edge_charger_query_topic_ = create_topic(*participant_, edge_charger_query_topic_name_, charging_query_type_support_);
    if (nullptr == edge_charger_query_topic_) { std::cerr << "[policy_engine] Failed to create topic: edge_charger_query" << std::endl; return false; }

    edge_charger_response_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::edge_charger_response());
    edge_charger_response_topic_ = create_topic(*participant_, edge_charger_response_topic_name_, charging_response_type_support_);
    if (nullptr == edge_charger_response_topic_) { std::cerr << "[policy_engine] Failed to create topic: edge_charger_response" << std::endl; return false; }

    return true;
}

bool PolicyEngineNode::create_endpoints()
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
        std::cerr << "[policy_engine] Failed to create publisher or subscriber" << std::endl;
        return false;
    }

    eprosima::safedds::dds::DataWriterQos writer_qos{};
    writer_qos.reliability().kind = eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    policy_decision_datawriter_ = publisher_->create_datawriter(
        *policy_decision_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    service_heartbeat_datawriter_ = publisher_->create_datawriter(
        *service_heartbeat_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);

    charging_query_datawriter_ = publisher_->create_datawriter(
        *charging_query_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    edge_charger_query_datawriter_ = publisher_->create_datawriter(
        *edge_charger_query_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    energy_advisory_out_datawriter_ = publisher_->create_datawriter(
        *energy_advisory_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);

    policy_decision_writer_ = downcast_writer<safe_edge::internal::PolicyDecisionTypeSupport>(policy_decision_datawriter_);
    service_heartbeat_writer_ = downcast_writer<safe_edge::common::ServiceHeartbeatTypeSupport>(service_heartbeat_datawriter_);
    charging_query_writer_ = downcast_writer<safe_edge::internal::ChargingQueryTypeSupport>(charging_query_datawriter_);
    edge_charger_query_writer_ = downcast_writer<safe_edge::internal::ChargingQueryTypeSupport>(edge_charger_query_datawriter_);
    energy_advisory_out_writer_ = downcast_writer<safe_edge::edge::EnergyAdvisoryTypeSupport>(energy_advisory_out_datawriter_);

    eprosima::safedds::dds::DataReaderQos reader_qos{};
    reader_qos.reliability().kind = eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    safety_input_frame_datareader_ = subscriber_->create_datareader(
        *safety_input_frame_topic_,
        reader_qos,
        &safety_input_frame_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    energy_advisory_datareader_ = subscriber_->create_datareader(
        *energy_advisory_topic_,
        reader_qos,
        &energy_advisory_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    edge_gateway_status_datareader_ = subscriber_->create_datareader(
        *edge_gateway_status_topic_,
        reader_qos,
        &edge_gateway_status_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    heartbeat_datareader_ = subscriber_->create_datareader(
        *service_heartbeat_topic_,
        reader_qos,
        &heartbeat_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);

    safety_input_frame_reader_ = downcast_reader<safe_edge::internal::SafetyInputFrameTypeSupport>(safety_input_frame_datareader_);
    energy_advisory_reader_ = downcast_reader<safe_edge::edge::EnergyAdvisoryTypeSupport>(energy_advisory_datareader_);
    edge_gateway_status_reader_ = downcast_reader<safe_edge::edge::EdgeGatewayStatusTypeSupport>(edge_gateway_status_datareader_);
    charging_response_datareader_ = subscriber_->create_datareader(
        *charging_response_topic_,
        reader_qos,
        &charging_response_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    server_availability_status_datareader_ = subscriber_->create_datareader(
        *server_availability_status_topic_,
        reader_qos,
        &server_availability_status_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    server_availability_status_reader_ = downcast_reader<safe_edge::internal::ServerAvailabilityStatusTypeSupport>(server_availability_status_datareader_);

    edge_charger_response_datareader_ = subscriber_->create_datareader(
        *edge_charger_response_topic_,
        reader_qos,
        &edge_charger_response_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    edge_charger_response_reader_ = downcast_reader<safe_edge::internal::ChargingResponseTypeSupport>(edge_charger_response_datareader_);

    heartbeat_reader_ = downcast_reader<safe_edge::common::ServiceHeartbeatTypeSupport>(heartbeat_datareader_);
    charging_response_reader_ = downcast_reader<safe_edge::internal::ChargingResponseTypeSupport>(charging_response_datareader_);

    const bool success = nullptr != policy_decision_writer_ &&
            nullptr != service_heartbeat_writer_ &&
            nullptr != charging_query_writer_ &&
            nullptr != edge_charger_query_writer_ &&
            nullptr != energy_advisory_out_writer_ &&
            nullptr != safety_input_frame_reader_ &&
            nullptr != energy_advisory_reader_ &&
            nullptr != edge_gateway_status_reader_ &&
            nullptr != heartbeat_reader_ &&
            nullptr != charging_response_reader_ &&
            nullptr != server_availability_status_reader_ &&
            nullptr != edge_charger_response_reader_;

    if (!success)
    {
        std::cerr << "[policy_engine] Failed to create endpoints" << std::endl;
        return false;
    }

    return true;
}

bool PolicyEngineNode::enable_entities()
{
    bool enabled = true;
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == publisher_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == policy_decision_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == service_heartbeat_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == charging_query_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == energy_advisory_out_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == subscriber_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == safety_input_frame_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == energy_advisory_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == edge_gateway_status_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == heartbeat_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == charging_response_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == edge_charger_query_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == server_availability_status_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == edge_charger_response_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == participant_->enable());

    if (!enabled)
    {
        std::cerr << "[policy_engine] Failed to enable DDS entities" << std::endl;
    }

    return enabled;
}

bool PolicyEngineNode::create_executor()
{
     executor_ = factory_.create_default_executor();

    if (nullptr == executor_)
    {
        std::cerr << "[policy_engine] Failed to create executor" << std::endl;
        return false;
    }

    return true;
}

void PolicyEngineNode::start_timers() noexcept
{
    heartbeat_timer_.start();
}

void PolicyEngineNode::evaluate_and_publish()
{
    if (!have_safety_input_frame_)
    {
        return;
    }

    pending_policy_publish_ = true;
}

void PolicyEngineNode::publish_policy_decision()
{
    pending_policy_publish_ = false;

    const safe_edge::edge::EnergyAdvisory* advisory = have_energy_advisory_ ? &latest_energy_advisory_ : nullptr;
    const safe_edge::edge::EdgeGatewayStatus* gateway_status = have_edge_gateway_status_ ? &latest_edge_gateway_status_ : nullptr;

    const bool edge_available = have_edge_gateway_status_ &&
        (common::HeaderFactory::now_ms() - edge_status_last_ms_) < EDGE_STATUS_TIMEOUT_MS;

    const policy::PolicyInputs inputs = policy::to_policy_inputs(
        latest_safety_input_frame_,
        advisory,
        gateway_status,
        server_available_,
        edge_available);
    const policy::PolicyOutputs outputs = policy_engine_.evaluate(inputs);
    const safe_edge::internal::PolicyDecision decision = policy::to_policy_decision(
        outputs,
        header_factory_.make_header("policy_decision"));

    if (eprosima::safedds::dds::ReturnCode::OK != policy_decision_writer_->write(decision, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[policy_engine] Failed to publish PolicyDecision" << std::endl;
        return;
    }

<<<<<<< Updated upstream
    std::cout << "[policy_engine] Published PolicyDecision mode=" << static_cast<int32_t>(decision.mode)
=======
    const eprosima::safedds::execution::TimePoint t_dec =
        eprosima::safedds::get_platform().get_current_timepoint();
    std::cout << "[policy_engine] Published PolicyDecision"
              << " t_dec=" << t_dec.seconds << "." << t_dec.nanoseconds
              << " mode=" << static_cast<int32_t>(decision.mode)
>>>>>>> Stashed changes
              << " allow_non_safety=" << decision.allow_non_safety
              << " allow_ota=" << decision.allow_ota
              << " reason=" << decision.reason << std::endl;
}

void PolicyEngineNode::publish_heartbeat()
{
    safe_edge::common::ServiceHeartbeat heartbeat;
    heartbeat.header_st = header_factory_.make_header("service_heartbeat");
    heartbeat.service_name = runtime_config_.service_name;
    heartbeat.status = safe_edge::common::HealthStatus::HEALTH_OK;
    heartbeat.detail = "running";

    if (eprosima::safedds::dds::ReturnCode::OK != service_heartbeat_writer_->write(heartbeat, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[policy_engine] Failed to publish ServiceHeartbeat" << std::endl;
        return;
    }

    std::cout << "[policy_engine] Published ServiceHeartbeat" << std::endl;
}

void PolicyEngineNode::on_safety_input_frame_received(
        const safe_edge::internal::SafetyInputFrame& frame)
{
<<<<<<< Updated upstream
    latest_safety_input_frame_ = frame;
    have_safety_input_frame_ = true;
    std::cout << "[policy_engine] Received SafetyInputFrame soc=" << frame.battery.soc_pct << std::endl;
=======
    const eprosima::safedds::execution::TimePoint t_rx =
        eprosima::safedds::get_platform().get_current_timepoint();
    latest_safety_input_frame_ = frame;
    have_safety_input_frame_ = true;
    std::cout << "[policy_engine] Received SafetyInputFrame"
              << " t_rx=" << t_rx.seconds << "." << t_rx.nanoseconds
              << " soc=" << frame.battery.soc_pct
              << " emergency_stop=" << frame.safety.emergency_stop << std::endl;
>>>>>>> Stashed changes

    if (frame.battery.soc_pct < 20.0F)
    {
        const bool edge_available = have_edge_gateway_status_ &&
            (common::HeaderFactory::now_ms() - edge_status_last_ms_) < EDGE_STATUS_TIMEOUT_MS;

        if (server_available_ && !charging_query_pending_)
        {
            publish_charging_query(frame.battery.soc_pct);
        }
        else if (!server_available_ && edge_available && !edge_charger_query_pending_)
        {
            publish_edge_charger_query(frame.battery.soc_pct);
        }
        // else: both down → no query (degraded_complete)
    }

    evaluate_and_publish();
}

void PolicyEngineNode::publish_charging_query(float soc_pct)
{
    charging_query_pending_ = true;

    safe_edge::internal::ChargingQuery query{};
    query.header = header_factory_.make_header("charging_query");
    query.soc_pct = soc_pct;

    if (eprosima::safedds::dds::ReturnCode::OK !=
            charging_query_writer_->write(query, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[policy_engine] Failed to publish ChargingQuery" << std::endl;
        charging_query_pending_ = false;
        return;
    }

    std::cout << "[policy_engine] Published ChargingQuery soc=" << soc_pct << std::endl;
}

void PolicyEngineNode::on_charging_response_received(
        const safe_edge::internal::ChargingResponse& response)
{
    charging_query_pending_ = false;
    std::cout << "[policy_engine] Received ChargingResponse has_charger=" << response.has_charger
              << " charger_id=" << response.preferred_charger_id << std::endl;
}

void PolicyEngineNode::publish_edge_charger_query(float soc_pct)
{
    edge_charger_query_pending_ = true;

    safe_edge::internal::ChargingQuery query{};
    query.header = header_factory_.make_header("edge_charger_query");
    query.soc_pct = soc_pct;

    if (eprosima::safedds::dds::ReturnCode::OK !=
            edge_charger_query_writer_->write(query, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[policy_engine] Failed to publish edge_charger_query" << std::endl;
        edge_charger_query_pending_ = false;
        return;
    }

    std::cout << "[policy_engine] Published edge_charger_query soc=" << soc_pct << std::endl;
}

void PolicyEngineNode::on_server_availability_status_received(
        const safe_edge::internal::ServerAvailabilityStatus& status)
{
    server_available_ = status.server_available;
    std::cout << "[policy_engine] Received ServerAvailabilityStatus server_available="
              << (status.server_available ? "true" : "false")
              << " detail=" << status.detail << std::endl;
    evaluate_and_publish();
}

void PolicyEngineNode::on_edge_charger_response_received(
        const safe_edge::internal::ChargingResponse& response)
{
    edge_charger_query_pending_ = false;
    std::cout << "[policy_engine] Received edge_charger_response has_charger=" << response.has_charger
              << " charger_id=" << response.preferred_charger_id << std::endl;
}

void PolicyEngineNode::on_energy_advisory_received(
        const safe_edge::edge::EnergyAdvisory& advisory)
{
    latest_energy_advisory_ = advisory;
    have_energy_advisory_ = true;
    std::cout << "[policy_engine] Received EnergyAdvisory mode=" << static_cast<int32_t>(advisory.suggested_mode)
              << " reason=" << advisory.advisory_reason << std::endl;
    evaluate_and_publish();
}

void PolicyEngineNode::on_edge_gateway_status_received(
        const safe_edge::edge::EdgeGatewayStatus& status)
{
    latest_edge_gateway_status_ = status;
    have_edge_gateway_status_ = true;
    edge_status_last_ms_ = common::HeaderFactory::now_ms();
    std::cout << "[policy_engine] Received EdgeGatewayStatus status=" << static_cast<int32_t>(status.status)
              << " detail=" << status.detail << std::endl;
    evaluate_and_publish();
}

void PolicyEngineNode::on_peer_heartbeat_received(
        const safe_edge::common::ServiceHeartbeat& heartbeat)
{
    if (heartbeat.service_name == runtime_config_.service_name)
    {
        return;
    }

    if (heartbeat.service_name != "cloud_gateway")
    {
        return;
    }

    static_cast<void>(heartbeat);
}

void PolicyEngineNode::log_subscription_match(
        const char* topic_name,
        int32_t total_count) const
{
    std::cout << "[policy_engine] Subscription matched on " << topic_name
              << " total_count=" << total_count << std::endl;
}

void PolicyEngineNode::log_publication_match(
        const char* topic_name,
        int32_t total_count) const
{
    std::cout << "[policy_engine] Publication matched on " << topic_name
              << " total_count=" << total_count << std::endl;
}

eprosima::safedds::execution::TimePoint PolicyEngineNode::next_wakeup_time() const noexcept
{
    eprosima::safedds::execution::TimePoint next = executor_->get_next_work_timepoint();
    next = eprosima::safedds::execution::TimePoint::min(next, heartbeat_timer_.next_trigger());
    return next;
}

} // namespace nodes
} // namespace safety_domain
} // namespace safe_edge
