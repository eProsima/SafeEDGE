#ifndef SAFE_EDGE_NON_SAFETY_DOMAIN_NODES_INFOTAINMENTNODE_HPP
#define SAFE_EDGE_NON_SAFETY_DOMAIN_NODES_INFOTAINMENTNODE_HPP

#include <safe_edge/non_safety_domain/common/HeaderFactory.hpp>
#include <safe_edge/non_safety_domain/common/RuntimeConfig.hpp>

#include <internal.hpp>
#include <pilot_server.hpp>

#include <safedds/dds/DataReader.hpp>
#include <safedds/dds/DataReaderListener.hpp>
#include <safedds/dds/DataWriter.hpp>
#include <safedds/dds/DomainParticipant.hpp>
#include <safedds/dds/DomainParticipantFactory.hpp>
#include <safedds/dds/DomainParticipantListener.hpp>
#include <safedds/dds/Publisher.hpp>
#include <safedds/dds/Subscriber.hpp>
#include <safedds/dds/Topic.hpp>
#include <safedds/dds/TypedDataReader.hpp>
#include <safedds/dds/TypedDataWriter.hpp>
#include <safedds/execution/Timer.hpp>
#include <safedds/memory/container/StaticList.hpp>
#include <safedds/memory/container/StaticString.hpp>
#include <safedds/transport/Locator.hpp>

#include <cstdint>

namespace safe_edge {
namespace non_safety_domain {
namespace nodes {

class InfotainmentNode
{
public:

    explicit InfotainmentNode(const common::RuntimeConfig& runtime_config);

    int run();

private:

