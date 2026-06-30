#ifndef SAFE_EDGE_SAFETY_DOMAIN_NODES_SAFETYIOADAPTERSNODE_HPP
#define SAFE_EDGE_SAFETY_DOMAIN_NODES_SAFETYIOADAPTERSNODE_HPP

#include <safe_edge/safety_domain/common/HeaderFactory.hpp>
#include <safe_edge/safety_domain/common/RuntimeConfig.hpp>

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

#include <cstdint>

namespace safe_edge {
namespace safety_domain {
namespace nodes {

class SafetyIoAdaptersNode
{
public:

    explicit SafetyIoAdaptersNode(const common::RuntimeConfig& runtime_config);

    int run();

private:

    class ParticipantListener :
        public eprosima::safedds::dds::DomainParticipantListener
    {
    public:

        explicit ParticipantListener(SafetyIoAdaptersNode& owner);

        void on_subscription_matched(
                eprosima::safedds::dds::DataReader& reader,
                const eprosima::safedds::dds::SubscriptionMatchedStatus& info) noexcept override;

        void on_publication_matched(
                eprosima::safedds::dds::DataWriter& writer,
                const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept override;

    private:

        SafetyIoAdaptersNode& owner_;
    };

    class PolicyDecisionListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit PolicyDecisionListener(SafetyIoAdaptersNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        SafetyIoAdaptersNode& owner_;
    };

    class EnergyAdvisoryListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit EnergyAdvisoryListener(SafetyIoAdaptersNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        SafetyIoAdaptersNode& owner_;
    };

    class HeartbeatListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit HeartbeatListener(SafetyIoAdaptersNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        SafetyIoAdaptersNode& owner_;
    };

    class SafetyInputFrameListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit SafetyInputFrameListener(SafetyIoAdaptersNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        SafetyIoAdaptersNode& owner_;
    };

    bool initialize();
    bool create_participant();
    bool register_types();
    bool create_topics();
    bool create_endpoints();
    bool enable_entities();
    bool create_executor();
    void start_timers() noexcept;

    void publish_edge_gateway_status();
    void publish_heartbeat();
    void publish_vehicle_edge_summary();

    void on_safety_input_frame_received(const safe_edge::internal::SafetyInputFrame& frame);
    void on_policy_decision_received(const safe_edge::internal::PolicyDecision& decision);
    void on_energy_advisory_received(const safe_edge::edge::EnergyAdvisory& advisory);
    void on_peer_heartbeat_received(const safe_edge::common::ServiceHeartbeat& heartbeat);

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

    ParticipantListener participant_listener_;
    PolicyDecisionListener policy_decision_listener_;
    EnergyAdvisoryListener energy_advisory_listener_;
    HeartbeatListener heartbeat_listener_;
    SafetyInputFrameListener safety_input_frame_listener_;

    safe_edge::internal::SafetyInputFrameTypeSupport safety_input_frame_type_support_;
    safe_edge::edge::EnergyAdvisoryTypeSupport energy_advisory_type_support_;
    safe_edge::edge::EdgeGatewayStatusTypeSupport edge_gateway_status_type_support_;
    safe_edge::edge::VehicleEdgeSummaryTypeSupport vehicle_edge_summary_type_support_;
    safe_edge::internal::PolicyDecisionTypeSupport policy_decision_type_support_;
    safe_edge::common::ServiceHeartbeatTypeSupport service_heartbeat_type_support_;

    eprosima::safedds::memory::container::StaticList<eprosima::safedds::transport::Locator, 8U> initial_peers_;

    eprosima::safedds::dds::DomainParticipant* participant_ = nullptr;
    eprosima::safedds::dds::Publisher* publisher_ = nullptr;
    eprosima::safedds::dds::Subscriber* subscriber_ = nullptr;
    eprosima::safedds::execution::ISpinnable* executor_ = nullptr;

    eprosima::safedds::dds::Topic* safety_input_frame_topic_ = nullptr;
    eprosima::safedds::dds::Topic* energy_advisory_topic_ = nullptr;
    eprosima::safedds::dds::Topic* edge_gateway_status_topic_ = nullptr;
    eprosima::safedds::dds::Topic* vehicle_edge_summary_topic_ = nullptr;
    eprosima::safedds::dds::Topic* policy_decision_topic_ = nullptr;
    eprosima::safedds::dds::Topic* service_heartbeat_topic_ = nullptr;

    eprosima::safedds::memory::container::StaticString256 safety_input_frame_topic_name_;
    eprosima::safedds::memory::container::StaticString256 energy_advisory_topic_name_;
    eprosima::safedds::memory::container::StaticString256 edge_gateway_status_topic_name_;
    eprosima::safedds::memory::container::StaticString256 vehicle_edge_summary_topic_name_;
    eprosima::safedds::memory::container::StaticString256 policy_decision_topic_name_;
    eprosima::safedds::memory::container::StaticString256 service_heartbeat_topic_name_;

    eprosima::safedds::dds::DataWriter* edge_gateway_status_datawriter_ = nullptr;
    eprosima::safedds::dds::DataWriter* vehicle_edge_summary_datawriter_ = nullptr;
    eprosima::safedds::dds::DataWriter* service_heartbeat_datawriter_ = nullptr;

    eprosima::safedds::dds::TypedDataWriter<safe_edge::edge::EdgeGatewayStatusTypeSupport>* edge_gateway_status_writer_ =
            nullptr;
    eprosima::safedds::dds::TypedDataWriter<safe_edge::edge::VehicleEdgeSummaryTypeSupport>* vehicle_edge_summary_writer_ =
            nullptr;
    eprosima::safedds::dds::TypedDataWriter<safe_edge::common::ServiceHeartbeatTypeSupport>* service_heartbeat_writer_ =
            nullptr;

    eprosima::safedds::dds::DataReader* safety_input_frame_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* policy_decision_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* energy_advisory_datareader_ = nullptr;
    eprosima::safedds::dds::DataReader* heartbeat_datareader_ = nullptr;

    eprosima::safedds::dds::TypedDataReader<safe_edge::internal::SafetyInputFrameTypeSupport>* safety_input_frame_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::internal::PolicyDecisionTypeSupport>* policy_decision_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::edge::EnergyAdvisoryTypeSupport>* energy_advisory_reader_ =
            nullptr;
    eprosima::safedds::dds::TypedDataReader<safe_edge::common::ServiceHeartbeatTypeSupport>* heartbeat_reader_ =
            nullptr;

    eprosima::safedds::execution::Timer edge_gateway_status_timer_;
    eprosima::safedds::execution::Timer heartbeat_timer_;

    safe_edge::internal::SafetyInputFrame latest_safety_input_frame_{};
    safe_edge::internal::PolicyDecision latest_policy_decision_{};
    bool have_safety_input_frame_ = false;
    bool have_policy_decision_ = false;
    bool pending_vehicle_edge_summary_publish_ = false;

    safe_edge::edge::VehicleEdgeSummary last_published_vehicle_edge_summary_{};
    bool has_published_vehicle_edge_summary_ = false;

    uint64_t last_advisory_received_ms_ = 0U;
};

} // namespace nodes
} // namespace safety_domain
} // namespace safe_edge

#endif // SAFE_EDGE_SAFETY_DOMAIN_NODES_SAFETYIOADAPTERSNODE_HPP
