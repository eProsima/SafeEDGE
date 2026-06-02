#ifndef SAFE_EDGE_EDGE_MODULE_NODES_EDGEGATEWAYNODE_HPP
#define SAFE_EDGE_EDGE_MODULE_NODES_EDGEGATEWAYNODE_HPP

#include <safe_edge/edge_module/common/HeaderFactory.hpp>
#include <safe_edge/edge_module/common/RuntimeConfig.hpp>

#include <common.hpp>
#include <edge.hpp>
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
#include <safedds/memory/container/StaticString.hpp>

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
        public eprosima::safedds::dds::DomainParticipantListener
    {
    public:

        explicit ParticipantListener(EdgeGatewayNode& owner);

        void on_subscription_matched(
                eprosima::safedds::dds::DataReader& reader,
                const eprosima::safedds::dds::SubscriptionMatchedStatus& info) noexcept override;

        void on_publication_matched(
                eprosima::safedds::dds::DataWriter& writer,
                const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept override;

    private:

        EdgeGatewayNode& owner_;
    };

    class VehicleEdgeSummaryListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit VehicleEdgeSummaryListener(EdgeGatewayNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        EdgeGatewayNode& owner_;
    };

    class ChargerLocationListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit ChargerLocationListener(EdgeGatewayNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        EdgeGatewayNode& owner_;
    };

    class HeartbeatListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit HeartbeatListener(EdgeGatewayNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        EdgeGatewayNode& owner_;
    };

    bool initialize();
    bool create_participant();
    bool register_types();
    bool create_topics();
    bool create_endpoints();
    bool enable_entities();
    bool create_executor();
    void start_timers() noexcept;

    void on_vehicle_edge_summary_received(const safe_edge::edge::VehicleEdgeSummary& summary);
    void on_charger_location_received(const safe_edge::pilot_server::ChargerLocation& location);
    void on_server_heartbeat_received(const safe_edge::common::ServiceHeartbeat& heartbeat);
    void publish_energy_advisory(const safe_edge::edge::EnergyAdvisory& advisory);
    void publish_edge_gateway_status();

    void log_subscription_match(const char* topic_name, int32_t total_count) const;
    void log_publication_match(const char* topic_name, int32_t total_count) const;

    eprosima::safedds::execution::TimePoint next_wakeup_time() const noexcept;

    eprosima::safedds::dds::DomainParticipantFactory factory_;
    common::RuntimeConfig runtime_config_;
    common::HeaderFactory header_factory_;

    ParticipantListener participant_listener_;
    VehicleEdgeSummaryListener vehicle_edge_summary_listener_;
    ChargerLocationListener charger_location_listener_;
    HeartbeatListener heartbeat_listener_;

    safe_edge::edge::VehicleEdgeSummaryTypeSupport vehicle_edge_summary_type_support_;
    safe_edge::edge::EnergyAdvisoryTypeSupport energy_advisory_type_support_;
    safe_edge::edge::EdgeGatewayStatusTypeSupport edge_gateway_status_type_support_;
    safe_edge::pilot_server::ChargerLocationTypeSupport charger_location_type_support_;
    safe_edge::common::ServiceHeartbeatTypeSupport service_heartbeat_type_support_;

    eprosima::safedds::dds::DomainParticipant* participant_ = nullptr;
    eprosima::safedds::dds::Publisher* publisher_ = nullptr;
    eprosima::safedds::dds::Subscriber* subscriber_ = nullptr;
    eprosima::safedds::execution::ISpinnable* executor_ = nullptr;

    eprosima::safedds::dds::Topic* vehicle_edge_summary_topic_ = nullptr;
    eprosima::safedds::dds::Topic* energy_advisory_topic_ = nullptr;
    eprosima::safedds::dds::Topic* edge_gateway_status_topic_ = nullptr;
    eprosima::safedds::dds::Topic* charger_location_topic_ = nullptr;
    eprosima::safedds::dds::Topic* service_heartbeat_topic_ = nullptr;

    eprosima::safedds::memory::container::StaticString256 vehicle_edge_summary_topic_name_;
    eprosima::safedds::memory::container::StaticString256 energy_advisory_topic_name_;
    eprosima::safedds::memory::container::StaticString256 edge_gateway_status_topic_name_;
    eprosima::safedds::memory::container::StaticString256 charger_location_topic_name_;
    eprosima::safedds::memory::container::StaticString256 service_heartbeat_topic_name_;

    eprosima::safedds::dds::DataWriter* energy_advisory_datawriter_ = nullptr;
    eprosima::safedds::dds::DataWriter* edge_gateway_status_datawriter_ = nullptr;

    eprosima::safedds::dds::TypedDataWriter<safe_edge::edge::EnergyAdvisoryTypeSupport>* energy_advisory_writer_ =
            nullptr;
    eprosima::safedds::dds::TypedDataWriter<safe_edge::edge::EdgeGatewayStatusTypeSupport>* edge_gateway_status_writer_ =
            nullptr;

    eprosima::safedds::dds::DataReader* vehicle_edge_summary_datareader_ = nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::edge::VehicleEdgeSummaryTypeSupport>* vehicle_edge_summary_reader_ =
            nullptr;

    eprosima::safedds::dds::DataReader* charger_location_datareader_ = nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::ChargerLocationTypeSupport>* charger_location_reader_ =
            nullptr;

    eprosima::safedds::dds::DataReader* service_heartbeat_datareader_ = nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::common::ServiceHeartbeatTypeSupport>* heartbeat_reader_ =
            nullptr;

    safe_edge::pilot_server::ChargerLocation cached_chargers_[3];
    int32_t cached_charger_count_ = 0;
    uint64_t last_server_sync_ms_ = 0U;
    uint64_t last_server_hb_ms_ = 0U;
    bool server_available_ = false;

    eprosima::safedds::execution::Timer status_timer_;
};

} // namespace nodes
} // namespace edge_module
} // namespace safe_edge

#endif // SAFE_EDGE_EDGE_MODULE_NODES_EDGEGATEWAYNODE_HPP