    class ParticipantListener :
        public eprosima::safedds::dds::DomainParticipantListener
    {
    public:

        explicit ParticipantListener(InfotainmentNode& owner);

        void on_subscription_matched(
                eprosima::safedds::dds::DataReader& reader,
                const eprosima::safedds::dds::SubscriptionMatchedStatus& info) noexcept override;

        void on_publication_matched(
                eprosima::safedds::dds::DataWriter& writer,
                const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept override;

    private:

        InfotainmentNode& owner_;
    };

    class TransitHealthListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit TransitHealthListener(InfotainmentNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        InfotainmentNode& owner_;
    };

    class RouteMetricsListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit RouteMetricsListener(InfotainmentNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        InfotainmentNode& owner_;
    };

    class TransitMetricsListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit TransitMetricsListener(InfotainmentNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        InfotainmentNode& owner_;
    };

    class HeartbeatListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit HeartbeatListener(InfotainmentNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        InfotainmentNode& owner_;
    };

    class RouteContextQueryListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit RouteContextQueryListener(InfotainmentNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        InfotainmentNode& owner_;
    };

    bool initialize();
    bool create_participant();
    bool register_types();
    bool create_topics();
    bool create_endpoints();
    bool enable_entities();
    bool create_executor();
    void start_timers() noexcept;

    void on_transit_health_received(const safe_edge::pilot_server::TransitHealth& health);
    void on_route_metrics_received(const safe_edge::pilot_server::RouteMetricSeq& metrics);
    void on_transit_metrics_received(const safe_edge::pilot_server::TransitMetrics& metrics);
    void publish_heartbeat();
    void on_peer_heartbeat_received(const safe_edge::common::ServiceHeartbeat& heartbeat);
    void on_route_context_query_received(const safe_edge::internal::RouteContextQuery& query);
    void publish_route_context_response(const safe_edge::internal::RouteContextQuery& query);
    void log_subscription_match(const char* topic_name, int32_t total_count) const;
    void log_publication_match(const char* topic_name, int32_t total_count) const;
    eprosima::safedds::execution::TimePoint next_wakeup_time() const noexcept;

    eprosima::safedds::dds::DomainParticipantFactory factory_;
    common::RuntimeConfig runtime_config_;
    common::HeaderFactory header_factory_;

    ParticipantListener participant_listener_;
    TransitHealthListener transit_health_listener_;
    RouteMetricsListener route_metrics_listener_;
    TransitMetricsListener transit_metrics_listener_;
    HeartbeatListener heartbeat_listener_;
    RouteContextQueryListener route_context_query_listener_;

    safe_edge::pilot_server::TransitHealthTypeSupport transit_health_type_support_;
    safe_edge::pilot_server::RouteMetricTypeSupport route_metrics_type_support_;
    safe_edge::pilot_server::TransitMetricsTypeSupport transit_metrics_type_support_;
    safe_edge::common::ServiceHeartbeatTypeSupport service_heartbeat_type_support_;
    safe_edge::internal::RouteContextQueryTypeSupport route_context_query_type_support_;
    safe_edge::internal::RouteContextResponseTypeSupport route_context_response_type_support_;

    eprosima::safedds::dds::DomainParticipant* participant_ = nullptr;
    eprosima::safedds::memory::container::StaticList<eprosima::safedds::transport::Locator, 4U> initial_peers_;
    eprosima::safedds::dds::Publisher* publisher_ = nullptr;
    eprosima::safedds::dds::Subscriber* subscriber_ = nullptr;
    eprosima::safedds::execution::ISpinnable* executor_ = nullptr;

    eprosima::safedds::dds::Topic* transit_health_topic_ = nullptr;
    eprosima::safedds::dds::Topic* route_metrics_topic_ = nullptr;
    eprosima::safedds::dds::Topic* transit_metrics_topic_ = nullptr;
    eprosima::safedds::dds::Topic* service_heartbeat_topic_ = nullptr;
    eprosima::safedds::dds::Topic* route_context_query_topic_ = nullptr;
    eprosima::safedds::dds::Topic* route_context_response_topic_ = nullptr;

    eprosima::safedds::memory::container::StaticString256 transit_health_topic_name_;
    eprosima::safedds::memory::container::StaticString256 route_metrics_topic_name_;
    eprosima::safedds::memory::container::StaticString256 transit_metrics_topic_name_;
    eprosima::safedds::memory::container::StaticString256 service_heartbeat_topic_name_;
    eprosima::safedds::memory::container::StaticString256 route_context_query_topic_name_;
    eprosima::safedds::memory::container::StaticString256 route_context_response_topic_name_;

    eprosima::safedds::dds::DataWriter* service_heartbeat_datawriter_ = nullptr;
    eprosima::safedds::dds::TypedDataWriter<safe_edge::common::ServiceHeartbeatTypeSupport>* service_heartbeat_writer_ =
            nullptr;

    eprosima::safedds::dds::DataWriter* route_context_response_datawriter_ = nullptr;
    eprosima::safedds::dds::TypedDataWriter<safe_edge::internal::RouteContextResponseTypeSupport>* route_context_response_writer_ =
            nullptr;

    eprosima::safedds::dds::DataReader* transit_health_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* route_metrics_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* transit_metrics_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* heartbeat_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* route_context_query_datareader_ = nullptr;

    eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::TransitHealthTypeSupport>* transit_health_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::RouteMetricTypeSupport>* route_metrics_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::TransitMetricsTypeSupport>* transit_metrics_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::common::ServiceHeartbeatTypeSupport>* heartbeat_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::internal::RouteContextQueryTypeSupport>* route_context_query_reader_ =
            nullptr;

    eprosima::safedds::execution::Timer heartbeat_timer_;

    safe_edge::pilot_server::TransitHealth cached_transit_health_{};
    bool have_transit_health_ = false;

    safe_edge::pilot_server::RouteMetric cached_route_metrics_[512];
    int32_t cached_route_metric_count_ = 0;

    int32_t cached_vehicles_seen_ = 0;
    bool have_transit_metrics_ = false;
};

} // namespace nodes
} // namespace non_safety_domain
} // namespace safe_edge

#endif // SAFE_EDGE_NON_SAFETY_DOMAIN_NODES_INFOTAINMENTNODE_HPP
