#ifndef SAFE_EDGE_SAFETY_DOMAIN_NODES_POLICYENGINENODE_HPP
#define SAFE_EDGE_SAFETY_DOMAIN_NODES_POLICYENGINENODE_HPP

#include <safe_edge/safety_domain/common/HeaderFactory.hpp>
#include <safe_edge/safety_domain/common/RuntimeConfig.hpp>
#include <safe_edge/safety_domain/policy/PolicyEngine.hpp>

#include <edge.hpp>
#include <internal.hpp>

#include <safedds/memory/container/StaticList.hpp>
#include <safedds/transport/Locator.hpp>

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
namespace safety_domain {
namespace nodes {

class PolicyEngineNode
{
public:

    explicit PolicyEngineNode(const common::RuntimeConfig& runtime_config);

    int run();

private:

    class ParticipantListener :
        public eprosima::safedds::dds::DomainParticipantListener
    {
    public:

        explicit ParticipantListener(PolicyEngineNode& owner);

        void on_subscription_matched(
                eprosima::safedds::dds::DataReader& reader,
                const eprosima::safedds::dds::SubscriptionMatchedStatus& info) noexcept override;

        void on_publication_matched(
                eprosima::safedds::dds::DataWriter& writer,
                const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept override;

    private:

        PolicyEngineNode& owner_;
    };

    class SafetyInputFrameListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit SafetyInputFrameListener(PolicyEngineNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        PolicyEngineNode& owner_;
    };

    class EnergyAdvisoryListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit EnergyAdvisoryListener(PolicyEngineNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        PolicyEngineNode& owner_;
    };

    class EdgeGatewayStatusListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit EdgeGatewayStatusListener(PolicyEngineNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        PolicyEngineNode& owner_;
    };

    class HeartbeatListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit HeartbeatListener(PolicyEngineNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        PolicyEngineNode& owner_;
    };

    class ChargingResponseListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit ChargingResponseListener(PolicyEngineNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        PolicyEngineNode& owner_;
    };

    class ServerAvailabilityStatusListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit ServerAvailabilityStatusListener(PolicyEngineNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        PolicyEngineNode& owner_;
    };

    class EdgeChargerResponseListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit EdgeChargerResponseListener(PolicyEngineNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        PolicyEngineNode& owner_;
    };

    bool initialize();
    bool create_participant();
    bool register_types();
    bool create_topics();
    bool create_endpoints();
    bool enable_entities();
    bool create_executor();
    void start_timers() noexcept;

    void evaluate_and_publish();
    void publish_policy_decision();
    void publish_heartbeat();
    void publish_charging_query(float soc_pct);

    void publish_edge_charger_query(float soc_pct);
    void on_server_availability_status_received(const safe_edge::internal::ServerAvailabilityStatus& status);
    void on_edge_charger_response_received(const safe_edge::internal::ChargingResponse& response);

    void on_safety_input_frame_received(const safe_edge::internal::SafetyInputFrame& frame);
    void on_energy_advisory_received(const safe_edge::edge::EnergyAdvisory& advisory);
    void on_edge_gateway_status_received(const safe_edge::edge::EdgeGatewayStatus& status);
    void on_peer_heartbeat_received(const safe_edge::common::ServiceHeartbeat& heartbeat);
    void on_charging_response_received(const safe_edge::internal::ChargingResponse& response);

    void log_subscription_match(
            const char* topic_name,
            int32_t total_count) const;

    void log_publication_match(
            const char* topic_name,
            int32_t total_count) const;

    eprosima::safedds::execution::TimePoint next_wakeup_time() const noexcept;

    eprosima::safedds::dds::DomainParticipantFactory factory_;
    common::RuntimeConfig runtime_config_;
    common::HeaderFactory header_factory_;
    policy::PolicyEngine policy_engine_;

    ParticipantListener participant_listener_;
    SafetyInputFrameListener safety_input_frame_listener_;
    EnergyAdvisoryListener energy_advisory_listener_;
    EdgeGatewayStatusListener edge_gateway_status_listener_;
    HeartbeatListener heartbeat_listener_;
    ChargingResponseListener charging_response_listener_;
    ServerAvailabilityStatusListener server_availability_status_listener_;
    EdgeChargerResponseListener edge_charger_response_listener_;

    safe_edge::internal::SafetyInputFrameTypeSupport safety_input_frame_type_support_;
    safe_edge::edge::EnergyAdvisoryTypeSupport energy_advisory_type_support_;
    safe_edge::edge::EdgeGatewayStatusTypeSupport edge_gateway_status_type_support_;
    safe_edge::internal::PolicyDecisionTypeSupport policy_decision_type_support_;
    safe_edge::common::ServiceHeartbeatTypeSupport service_heartbeat_type_support_;
    safe_edge::internal::ChargingQueryTypeSupport charging_query_type_support_;
    safe_edge::internal::ChargingResponseTypeSupport charging_response_type_support_;
    safe_edge::internal::ServerAvailabilityStatusTypeSupport server_availability_status_type_support_;

    eprosima::safedds::memory::container::StaticList<eprosima::safedds::transport::Locator, 1U> initial_peers_;

    eprosima::safedds::dds::DomainParticipant* participant_ = nullptr;
    eprosima::safedds::dds::Publisher* publisher_ = nullptr;
    eprosima::safedds::dds::Subscriber* subscriber_ = nullptr;
    eprosima::safedds::execution::ISpinnable* executor_ = nullptr;

    eprosima::safedds::dds::Topic* safety_input_frame_topic_ = nullptr;
    eprosima::safedds::dds::Topic* energy_advisory_topic_ = nullptr;
    eprosima::safedds::dds::Topic* edge_gateway_status_topic_ = nullptr;
    eprosima::safedds::dds::Topic* policy_decision_topic_ = nullptr;
    eprosima::safedds::dds::Topic* service_heartbeat_topic_ = nullptr;
    eprosima::safedds::dds::Topic* charging_query_topic_ = nullptr;
    eprosima::safedds::dds::Topic* charging_response_topic_ = nullptr;
    eprosima::safedds::dds::Topic* server_availability_status_topic_ = nullptr;
    eprosima::safedds::dds::Topic* edge_charger_query_topic_ = nullptr;
    eprosima::safedds::dds::Topic* edge_charger_response_topic_ = nullptr;

    eprosima::safedds::memory::container::StaticString256 safety_input_frame_topic_name_;
    eprosima::safedds::memory::container::StaticString256 energy_advisory_topic_name_;
    eprosima::safedds::memory::container::StaticString256 edge_gateway_status_topic_name_;
    eprosima::safedds::memory::container::StaticString256 policy_decision_topic_name_;
    eprosima::safedds::memory::container::StaticString256 service_heartbeat_topic_name_;
    eprosima::safedds::memory::container::StaticString256 charging_query_topic_name_;
    eprosima::safedds::memory::container::StaticString256 charging_response_topic_name_;
    eprosima::safedds::memory::container::StaticString256 server_availability_status_topic_name_;
    eprosima::safedds::memory::container::StaticString256 edge_charger_query_topic_name_;
    eprosima::safedds::memory::container::StaticString256 edge_charger_response_topic_name_;

    eprosima::safedds::dds::DataWriter* policy_decision_datawriter_ = nullptr;
    eprosima::safedds::dds::DataWriter* service_heartbeat_datawriter_ = nullptr;
    eprosima::safedds::dds::DataWriter* charging_query_datawriter_ = nullptr;
    eprosima::safedds::dds::DataWriter* edge_charger_query_datawriter_ = nullptr;
    eprosima::safedds::dds::DataWriter* energy_advisory_out_datawriter_ = nullptr;

    eprosima::safedds::dds::TypedDataWriter<safe_edge::internal::PolicyDecisionTypeSupport>* policy_decision_writer_ =
            nullptr;
    eprosima::safedds::dds::TypedDataWriter<safe_edge::common::ServiceHeartbeatTypeSupport>* service_heartbeat_writer_ =
            nullptr;
    eprosima::safedds::dds::TypedDataWriter<safe_edge::internal::ChargingQueryTypeSupport>* charging_query_writer_ =
            nullptr;
    eprosima::safedds::dds::TypedDataWriter<safe_edge::internal::ChargingQueryTypeSupport>* edge_charger_query_writer_ =
            nullptr;
    eprosima::safedds::dds::TypedDataWriter<safe_edge::edge::EnergyAdvisoryTypeSupport>* energy_advisory_out_writer_ =
            nullptr;

    eprosima::safedds::dds::DataReader* safety_input_frame_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* energy_advisory_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* edge_gateway_status_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* heartbeat_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* charging_response_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* server_availability_status_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* edge_charger_response_datareader_ = nullptr;

    eprosima::safedds::dds::TypedDataReader<safe_edge::internal::SafetyInputFrameTypeSupport>* safety_input_frame_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::edge::EnergyAdvisoryTypeSupport>* energy_advisory_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::edge::EdgeGatewayStatusTypeSupport>* edge_gateway_status_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::common::ServiceHeartbeatTypeSupport>* heartbeat_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::internal::ChargingResponseTypeSupport>* charging_response_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::internal::ServerAvailabilityStatusTypeSupport>* server_availability_status_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::internal::ChargingResponseTypeSupport>* edge_charger_response_reader_ =
            nullptr;

    eprosima::safedds::execution::Timer heartbeat_timer_;

    safe_edge::internal::SafetyInputFrame latest_safety_input_frame_{};
    safe_edge::edge::EnergyAdvisory latest_energy_advisory_{};
    safe_edge::edge::EdgeGatewayStatus latest_edge_gateway_status_{};

    bool have_safety_input_frame_ = false;
    bool have_energy_advisory_ = false;
    bool have_edge_gateway_status_ = false;
    bool pending_policy_publish_ = false;
    bool charging_query_pending_ = false;
    bool edge_charger_query_pending_ = false;
    bool server_available_ = false;
    uint64_t edge_status_last_ms_ = 0U;
};

} // namespace nodes
} // namespace safety_domain
} // namespace safe_edge

#endif // SAFE_EDGE_SAFETY_DOMAIN_NODES_POLICYENGINENODE_HPP
