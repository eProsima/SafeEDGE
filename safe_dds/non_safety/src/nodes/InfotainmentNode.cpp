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

#include <iostream>

namespace safe_edge {
namespace non_safety_domain {
namespace nodes {

namespace {

constexpr eprosima::safedds::execution::TimePeriod TIMEOUT = {5, 0};

const char* health_status_to_text(
        safe_edge::common::HealthStatus status) noexcept
{
    switch (status)
    {
        case safe_edge::common::HealthStatus::HEALTH_OK:
            return "HEALTH_OK";
        case safe_edge::common::HealthStatus::HEALTH_DEGRADED:
            return "HEALTH_DEGRADED";
        case safe_edge::common::HealthStatus::HEALTH_ERROR:
            return "HEALTH_ERROR";
        case safe_edge::common::HealthStatus::HEALTH_UNKNOWN:
            return "HEALTH_UNKNOWN";
        default:
            return "UNKNOWN";
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

InfotainmentNode::InfotainmentNode(
        const common::RuntimeConfig& runtime_config)
    : runtime_config_(runtime_config)
    , header_factory_(runtime_config.source_name)
    , participant_listener_(*this)
    , transit_health_listener_(*this)
    , route_metrics_listener_(*this)
    , transit_metrics_listener_(*this)
    , heartbeat_listener_(*this)
    , route_context_query_listener_(*this)
    , heartbeat_timer_(TIMEOUT)
{
}

int InfotainmentNode::run()
{
    if (!initialize())
    {
        return 1;
    }

    start_timers();
    std::cout << "[infotainment] [START] Running with participant port " << runtime_config_.participant_port << std::endl;

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
        {127, 0, 0, 1},
        runtime_config_.participant_port);
    for (std::size_t i = 0U; i < runtime_config_.initial_peer_count; ++i)
    {
        initial_peers_.add(eprosima::safedds::transport::Locator::from_ipv4({127, 0, 0, 1}, runtime_config_.initial_peer_ports[i]));
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
           register_type(*participant_, route_context_response_type_support_, "RouteContextResponse");
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

    transit_health_reader_ = downcast_reader<safe_edge::pilot_server::TransitHealthTypeSupport>(transit_health_datareader_);
    route_metrics_reader_ = downcast_reader<safe_edge::pilot_server::RouteMetricTypeSupport>(route_metrics_datareader_);
    transit_metrics_reader_ = downcast_reader<safe_edge::pilot_server::TransitMetricsTypeSupport>(transit_metrics_datareader_);
    heartbeat_reader_ = downcast_reader<safe_edge::common::ServiceHeartbeatTypeSupport>(heartbeat_datareader_);
    route_context_query_reader_ = downcast_reader<safe_edge::internal::RouteContextQueryTypeSupport>(route_context_query_datareader_);

    const bool success = nullptr != service_heartbeat_writer_ &&
            nullptr != route_context_response_writer_ &&
            nullptr != transit_health_reader_ &&
            nullptr != route_metrics_reader_ &&
            nullptr != transit_metrics_reader_ &&
            nullptr != heartbeat_reader_ &&
            nullptr != route_context_query_reader_;

    if (!success)
    {
        std::cerr << "[infotainment] Failed to create endpoints" << std::endl;
        return false;
    }

    return true;
}

bool InfotainmentNode::enable_entities()
{
    bool enabled = true;
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == publisher_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == service_heartbeat_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == subscriber_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == transit_health_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == route_metrics_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == transit_metrics_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == heartbeat_datareader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == route_context_response_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == route_context_query_datareader_->enable());
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

void InfotainmentNode::on_peer_heartbeat_received(
        const safe_edge::common::ServiceHeartbeat& heartbeat)
{
    if (heartbeat.service_name == runtime_config_.service_name)
    {
        return;
    }

    std::cout << "[infotainment] Received ServiceHeartbeat service="
              << heartbeat.service_name
              << " status=" << health_status_to_text(heartbeat.status)
              << " detail=" << heartbeat.detail << std::endl;
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
    return next;
}

} // namespace nodes
} // namespace non_safety_domain
} // namespace safe_edge
