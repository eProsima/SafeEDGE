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

    class PolicyDecisionListener : public eprosima::safedds::dds::DataReaderListener
    {
    public:
        explicit PolicyDecisionListener(InfotainmentNode& owner);
        void on_data_available(eprosima::safedds::dds::DataReader& reader) noexcept override;
    private:
        InfotainmentNode& owner_;
    };

    class ServerAvailabilityListener : public eprosima::safedds::dds::DataReaderListener
    {
    public:
        explicit ServerAvailabilityListener(InfotainmentNode& owner);
        void on_data_available(eprosima::safedds::dds::DataReader& reader) noexcept override;
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
    void on_policy_decision_received(const safe_edge::internal::PolicyDecision& decision);
    void on_server_availability_received(const safe_edge::internal::ServerAvailabilityStatus& status);
    void update_liveness(const char* service_name) noexcept;
    bool is_alive(const char* service_name) const noexcept;
    void write_status_file() noexcept;
    const char* policy_mode_to_str(safe_edge::common::PolicyMode mode) const noexcept;
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
    PolicyDecisionListener policy_decision_listener_;
    ServerAvailabilityListener server_availability_listener_;

    safe_edge::pilot_server::TransitHealthTypeSupport transit_health_type_support_;
    safe_edge::pilot_server::RouteMetricTypeSupport route_metrics_type_support_;
    safe_edge::pilot_server::TransitMetricsTypeSupport transit_metrics_type_support_;
    safe_edge::common::ServiceHeartbeatTypeSupport service_heartbeat_type_support_;
    safe_edge::internal::RouteContextQueryTypeSupport route_context_query_type_support_;
    safe_edge::internal::RouteContextResponseTypeSupport route_context_response_type_support_;
    safe_edge::internal::PolicyDecisionTypeSupport policy_decision_type_support_;
    safe_edge::internal::ServerAvailabilityStatusTypeSupport server_availability_status_type_support_;

    eprosima::safedds::dds::DomainParticipant* participant_ = nullptr;
    eprosima::safedds::memory::container::StaticList<eprosima::safedds::transport::Locator, 8U> initial_peers_;
    eprosima::safedds::dds::Publisher* publisher_ = nullptr;
    eprosima::safedds::dds::Subscriber* subscriber_ = nullptr;
    eprosima::safedds::execution::ISpinnable* executor_ = nullptr;

    eprosima::safedds::dds::Topic* transit_health_topic_ = nullptr;
    eprosima::safedds::dds::Topic* route_metrics_topic_ = nullptr;
    eprosima::safedds::dds::Topic* transit_metrics_topic_ = nullptr;
    eprosima::safedds::dds::Topic* service_heartbeat_topic_ = nullptr;
    eprosima::safedds::dds::Topic* route_context_query_topic_ = nullptr;
    eprosima::safedds::dds::Topic* route_context_response_topic_ = nullptr;
    eprosima::safedds::dds::Topic* policy_decision_topic_ = nullptr;
    eprosima::safedds::dds::Topic* server_availability_status_topic_ = nullptr;

    eprosima::safedds::memory::container::StaticString256 transit_health_topic_name_;
    eprosima::safedds::memory::container::StaticString256 route_metrics_topic_name_;
    eprosima::safedds::memory::container::StaticString256 transit_metrics_topic_name_;
    eprosima::safedds::memory::container::StaticString256 service_heartbeat_topic_name_;
    eprosima::safedds::memory::container::StaticString256 route_context_query_topic_name_;
    eprosima::safedds::memory::container::StaticString256 route_context_response_topic_name_;
    eprosima::safedds::memory::container::StaticString256 policy_decision_topic_name_;
    eprosima::safedds::memory::container::StaticString256 server_availability_status_topic_name_;

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
    eprosima::safedds::dds::DataReader* policy_decision_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* server_availability_status_datareader_ = nullptr;

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
    eprosima::safedds::dds::TypedDataReader<safe_edge::internal::PolicyDecisionTypeSupport>* policy_decision_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::internal::ServerAvailabilityStatusTypeSupport>* server_availability_status_reader_ =
            nullptr;

    eprosima::safedds::execution::Timer heartbeat_timer_;
    eprosima::safedds::execution::Timer status_write_timer_;

    // Cached transit/route state (existing)
    safe_edge::pilot_server::TransitHealth cached_transit_health_{};
    bool have_transit_health_ = false;

    safe_edge::pilot_server::RouteMetric cached_route_metrics_[512];
    int32_t cached_route_metric_count_ = 0;

    int32_t cached_vehicles_seen_ = 0;
    bool have_transit_metrics_ = false;

    safe_edge::common::PolicyMode cached_policy_mode_ = safe_edge::common::PolicyMode::POLICY_UNKNOWN;
    char cached_policy_reason_[256] = {};
    bool cached_allow_non_safety_ = false;
    bool have_policy_ = false;

    bool cached_server_available_ = false;
    char cached_server_detail_[256] = {};
    bool have_server_status_ = false;
    uint64_t last_server_status_ms_ = 0U;

    struct LivenessEntry
    {
        char name[64];
        uint64_t last_seen_ms;
    };
    LivenessEntry liveness_[16];
    size_t liveness_count_ = 0;

    char status_file_path_[512] = {};
};

} // namespace nodes
} // namespace non_safety_domain
} // namespace safe_edge

#endif // SAFE_EDGE_NON_SAFETY_DOMAIN_NODES_INFOTAINMENTNODE_HPP
