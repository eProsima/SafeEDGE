#ifndef SAFE_EDGE_EDGE_MODULE_NODES_EDGEGATEWAYNODE_HPP
#define SAFE_EDGE_EDGE_MODULE_NODES_EDGEGATEWAYNODE_HPP

#include <safe_edge/edge_module/common/HeaderFactory.hpp>
#include <safe_edge/edge_module/common/RuntimeConfig.hpp>

#include <common.hpp>
#include <edge.hpp>
#include <pilot_server.hpp>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

#include <chrono>
#include <string>

namespace safe_edge {
namespace edge_module {
namespace nodes {

class EdgeGatewayNode
{
public:

    explicit EdgeGatewayNode(const common::RuntimeConfig& runtime_config);

    int run();

private:

    class ParticipantListener :
        public eprosima::fastdds::dds::DomainParticipantListener
    {
    public:

        explicit ParticipantListener(EdgeGatewayNode& owner);

        void on_subscription_matched(
                eprosima::fastdds::dds::DataReader* reader,
                const eprosima::fastdds::dds::SubscriptionMatchedStatus& info) noexcept override;

        void on_publication_matched(
                eprosima::fastdds::dds::DataWriter* writer,
                const eprosima::fastdds::dds::PublicationMatchedStatus& info) noexcept override;

    private:

        EdgeGatewayNode& owner_;
    };

    class VehicleEdgeSummaryListener :
        public eprosima::fastdds::dds::DataReaderListener
    {
    public:

        explicit VehicleEdgeSummaryListener(EdgeGatewayNode& owner);

        void on_data_available(
                eprosima::fastdds::dds::DataReader* reader) noexcept override;

    private:

        EdgeGatewayNode& owner_;
    };

    class ChargerLocationListener :
        public eprosima::fastdds::dds::DataReaderListener
    {
    public:

        explicit ChargerLocationListener(EdgeGatewayNode& owner);

        void on_data_available(
                eprosima::fastdds::dds::DataReader* reader) noexcept override;

    private:

        EdgeGatewayNode& owner_;
    };

    class HeartbeatListener :
        public eprosima::fastdds::dds::DataReaderListener
    {
    public:

        explicit HeartbeatListener(EdgeGatewayNode& owner);

        void on_data_available(
                eprosima::fastdds::dds::DataReader* reader) noexcept override;

    private:

        EdgeGatewayNode& owner_;
    };

    bool initialize();
    bool create_participant();
    bool register_types();
    bool create_topics();
    bool create_endpoints();
    bool enable_entities();

    void on_vehicle_edge_summary_received(const safe_edge::edge::VehicleEdgeSummary& summary);
    void on_charger_location_received(const safe_edge::pilot_server::ChargerLocation& location);
    void on_server_heartbeat_received(const safe_edge::common::ServiceHeartbeat& heartbeat);
    void publish_energy_advisory(const safe_edge::edge::EnergyAdvisory& advisory);
    void publish_edge_gateway_status();
    void publish_heartbeat();

    void log_subscription_match(const char* topic_name, int32_t total_count) const;
    void log_publication_match(const char* topic_name, int32_t total_count) const;

    common::RuntimeConfig runtime_config_;
    common::HeaderFactory header_factory_;

    ParticipantListener participant_listener_;
    VehicleEdgeSummaryListener vehicle_edge_summary_listener_;
    ChargerLocationListener charger_location_listener_;
    HeartbeatListener heartbeat_listener_;

    eprosima::fastdds::dds::TypeSupport vehicle_edge_summary_type_support_;
    eprosima::fastdds::dds::TypeSupport energy_advisory_type_support_;
    eprosima::fastdds::dds::TypeSupport edge_gateway_status_type_support_;
    eprosima::fastdds::dds::TypeSupport charger_location_type_support_;
    eprosima::fastdds::dds::TypeSupport service_heartbeat_type_support_;

    eprosima::fastdds::dds::DomainParticipant* participant_ = nullptr;
    eprosima::fastdds::dds::Publisher* publisher_ = nullptr;
    eprosima::fastdds::dds::Subscriber* subscriber_ = nullptr;

    std::string vehicle_edge_summary_topic_name_;
    std::string energy_advisory_topic_name_;
    std::string edge_gateway_status_topic_name_;
    std::string charger_location_topic_name_;
    std::string service_heartbeat_topic_name_;

    eprosima::fastdds::dds::Topic* vehicle_edge_summary_topic_ = nullptr;
    eprosima::fastdds::dds::Topic* energy_advisory_topic_ = nullptr;
    eprosima::fastdds::dds::Topic* edge_gateway_status_topic_ = nullptr;
    eprosima::fastdds::dds::Topic* charger_location_topic_ = nullptr;
    eprosima::fastdds::dds::Topic* service_heartbeat_topic_ = nullptr;

    eprosima::fastdds::dds::DataWriter* energy_advisory_datawriter_ = nullptr;
    eprosima::fastdds::dds::DataWriter* edge_gateway_status_datawriter_ = nullptr;
    eprosima::fastdds::dds::DataWriter* service_heartbeat_datawriter_ = nullptr;

    eprosima::fastdds::dds::DataReader* vehicle_edge_summary_datareader_ = nullptr;
    eprosima::fastdds::dds::DataReader* charger_location_datareader_ = nullptr;
    eprosima::fastdds::dds::DataReader* service_heartbeat_datareader_ = nullptr;

    safe_edge::pilot_server::ChargerLocation cached_chargers_[3];
    int32_t cached_charger_count_ = 0;
    uint64_t last_server_sync_ms_ = 0U;
    uint64_t last_server_hb_ms_ = 0U;
    bool server_available_ = false;

    std::chrono::steady_clock::time_point next_status_fire_;
};

} // namespace nodes
} // namespace edge_module
} // namespace safe_edge

#endif // SAFE_EDGE_EDGE_MODULE_NODES_EDGEGATEWAYNODE_HPP
