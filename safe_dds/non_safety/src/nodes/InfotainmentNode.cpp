#include <safe_edge/non_safety_domain/nodes/InfotainmentNode.hpp>

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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>

namespace safe_edge {
namespace non_safety_domain {
namespace nodes {

namespace {

constexpr eprosima::safedds::execution::TimePeriod HEARTBEAT_PERIOD    = {0, 100'000'000};
constexpr eprosima::safedds::execution::TimePeriod STATUS_WRITE_PERIOD = {0, 250'000'000};
constexpr uint64_t LIVENESS_THRESHOLD_MS = 2000U;
constexpr uint64_t SERVER_STATUS_TIMEOUT_MS = 2000U;
constexpr size_t MAX_LIVENESS_ENTRIES = 16U;

const char* DEFAULT_STATUS_FILE = "/tmp/safe-edge-status.json";

uint64_t monotonic_ms() noexcept
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
           static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
}

uint64_t realtime_ms() noexcept
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
           static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
}

const char* health_status_to_text(safe_edge::common::HealthStatus status) noexcept
{
    switch (status)
    {
        case safe_edge::common::HealthStatus::HEALTH_OK:
            return "HEALTH_OK";
        case safe_edge::common::HealthStatus::HEALTH_DEGRADED:
            return "HEALTH_DEGRADED";
        case safe_edge::common::HealthStatus::HEALTH_ERROR:
            return "HEALTH_ERROR";
        default:
            return "HEALTH_UNKNOWN";
    }
}

template<typename TypeSupportT>
bool register_type(
        eprosima::safedds::dds::DomainParticipant& participant,
        TypeSupportT& type_support,
        const char* label)
{
    if (eprosima::safedds::dds::ReturnCode::OK != type_support.register_type(participant, type_support.get_type_name()))
    {
        std::cerr << "[infotainment] Failed to register type: " << label << std::endl;
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

InfotainmentNode::ParticipantListener::ParticipantListener(
        InfotainmentNode& owner)
    : owner_(owner)
{
}

void InfotainmentNode::ParticipantListener::on_subscription_matched(
        eprosima::safedds::dds::DataReader& reader,
        const eprosima::safedds::dds::SubscriptionMatchedStatus& info) noexcept
{
    owner_.log_subscription_match(reader.get_topicdescription().get_name().const_string_data(), info.total_count);
}

void InfotainmentNode::ParticipantListener::on_publication_matched(
        eprosima::safedds::dds::DataWriter& writer,
        const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept
{
    owner_.log_publication_match(writer.get_topic().get_name().const_string_data(), info.total_count);
}

InfotainmentNode::TransitHealthListener::TransitHealthListener(
        InfotainmentNode& owner)
    : owner_(owner)
{
}

void InfotainmentNode::TransitHealthListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::TransitHealthTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[infotainment] Failed to downcast transit_health reader" << std::endl;
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

InfotainmentNode::RouteMetricsListener::RouteMetricsListener(
        InfotainmentNode& owner)
    : owner_(owner)
{
}

void InfotainmentNode::RouteMetricsListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::RouteMetricTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[infotainment] Failed to downcast route_metrics reader" << std::endl;
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

InfotainmentNode::TransitMetricsListener::TransitMetricsListener(
        InfotainmentNode& owner)
    : owner_(owner)
{
}

void InfotainmentNode::TransitMetricsListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::TransitMetricsTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[infotainment] Failed to downcast transit_metrics reader" << std::endl;
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

InfotainmentNode::HeartbeatListener::HeartbeatListener(
        InfotainmentNode& owner)
    : owner_(owner)
{
}

void InfotainmentNode::HeartbeatListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::common::ServiceHeartbeatTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[infotainment] Failed to downcast heartbeat reader" << std::endl;
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

InfotainmentNode::RouteContextQueryListener::RouteContextQueryListener(
        InfotainmentNode& owner)
    : owner_(owner)
{
}

void InfotainmentNode::RouteContextQueryListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader = eprosima::safedds::dds::TypedDataReader<safe_edge::internal::RouteContextQueryTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[infotainment] Failed to downcast route_context_query reader" << std::endl;
        return;
    }

    safe_edge::internal::RouteContextQuery sample{};
    eprosima::safedds::dds::SampleInfo info{};
    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            owner_.on_route_context_query_received(sample);
        }
    }
}

InfotainmentNode::PolicyDecisionListener::PolicyDecisionListener(
        InfotainmentNode& owner)
    : owner_(owner)
{
}

void InfotainmentNode::PolicyDecisionListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader =
        eprosima::safedds::dds::TypedDataReader<safe_edge::internal::PolicyDecisionTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[infotainment] Failed to downcast policy_decision reader" << std::endl;
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

InfotainmentNode::ServerAvailabilityListener::ServerAvailabilityListener(
        InfotainmentNode& owner)
    : owner_(owner)
{
}

void InfotainmentNode::ServerAvailabilityListener::on_data_available(
        eprosima::safedds::dds::DataReader& reader) noexcept
{
    auto* typed_reader =
        eprosima::safedds::dds::TypedDataReader<safe_edge::internal::ServerAvailabilityStatusTypeSupport>::downcast(reader);
    if (nullptr == typed_reader)
    {
        std::cerr << "[infotainment] Failed to downcast server_availability_status reader" << std::endl;
        return;
    }
    safe_edge::internal::ServerAvailabilityStatus sample{};
    eprosima::safedds::dds::SampleInfo info{};
    while (typed_reader->take_next_sample(sample, info) == eprosima::safedds::dds::ReturnCode::OK)
    {
        if (info.valid_data)
        {
            owner_.on_server_availability_received(sample);
        }
    }
}

InfotainmentNode::InfotainmentNode(const common::RuntimeConfig& runtime_config)
    : runtime_config_(runtime_config)
    , header_factory_(runtime_config.source_name)
    , participant_listener_(*this)
    , transit_health_listener_(*this)
    , route_metrics_listener_(*this)
    , transit_metrics_listener_(*this)
    , heartbeat_listener_(*this)
    , route_context_query_listener_(*this)
    , policy_decision_listener_(*this)
    , server_availability_listener_(*this)
    , heartbeat_timer_(HEARTBEAT_PERIOD)
    , status_write_timer_(STATUS_WRITE_PERIOD)
    , liveness_count_(0U)
{
    const char* env_path = std::getenv("SAFE_EDGE_STATUS_FILE");
    const char* path = (nullptr != env_path && '\0' != env_path[0]) ? env_path : DEFAULT_STATUS_FILE;
    std::strncpy(status_file_path_, path, sizeof(status_file_path_) - 1U);
    status_file_path_[sizeof(status_file_path_) - 1U] = '\0';
}

int InfotainmentNode::run()
{
    if (!initialize())
    {
        return 1;
    }

    start_timers();
    std::cout << "[infotainment] [START] Running with participant port "
              << runtime_config_.participant_port << std::endl;
    std::cout << "[infotainment] Status file: " << status_file_path_ << std::endl;

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

        if (status_write_timer_.is_triggered_and_reset())
        {
            write_status_file();
        }

        executor_->spin(next_wakeup_time());
    }

