#include <safe_edge/non_safety_domain/nodes/OtaServiceNode.hpp>

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
        std::cerr << "[ota_service] Failed to register type: " << label << std::endl;
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

OtaServiceNode::ParticipantListener::ParticipantListener(
        OtaServiceNode& owner)
    : owner_(owner)
{
}

void OtaServiceNode::ParticipantListener::on_subscription_matched(
        eprosima::safedds::dds::DataReader& reader,
        const eprosima::safedds::dds::SubscriptionMatchedStatus& info) noexcept
{
    owner_.log_subscription_match(reader.get_topicdescription().get_name().const_string_data(), info.total_count);
}

void OtaServiceNode::ParticipantListener::on_publication_matched(
        eprosima::safedds::dds::DataWriter& writer,
        const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept
{
    owner_.log_publication_match(writer.get_topic().get_name().const_string_data(), info.total_count);
}

OtaServiceNode::ChargerLocationsListener::ChargerLocationsListener(
        OtaServiceNode& owner)
    : owner_(owner)
{
}

void OtaServiceNode::ChargerLocationsListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::ChargerLocationTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[ota_service] Failed to downcast charger_locations reader" << std::endl;
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

OtaServiceNode::ChargerTypesListener::ChargerTypesListener(
        OtaServiceNode& owner)
    : owner_(owner)
{
}

void OtaServiceNode::ChargerTypesListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::ChargerTypeTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[ota_service] Failed to downcast charger_types reader" << std::endl;
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

OtaServiceNode::ChargingSessionsListener::ChargingSessionsListener(
        OtaServiceNode& owner)
    : owner_(owner)
{
}

void OtaServiceNode::ChargingSessionsListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::ChargingSessionTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[ota_service] Failed to downcast charging_sessions reader" << std::endl;
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

OtaServiceNode::HeartbeatListener::HeartbeatListener(
        OtaServiceNode& owner)
    : owner_(owner)
{
}

void OtaServiceNode::HeartbeatListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::common::ServiceHeartbeatTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[ota_service] Failed to downcast heartbeat reader" << std::endl;
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

OtaServiceNode::OtaServiceNode(
        const common::RuntimeConfig& runtime_config)
    : runtime_config_(runtime_config)
    , header_factory_(runtime_config.source_name)
    , participant_listener_(*this)
    , charger_locations_listener_(*this)
    , charger_types_listener_(*this)
    , charging_sessions_listener_(*this)
    , heartbeat_listener_(*this)
    , heartbeat_timer_(TIMEOUT)
{
}

int OtaServiceNode::run()
{
    if (!initialize())
    {
        return 1;
    }

    start_timers();
    std::cout << "[ota_service] [START] Running with participant port " << runtime_config_.participant_port << std::endl;

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

        executor_->spin(next_wakeup_time());
    }

    return 0;
}

bool OtaServiceNode::initialize()
{
    return create_participant() &&
           register_types() &&
           create_topics() &&
           create_endpoints() &&
           enable_entities() &&
           create_executor();
}

bool OtaServiceNode::create_participant()
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
        std::cerr << "[ota_service] Failed to create participant" << std::endl;
        return false;
    }

    return true;
}

bool OtaServiceNode::register_types()
{
    return register_type(*participant_, charger_locations_type_support_, "ChargerLocationSeq") &&
           register_type(*participant_, charger_types_type_support_, "ChargerTypeSeq") &&
           register_type(*participant_, charging_sessions_type_support_, "ChargingSessionSeq") &&
           register_type(*participant_, service_heartbeat_type_support_, "ServiceHeartbeat");
}

bool OtaServiceNode::create_topics()
{
    charger_locations_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::charger_locations());
    charger_locations_topic_ = create_topic(*participant_, charger_locations_topic_name_, charger_locations_type_support_);
    if (nullptr == charger_locations_topic_) { std::cerr << "[ota_service] Failed to create topic: charger_locations" << std::endl; return false; }

    charger_types_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::charger_types());
    charger_types_topic_ = create_topic(*participant_, charger_types_topic_name_, charger_types_type_support_);
    if (nullptr == charger_types_topic_) { std::cerr << "[ota_service] Failed to create topic: charger_types" << std::endl; return false; }

    charging_sessions_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::charging_sessions());
    charging_sessions_topic_ = create_topic(*participant_, charging_sessions_topic_name_, charging_sessions_type_support_);
    if (nullptr == charging_sessions_topic_) { std::cerr << "[ota_service] Failed to create topic: charging_sessions" << std::endl; return false; }

    service_heartbeat_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::service_heartbeat());
    service_heartbeat_topic_ = create_topic(*participant_, service_heartbeat_topic_name_, service_heartbeat_type_support_);
    if (nullptr == service_heartbeat_topic_) { std::cerr << "[ota_service] Failed to create topic: service_heartbeat" << std::endl; return false; }

    return true;
}

