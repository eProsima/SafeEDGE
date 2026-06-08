#ifndef SAFE_EDGE_NON_SAFETY_DOMAIN_NODES_CLOUDGATEWAYNODE_HPP
#define SAFE_EDGE_NON_SAFETY_DOMAIN_NODES_CLOUDGATEWAYNODE_HPP

#include <safe_edge/non_safety_domain/common/HeaderFactory.hpp>
#include <safe_edge/non_safety_domain/common/RuntimeConfig.hpp>

#include <common.hpp>
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

class CloudGatewayNode
{
public:

    explicit CloudGatewayNode(const common::RuntimeConfig& runtime_config);

    int run();

private:

    class ParticipantListener :
        public eprosima::safedds::dds::DomainParticipantListener
    {
    public:

        explicit ParticipantListener(CloudGatewayNode& owner);

        void on_subscription_matched(
                eprosima::safedds::dds::DataReader& reader,
                const eprosima::safedds::dds::SubscriptionMatchedStatus& info) noexcept override;

        void on_publication_matched(
                eprosima::safedds::dds::DataWriter& writer,
                const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept override;

    private:

        CloudGatewayNode& owner_;
    };

    class ChargerLocationsListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit ChargerLocationsListener(CloudGatewayNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        CloudGatewayNode& owner_;
    };

    class ChargerTypesListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit ChargerTypesListener(CloudGatewayNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        CloudGatewayNode& owner_;
    };

    class ChargingSessionsListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit ChargingSessionsListener(CloudGatewayNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        CloudGatewayNode& owner_;
    };

    class TransitHealthListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit TransitHealthListener(CloudGatewayNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        CloudGatewayNode& owner_;
    };

    class RouteMetricsListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit RouteMetricsListener(CloudGatewayNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        CloudGatewayNode& owner_;
    };

    class TransitMetricsListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit TransitMetricsListener(CloudGatewayNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        CloudGatewayNode& owner_;
    };

    class HeartbeatListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit HeartbeatListener(CloudGatewayNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        CloudGatewayNode& owner_;
    };

    class ChargingQueryListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit ChargingQueryListener(CloudGatewayNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        CloudGatewayNode& owner_;
    };

    bool initialize();
    bool create_participant();
    bool register_types();
    bool create_topics();
    bool create_endpoints();
    bool enable_entities();
    bool create_executor();
    void start_timers() noexcept;

    void on_charger_locations_received(const safe_edge::pilot_server::ChargerLocationSeq& locations);
    void on_charger_types_received(const safe_edge::pilot_server::ChargerTypeSeq& types);
    void on_charging_sessions_received(const safe_edge::pilot_server::ChargingSessionSeq& sessions);
    void on_transit_health_received(const safe_edge::pilot_server::TransitHealth& health);
    void on_route_metrics_received(const safe_edge::pilot_server::RouteMetricSeq& metrics);
    void on_transit_metrics_received(const safe_edge::pilot_server::TransitMetrics& metrics);
    void publish_heartbeat();
    void on_peer_heartbeat_received(const safe_edge::common::ServiceHeartbeat& heartbeat);
    void on_charging_query_received(const safe_edge::internal::ChargingQuery& query);
    void publish_charging_response();
    void publish_server_availability_status(bool server_available, const char* detail);

    void log_subscription_match(const char* topic_name, int32_t total_count) const;
    void log_publication_match(const char* topic_name, int32_t total_count) const;
    eprosima::safedds::execution::TimePoint next_wakeup_time() const noexcept;

    eprosima::safedds::dds::DomainParticipantFactory factory_;
    common::RuntimeConfig runtime_config_;
    common::HeaderFactory header_factory_;

    ParticipantListener participant_listener_;
    ChargerLocationsListener charger_locations_listener_;
    ChargerTypesListener charger_types_listener_;
    ChargingSessionsListener charging_sessions_listener_;
    TransitHealthListener transit_health_listener_;
    RouteMetricsListener route_metrics_listener_;
    TransitMetricsListener transit_metrics_listener_;
    HeartbeatListener heartbeat_listener_;
    ChargingQueryListener charging_query_listener_;

    safe_edge::pilot_server::ChargerLocationTypeSupport charger_locations_type_support_;
    safe_edge::pilot_server::ChargerTypeTypeSupport charger_types_type_support_;
    safe_edge::pilot_server::ChargingSessionTypeSupport charging_sessions_type_support_;
    safe_edge::pilot_server::TransitHealthTypeSupport transit_health_type_support_;
    safe_edge::pilot_server::RouteMetricTypeSupport route_metrics_type_support_;
    safe_edge::pilot_server::TransitMetricsTypeSupport transit_metrics_type_support_;
    safe_edge::common::ServiceHeartbeatTypeSupport service_heartbeat_type_support_;
    safe_edge::internal::ChargingQueryTypeSupport charging_query_type_support_;
    safe_edge::internal::ChargingResponseTypeSupport charging_response_type_support_;
    safe_edge::pilot_server::ServerQueryTypeSupport server_query_type_support_;
    safe_edge::internal::ServerAvailabilityStatusTypeSupport server_availability_status_type_support_;

    eprosima::safedds::dds::DomainParticipant* participant_ = nullptr;
    eprosima::safedds::memory::container::StaticList<eprosima::safedds::transport::Locator, 4U> initial_peers_;
    eprosima::safedds::dds::Publisher* publisher_ = nullptr;
    eprosima::safedds::dds::Subscriber* subscriber_ = nullptr;
    eprosima::safedds::execution::ISpinnable* executor_ = nullptr;

    eprosima::safedds::dds::Topic* charger_locations_topic_ = nullptr;
    eprosima::safedds::dds::Topic* charger_types_topic_ = nullptr;
    eprosima::safedds::dds::Topic* charging_sessions_topic_ = nullptr;
    eprosima::safedds::dds::Topic* transit_health_topic_ = nullptr;
    eprosima::safedds::dds::Topic* route_metrics_topic_ = nullptr;
    eprosima::safedds::dds::Topic* transit_metrics_topic_ = nullptr;
    eprosima::safedds::dds::Topic* service_heartbeat_topic_ = nullptr;
    eprosima::safedds::dds::Topic* charging_query_topic_ = nullptr;
    eprosima::safedds::dds::Topic* charging_response_topic_ = nullptr;
    eprosima::safedds::dds::Topic* server_query_topic_ = nullptr;
    eprosima::safedds::dds::Topic* server_availability_status_topic_ = nullptr;

    eprosima::safedds::memory::container::StaticString256 charger_locations_topic_name_;
    eprosima::safedds::memory::container::StaticString256 charger_types_topic_name_;
    eprosima::safedds::memory::container::StaticString256 charging_sessions_topic_name_;
    eprosima::safedds::memory::container::StaticString256 transit_health_topic_name_;
    eprosima::safedds::memory::container::StaticString256 route_metrics_topic_name_;
    eprosima::safedds::memory::container::StaticString256 transit_metrics_topic_name_;
    eprosima::safedds::memory::container::StaticString256 service_heartbeat_topic_name_;
    eprosima::safedds::memory::container::StaticString256 charging_query_topic_name_;
    eprosima::safedds::memory::container::StaticString256 charging_response_topic_name_;
    eprosima::safedds::memory::container::StaticString256 server_query_topic_name_;
    eprosima::safedds::memory::container::StaticString256 server_availability_status_topic_name_;

    eprosima::safedds::dds::DataWriter* service_heartbeat_datawriter_ = nullptr;
    eprosima::safedds::dds::DataWriter* charging_response_datawriter_ = nullptr;
    eprosima::safedds::dds::DataWriter* server_query_datawriter_ = nullptr;
    eprosima::safedds::dds::DataWriter* server_availability_status_datawriter_ = nullptr;

    eprosima::safedds::dds::TypedDataWriter<safe_edge::common::ServiceHeartbeatTypeSupport>* service_heartbeat_writer_ =
            nullptr;
    eprosima::safedds::dds::TypedDataWriter<safe_edge::internal::ChargingResponseTypeSupport>* charging_response_writer_ =
            nullptr;
    eprosima::safedds::dds::TypedDataWriter<safe_edge::pilot_server::ServerQueryTypeSupport>* server_query_writer_ =
            nullptr;
    eprosima::safedds::dds::TypedDataWriter<safe_edge::internal::ServerAvailabilityStatusTypeSupport>* server_availability_status_writer_ =
            nullptr;

    eprosima::safedds::dds::DataReader* charger_locations_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* charger_types_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* charging_sessions_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* transit_health_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* route_metrics_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* transit_metrics_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* heartbeat_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* charging_query_datareader_ = nullptr;

    eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::ChargerLocationTypeSupport>* charger_locations_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::ChargerTypeTypeSupport>* charger_types_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::ChargingSessionTypeSupport>* charging_sessions_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::TransitHealthTypeSupport>* transit_health_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::RouteMetricTypeSupport>* route_metrics_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::TransitMetricsTypeSupport>* transit_metrics_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::common::ServiceHeartbeatTypeSupport>* heartbeat_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::internal::ChargingQueryTypeSupport>* charging_query_reader_ =
            nullptr;

    eprosima::safedds::execution::Timer heartbeat_timer_;

    safe_edge::pilot_server::ChargerLocation cached_chargers_[3];
    int32_t cached_charger_count_ = 0;
    bool charger_query_pending_ = false;

    uint64_t last_server_hb_ms_ = 0U;
    bool server_available_ = false;
};

} // namespace nodes
} // namespace non_safety_domain
} // namespace safe_edge

#endif // SAFE_EDGE_NON_SAFETY_DOMAIN_NODES_CLOUDGATEWAYNODE_HPP