    return 0;
}

bool InfotainmentNode::initialize()
{
    return create_participant() &&
           register_types() &&
           create_topics() &&
           create_endpoints() &&
           enable_entities() &&
           create_executor();
}

bool InfotainmentNode::create_participant()
{
    eprosima::safedds::dds::DomainParticipantQos participant_qos{};
    eprosima::safedds::memory::container::StaticString256 participant_name(runtime_config_.participant_name.c_str());
    participant_qos.participant_name() = participant_name;
    participant_qos.wire_protocol_qos().announced_locator = eprosima::safedds::transport::Locator::from_ipv4(
        runtime_config_.own_ip,
        runtime_config_.participant_port);
    participant_qos.wire_protocol_qos().use_multicast_discovery = false;
    if (runtime_config_.initial_peer_locator_count > 0U)
    {
        for (std::size_t i = 0U; i < runtime_config_.initial_peer_locator_count; ++i)
        {
            initial_peers_.add(runtime_config_.initial_peer_locators[i]);
        }
    }
    else
    {
        for (std::size_t i = 0U; i < runtime_config_.initial_peer_count; ++i)
        {
            const uint16_t port = runtime_config_.initial_peer_ports[i];
            const auto& peer_ip =
                    (port >= 8011U && port <= 8013U) ?
                    runtime_config_.own_ip :
                    runtime_config_.cross_domain_peer_ip;
            initial_peers_.add(eprosima::safedds::transport::Locator::from_ipv4(peer_ip, port));
        }
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
        std::cerr << "[infotainment] Failed to create participant" << std::endl;
        return false;
    }

    return true;
}