bool OtaServiceNode::create_endpoints()
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
        std::cerr << "[ota_service] Failed to create publisher or subscriber" << std::endl;
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

    eprosima::safedds::dds::DataReaderQos reader_qos{};
    reader_qos.reliability().kind = eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    charger_locations_datareader_ = subscriber_->create_datareader(*charger_locations_topic_, reader_qos, &charger_locations_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    charger_types_datareader_ = subscriber_->create_datareader(*charger_types_topic_, reader_qos, &charger_types_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    charging_sessions_datareader_ = subscriber_->create_datareader(*charging_sessions_topic_, reader_qos, &charging_sessions_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    heartbeat_datareader_ = subscriber_->create_datareader(*service_heartbeat_topic_, reader_qos, &heartbeat_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);

    charger_locations_reader_ = downcast_reader<safe_edge::pilot_server::ChargerLocationTypeSupport>(charger_locations_datareader_);
    charger_types_reader_ = downcast_reader<safe_edge::pilot_server::ChargerTypeTypeSupport>(charger_types_datareader_);
    charging_sessions_reader_ = downcast_reader<safe_edge::pilot_server::ChargingSessionTypeSupport>(charging_sessions_datareader_);
    heartbeat_reader_ = downcast_reader<safe_edge::common::ServiceHeartbeatTypeSupport>(heartbeat_datareader_);

    const bool success = nullptr != service_heartbeat_writer_ &&
            nullptr != charger_locations_reader_ &&
            nullptr != charger_types_reader_ &&
            nullptr != charging_sessions_reader_ &&
            nullptr != heartbeat_reader_;

    if (!success)
    {
        std::cerr << "[ota_service] Failed to create endpoints" << std::endl;
        return false;
    }

    return true;
}

bool OtaServiceNode::enable_entities()
{
    bool enabled = true;
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == publisher_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == service_heartbeat_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == subscriber_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == charger_locations_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == charger_types_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == charging_sessions_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == heartbeat_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == participant_->enable());

    if (!enabled)
    {
        std::cerr << "[ota_service] Failed to enable DDS entities" << std::endl;
    }

    return enabled;
}

bool OtaServiceNode::create_executor()
{
    executor_ = factory_.create_default_executor();

    if (nullptr == executor_)
    {
        std::cerr << "[ota_service] Failed to create executor" << std::endl;
        return false;
    }

    return true;
}

void OtaServiceNode::start_timers() noexcept
{
    heartbeat_timer_.start();
}

void OtaServiceNode::on_charger_locations_received(
        const safe_edge::pilot_server::ChargerLocationSeq& locations)
{
    std::cout << "[ota_service] Received ChargerLocationSeq count=" << locations.size() << std::endl;
}

void OtaServiceNode::on_charger_types_received(
        const safe_edge::pilot_server::ChargerTypeSeq& types)
{
    std::cout << "[ota_service] Received ChargerTypeSeq count=" << types.size() << std::endl;
}

void OtaServiceNode::on_charging_sessions_received(
        const safe_edge::pilot_server::ChargingSessionSeq& sessions)
{
    std::cout << "[ota_service] Received ChargingSessionSeq count=" << sessions.size() << std::endl;
}

void OtaServiceNode::publish_heartbeat()
{
    safe_edge::common::ServiceHeartbeat heartbeat;
    heartbeat.header_st = header_factory_.make_header("service_heartbeat");
    heartbeat.service_name = runtime_config_.service_name;
    heartbeat.status = safe_edge::common::HealthStatus::HEALTH_OK;
    heartbeat.detail = "running";

    if (eprosima::safedds::dds::ReturnCode::OK != service_heartbeat_writer_->write(heartbeat, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[ota_service] Failed to publish ServiceHeartbeat" << std::endl;
        return;
    }

    std::cout << "[ota_service] Published ServiceHeartbeat" << std::endl;
}

void OtaServiceNode::on_peer_heartbeat_received(
        const safe_edge::common::ServiceHeartbeat& heartbeat)
{
    static_cast<void>(heartbeat);
}

void OtaServiceNode::log_subscription_match(
        const char* topic_name,
        int32_t total_count) const
{
    std::cout << "[ota_service] Subscription matched on " << topic_name
              << " total_count=" << total_count << std::endl;
}

void OtaServiceNode::log_publication_match(
        const char* topic_name,
        int32_t total_count) const
{
    std::cout << "[ota_service] Publication matched on " << topic_name
              << " total_count=" << total_count << std::endl;
}

eprosima::safedds::execution::TimePoint OtaServiceNode::next_wakeup_time() const noexcept
{
    eprosima::safedds::execution::TimePoint next = executor_->get_next_work_timepoint();
    next = eprosima::safedds::execution::TimePoint::min(next, heartbeat_timer_.next_trigger());
    return next;
}

} // namespace nodes
} // namespace non_safety_domain
} // namespace safe_edge