bool InfotainmentNode::register_types()
{
    return register_type(*participant_, transit_health_type_support_, "TransitHealth") &&
           register_type(*participant_, route_metrics_type_support_, "RouteMetricSeq") &&
           register_type(*participant_, transit_metrics_type_support_, "TransitMetrics") &&
           register_type(*participant_, service_heartbeat_type_support_, "ServiceHeartbeat") &&
           register_type(*participant_, route_context_query_type_support_, "RouteContextQuery") &&
           register_type(*participant_, route_context_response_type_support_, "RouteContextResponse") &&
           register_type(*participant_, policy_decision_type_support_, "PolicyDecision") &&
           register_type(*participant_, server_availability_status_type_support_, "ServerAvailabilityStatus");
}

bool InfotainmentNode::create_topics()
{
    transit_health_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::transit_health());
    transit_health_topic_ = create_topic(*participant_, transit_health_topic_name_, transit_health_type_support_);
    if (nullptr == transit_health_topic_) { std::cerr << "[infotainment] Failed to create topic: transit_health" << std::endl; return false; }

    route_metrics_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::route_metrics());
    route_metrics_topic_ = create_topic(*participant_, route_metrics_topic_name_, route_metrics_type_support_);
    if (nullptr == route_metrics_topic_) { std::cerr << "[infotainment] Failed to create topic: route_metrics" << std::endl; return false; }

    transit_metrics_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::transit_metrics());
    transit_metrics_topic_ = create_topic(*participant_, transit_metrics_topic_name_, transit_metrics_type_support_);
    if (nullptr == transit_metrics_topic_) { std::cerr << "[infotainment] Failed to create topic: transit_metrics" << std::endl; return false; }

    service_heartbeat_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::service_heartbeat());
    service_heartbeat_topic_ = create_topic(*participant_, service_heartbeat_topic_name_, service_heartbeat_type_support_);
    if (nullptr == service_heartbeat_topic_) { std::cerr << "[infotainment] Failed to create topic: service_heartbeat" << std::endl; return false; }

    route_context_query_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::route_context_query());
    route_context_query_topic_ = create_topic(*participant_, route_context_query_topic_name_, route_context_query_type_support_);
    if (nullptr == route_context_query_topic_) { std::cerr << "[infotainment] Failed to create topic: route_context_query" << std::endl; return false; }

    route_context_response_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::route_context_response());
    route_context_response_topic_ = create_topic(*participant_, route_context_response_topic_name_, route_context_response_type_support_);
    if (nullptr == route_context_response_topic_) { std::cerr << "[infotainment] Failed to create topic: route_context_response" << std::endl; return false; }

    policy_decision_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::policy_decision());
    policy_decision_topic_ = create_topic(*participant_, policy_decision_topic_name_, policy_decision_type_support_);
    if (nullptr == policy_decision_topic_) { std::cerr << "[infotainment] Failed to create topic: policy_decision" << std::endl; return false; }

    server_availability_status_topic_name_ = eprosima::safedds::memory::container::StaticString256(common::topic_names::server_availability_status());
    server_availability_status_topic_ = create_topic(*participant_, server_availability_status_topic_name_, server_availability_status_type_support_);
    if (nullptr == server_availability_status_topic_) { std::cerr << "[infotainment] Failed to create topic: server_availability_status" << std::endl; return false; }

    return true;
}

bool InfotainmentNode::create_endpoints()
{
    eprosima::safedds::dds::PublisherQos publisher_qos{};
    publisher_ = participant_->create_publisher(
        publisher_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    if (nullptr == publisher_ )
    {
        std::cerr << "[infotainment] Failed to create publisher" << std::endl;
        return false;
    }

    eprosima::safedds::dds::SubscriberQos subscriber_qos{};
    subscriber_ = participant_->create_subscriber(
        subscriber_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);

    if (nullptr == subscriber_)
    {
        std::cerr << "[infotainment] Failed to create subscriber" << std::endl;
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

    route_context_response_datawriter_ = publisher_->create_datawriter(
        *route_context_response_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    route_context_response_writer_ = downcast_writer<safe_edge::internal::RouteContextResponseTypeSupport>(route_context_response_datawriter_);

    eprosima::safedds::dds::DataReaderQos reader_qos{};
    reader_qos.reliability().kind = eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    transit_health_datareader_ = subscriber_->create_datareader(*transit_health_topic_, reader_qos, &transit_health_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    route_metrics_datareader_ = subscriber_->create_datareader(*route_metrics_topic_, reader_qos, &route_metrics_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    transit_metrics_datareader_ = subscriber_->create_datareader(*transit_metrics_topic_, reader_qos, &transit_metrics_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    heartbeat_datareader_ = subscriber_->create_datareader(*service_heartbeat_topic_, reader_qos, &heartbeat_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    route_context_query_datareader_ = subscriber_->create_datareader(*route_context_query_topic_, reader_qos, &route_context_query_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    policy_decision_datareader_ = subscriber_->create_datareader(*policy_decision_topic_, reader_qos, &policy_decision_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);
    server_availability_status_datareader_ = subscriber_->create_datareader(*server_availability_status_topic_, reader_qos, &server_availability_listener_, eprosima::safedds::dds::DATA_AVAILABLE_STATUS);

    transit_health_reader_ = downcast_reader<safe_edge::pilot_server::TransitHealthTypeSupport>(transit_health_datareader_);
    route_metrics_reader_ = downcast_reader<safe_edge::pilot_server::RouteMetricTypeSupport>(route_metrics_datareader_);
    transit_metrics_reader_ = downcast_reader<safe_edge::pilot_server::TransitMetricsTypeSupport>(transit_metrics_datareader_);
    heartbeat_reader_ = downcast_reader<safe_edge::common::ServiceHeartbeatTypeSupport>(heartbeat_datareader_);
    route_context_query_reader_ = downcast_reader<safe_edge::internal::RouteContextQueryTypeSupport>(route_context_query_datareader_);
    policy_decision_reader_ = downcast_reader<safe_edge::internal::PolicyDecisionTypeSupport>(policy_decision_datareader_);
    server_availability_status_reader_ = downcast_reader<safe_edge::internal::ServerAvailabilityStatusTypeSupport>(server_availability_status_datareader_);

    const bool success = nullptr != service_heartbeat_writer_ &&
            nullptr != route_context_response_writer_ &&
            nullptr != transit_health_reader_ &&
            nullptr != route_metrics_reader_ &&
            nullptr != transit_metrics_reader_ &&
            nullptr != heartbeat_reader_ &&
            nullptr != route_context_query_reader_ &&
            nullptr != policy_decision_reader_ &&
            nullptr != server_availability_status_reader_;

    if (!success)
    {
        std::cerr << "[infotainment] Failed to create endpoints" << std::endl;
    }

    return success;
}

bool InfotainmentNode::enable_entities()
{
    bool enabled = true;
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == publisher_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == service_heartbeat_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == route_context_response_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == subscriber_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == transit_health_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == route_metrics_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == transit_metrics_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == heartbeat_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == route_context_query_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == policy_decision_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == server_availability_status_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == participant_->enable());

    if (!enabled)
    {
        std::cerr << "[infotainment] Failed to enable DDS entities" << std::endl;
    }

    return enabled;
}

bool InfotainmentNode::create_executor()
{
    executor_ = factory_.create_default_executor();

    if (nullptr == executor_)
    {
        std::cerr << "[infotainment] Failed to create executor" << std::endl;
        return false;
    }

    return true;
}

void InfotainmentNode::start_timers() noexcept
{
    heartbeat_timer_.start();
    status_write_timer_.start();
}

void InfotainmentNode::on_transit_health_received(
        const safe_edge::pilot_server::TransitHealth& health)
{
    cached_transit_health_ = health;
    have_transit_health_ = true;
    std::cout << "[infotainment] Received TransitHealth status=" << health.status
              << " last_fetch_ts=" << health.last_fetch_ts << std::endl;
}

void InfotainmentNode::on_route_metrics_received(
        const safe_edge::pilot_server::RouteMetricSeq& metrics)
{
    cached_route_metric_count_ = 0;
    for (uint32_t i = 0; i < metrics.size() && cached_route_metric_count_ < 512; ++i)
    {
        cached_route_metrics_[cached_route_metric_count_++] = metrics[i];
    }
    std::cout << "[infotainment] Received RouteMetricSeq count=" << metrics.size() << std::endl;
}

void InfotainmentNode::on_transit_metrics_received(
        const safe_edge::pilot_server::TransitMetrics& metrics)
{
    cached_vehicles_seen_ = metrics.vehicles_seen;
    have_transit_metrics_ = true;
    std::cout << "[infotainment] Received TransitMetrics routes=" << metrics.by_route.size()
              << " vehicles_seen=" << metrics.vehicles_seen << std::endl;
}

void InfotainmentNode::on_route_context_query_received(
        const safe_edge::internal::RouteContextQuery& query)
{
    std::cout << "[infotainment] Received RouteContextQuery vehicle_health="
              << static_cast<int32_t>(query.vehicle_health) << std::endl;
    publish_route_context_response(query);
}

void InfotainmentNode::on_policy_decision_received(
        const safe_edge::internal::PolicyDecision& decision)
{
    cached_policy_mode_ = decision.mode;
    std::strncpy(cached_policy_reason_, decision.reason.c_str(), sizeof(cached_policy_reason_) - 1U);
    cached_policy_reason_[sizeof(cached_policy_reason_) - 1U] = '\0';
    cached_allow_non_safety_ = decision.allow_non_safety;
    have_policy_ = true;
    std::cout << "[infotainment] PolicyDecision mode=" << policy_mode_to_str(decision.mode)
              << " reason=" << decision.reason << std::endl;
}

void InfotainmentNode::on_server_availability_received(
        const safe_edge::internal::ServerAvailabilityStatus& status)
{
    cached_server_available_ = status.server_available;
    std::strncpy(cached_server_detail_, status.detail.c_str(), sizeof(cached_server_detail_) - 1U);
    cached_server_detail_[sizeof(cached_server_detail_) - 1U] = '\0';
    have_server_status_ = true;
    last_server_status_ms_ = monotonic_ms();
    std::cout << "[infotainment] ServerAvailabilityStatus available=" << status.server_available
              << " detail=" << status.detail << std::endl;
}

void InfotainmentNode::update_liveness(const char* service_name) noexcept
{
    const uint64_t now = monotonic_ms();
    for (size_t i = 0U; i < liveness_count_; ++i)
    {
        if (std::strncmp(liveness_[i].name, service_name, sizeof(liveness_[i].name) - 1U) == 0)
        {
            liveness_[i].last_seen_ms = now;
            return;
        }
    }
    if (liveness_count_ < MAX_LIVENESS_ENTRIES)
    {
        std::strncpy(liveness_[liveness_count_].name, service_name, sizeof(liveness_[0].name) - 1U);
        liveness_[liveness_count_].name[sizeof(liveness_[0].name) - 1U] = '\0';
        liveness_[liveness_count_].last_seen_ms = now;
        ++liveness_count_;
    }
}

bool InfotainmentNode::is_alive(const char* service_name) const noexcept
{
    const uint64_t now = monotonic_ms();
    for (size_t i = 0U; i < liveness_count_; ++i)
    {
        if (std::strncmp(liveness_[i].name, service_name, sizeof(liveness_[i].name) - 1U) == 0)
        {
            return (now - liveness_[i].last_seen_ms) < LIVENESS_THRESHOLD_MS;
        }
    }
    return false;
}

void InfotainmentNode::on_peer_heartbeat_received(
        const safe_edge::common::ServiceHeartbeat& heartbeat)
{
    if (heartbeat.service_name == runtime_config_.service_name)
    {
        return;
    }
    update_liveness(heartbeat.service_name.c_str());
    std::cout << "[infotainment] Heartbeat service=" << heartbeat.service_name
              << " status=" << health_status_to_text(heartbeat.status) << std::endl;
}

// ---------------------------------------------------------------------------
// Status file write
// ---------------------------------------------------------------------------

const char* InfotainmentNode::policy_mode_to_str(safe_edge::common::PolicyMode mode) const noexcept
{
    switch (mode)
    {
        case safe_edge::common::PolicyMode::POLICY_NOMINAL:             return "POLICY_NOMINAL";
        case safe_edge::common::PolicyMode::POLICY_LOW_SOC:             return "POLICY_LOW_SOC";
        case safe_edge::common::PolicyMode::POLICY_EDGE_AUTONOMOUS:     return "POLICY_EDGE_AUTONOMOUS";
        case safe_edge::common::PolicyMode::POLICY_DEGRADED_SERVER_DOWN: return "POLICY_DEGRADED_SERVER_DOWN";
        case safe_edge::common::PolicyMode::POLICY_DEGRADED_EDGE_DOWN:  return "POLICY_DEGRADED_EDGE_DOWN";
        case safe_edge::common::PolicyMode::POLICY_DEGRADED_COMPLETE:   return "POLICY_DEGRADED_COMPLETE";
        default:                                                         return "POLICY_UNKNOWN";
    }
}

void InfotainmentNode::write_status_file() noexcept
{
    const bool server_ok = have_server_status_ && cached_server_available_ &&
                           (monotonic_ms() - last_server_status_ms_) < SERVER_STATUS_TIMEOUT_MS;
    const bool safety_ok = is_alive("policy_engine");
    const bool edge_ok   = is_alive("edge_gateway");

    const bool nominal = (cached_policy_mode_ == safe_edge::common::PolicyMode::POLICY_NOMINAL)
                      && server_ok && safety_ok;
    const char* global_status = nominal ? "nominal" : "degraded";

    char buf[4096];
    int pos = 0;

    // Escape helper lambda equivalent — inline, since no lambdas in no-exception C++14 with strict warnings
    // We only emit ASCII strings from controlled sources, so no escaping needed

    pos += std::snprintf(buf + pos, static_cast<size_t>(sizeof(buf) - pos),
        "{\n"
        "  \"timestamp_ms\": %llu,\n"
        "  \"global_status\": \"%s\",\n"
        "  \"policy\": {\n"
        "    \"mode\": \"%s\",\n"
        "    \"reason\": \"%s\",\n"
        "    \"allow_non_safety\": %s\n"
        "  },\n"
        "  \"server\": {\n"
        "    \"available\": %s,\n"
        "    \"detail\": \"%s\"\n"
        "  },\n",
        static_cast<unsigned long long>(realtime_ms()),
        global_status,
        policy_mode_to_str(cached_policy_mode_),
        cached_policy_reason_,
        cached_allow_non_safety_ ? "true" : "false",
        server_ok ? "true" : "false",
        have_server_status_ ? cached_server_detail_ : "unknown"
    );

    // Nodes
    struct NodeEntry { const char* name; bool self; };
    const NodeEntry nodes[] = {
        { "vehicle_mock",       false },
        { "policy_engine",      false },
        { "safety_io_adapters", false },
        { "cloud_gateway",      false },
        { "ota_service",        false },
        { "infotainment",       true  }
    };
    const size_t node_count = sizeof(nodes) / sizeof(nodes[0]);

    pos += std::snprintf(buf + pos, static_cast<size_t>(sizeof(buf) - pos),
        "  \"nodes\": [\n");
    for (size_t i = 0U; i < node_count; ++i)
    {
        const bool alive = nodes[i].self ? true : is_alive(nodes[i].name);
        const char* comma = (i + 1U < node_count) ? "," : "";
        pos += std::snprintf(buf + pos, static_cast<size_t>(sizeof(buf) - pos),
            "    { \"name\": \"%s\", \"alive\": %s }%s\n",
            nodes[i].name, alive ? "true" : "false", comma);
    }
    pos += std::snprintf(buf + pos, static_cast<size_t>(sizeof(buf) - pos),
        "  ],\n");

    // Communications
    pos += std::snprintf(buf + pos, static_cast<size_t>(sizeof(buf) - pos),
        "  \"communications\": [\n"
        "    { \"name\": \"server_link\",  \"ok\": %s, \"detail\": \"%s\" },\n"
        "    { \"name\": \"edge_link\",    \"ok\": %s, \"detail\": \"%s\" },\n"
        "    { \"name\": \"safety_link\",  \"ok\": %s, \"detail\": \"%s\" }\n"
        "  ]\n"
        "}\n",
        server_ok ? "true" : "false",
        have_server_status_ ? cached_server_detail_ : "not observed",
        edge_ok ? "true" : "false",
        edge_ok ? "edge_gateway alive" : "no heartbeat from edge_gateway",
        safety_ok ? "true" : "false",
        safety_ok ? "policy_engine alive" : "no heartbeat from policy_engine"
    );

    if (pos <= 0 || pos >= static_cast<int>(sizeof(buf)))
    {
        std::cerr << "[infotainment] Status buffer overflow" << std::endl;
        return;
    }

    // Atomic write: tmp → rename
    char tmp_path[520];
    std::snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", status_file_path_);

    std::FILE* f = std::fopen(tmp_path, "w");
    if (nullptr == f)
    {
        std::cerr << "[infotainment] Cannot open status tmp file: " << tmp_path << std::endl;
        return;
    }
    std::fwrite(buf, 1U, static_cast<size_t>(pos), f);
    std::fclose(f);

    if (std::rename(tmp_path, status_file_path_) != 0)
    {
        std::cerr << "[infotainment] Failed to rename status file" << std::endl;
    }
}

void InfotainmentNode::publish_route_context_response(
        const safe_edge::internal::RouteContextQuery& query)
{
    safe_edge::internal::RouteContextResponse response{};
    response.header = header_factory_.make_header("route_context_response");
    response.has_data = have_transit_health_ || have_transit_metrics_;
    response.transit_status = have_transit_health_ ? cached_transit_health_.status : "";
    response.vehicles_seen = cached_vehicles_seen_;

    response.max_route_updates = 0;
    for (int32_t i = 0; i < cached_route_metric_count_; ++i)
    {
        if (cached_route_metrics_[i].updates_count > response.max_route_updates)
        {
            response.max_route_updates = cached_route_metrics_[i].updates_count;
        }
    }

    (void)query;

    if (eprosima::safedds::dds::ReturnCode::OK !=
            route_context_response_writer_->write(response, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[infotainment] Failed to publish RouteContextResponse" << std::endl;
        return;
    }

    std::cout << "[infotainment] Published RouteContextResponse"
              << " transit_status=" << response.transit_status
              << " max_route_updates=" << response.max_route_updates
              << " vehicles_seen=" << response.vehicles_seen << std::endl;
}

void InfotainmentNode::publish_heartbeat()
{
    safe_edge::common::ServiceHeartbeat heartbeat;
    heartbeat.header_st = header_factory_.make_header("service_heartbeat");
    heartbeat.service_name = runtime_config_.service_name;
    heartbeat.status = safe_edge::common::HealthStatus::HEALTH_OK;
    heartbeat.detail = "running";

    if (eprosima::safedds::dds::ReturnCode::OK != service_heartbeat_writer_->write(heartbeat, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[infotainment] Failed to publish ServiceHeartbeat" << std::endl;
        return;
    }

    std::cout << "[infotainment] Published ServiceHeartbeat" << std::endl;
}

void InfotainmentNode::log_subscription_match(
        const char* topic_name,
        int32_t total_count) const
{
    std::cout << "[infotainment] Subscription matched on " << topic_name
              << " total_count=" << total_count << std::endl;
}

void InfotainmentNode::log_publication_match(
        const char* topic_name,
        int32_t total_count) const
{
    std::cout << "[infotainment] Publication matched on " << topic_name
              << " total_count=" << total_count << std::endl;
}

eprosima::safedds::execution::TimePoint InfotainmentNode::next_wakeup_time() const noexcept
{
    eprosima::safedds::execution::TimePoint next = executor_->get_next_work_timepoint();
    next = eprosima::safedds::execution::TimePoint::min(next, heartbeat_timer_.next_trigger());
    next = eprosima::safedds::execution::TimePoint::min(next, status_write_timer_.next_trigger());
    return next;
}

} // namespace nodes
} // namespace non_safety_domain
} // namespace safe_edge
