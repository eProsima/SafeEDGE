#ifndef SAFE_EDGE_SAFETY_DOMAIN_NODES_VEHICLEMOCKNODE_HPP
#define SAFE_EDGE_SAFETY_DOMAIN_NODES_VEHICLEMOCKNODE_HPP

#include <safe_edge/safety_domain/common/HeaderFactory.hpp>
#include <safe_edge/safety_domain/common/RuntimeConfig.hpp>

#include <internal.hpp>

<<<<<<< Updated upstream
=======
#include <safedds/dds/DataReader.hpp>
#include <safedds/dds/DataReaderListener.hpp>
>>>>>>> Stashed changes
#include <safedds/dds/DataWriter.hpp>
#include <safedds/dds/DomainParticipant.hpp>
#include <safedds/dds/DomainParticipantFactory.hpp>
#include <safedds/dds/DomainParticipantListener.hpp>
#include <safedds/dds/Publisher.hpp>
<<<<<<< Updated upstream
#include <safedds/dds/Topic.hpp>
#include <safedds/dds/TypedDataWriter.hpp>
#include <safedds/execution/Timer.hpp>
#include <safedds/memory/container/StaticString.hpp>
=======
#include <safedds/dds/SampleInfo.hpp>
#include <safedds/dds/Subscriber.hpp>
#include <safedds/dds/Topic.hpp>
#include <safedds/dds/TypedDataReader.hpp>
#include <safedds/dds/TypedDataWriter.hpp>
#include <safedds/execution/TimePoint.hpp>
#include <safedds/execution/Timer.hpp>
#include <safedds/memory/container/StaticList.hpp>
#include <safedds/memory/container/StaticString.hpp>
#include <safedds/transport/Locator.hpp>
>>>>>>> Stashed changes

#include <cstdint>

namespace safe_edge {
namespace safety_domain {
namespace nodes {

class VehicleMockNode
{
public:

    explicit VehicleMockNode(const common::RuntimeConfig& runtime_config);

    int run();

private:

    class ParticipantListener :
        public eprosima::safedds::dds::DomainParticipantListener
    {
    public:

        explicit ParticipantListener(VehicleMockNode& owner);

        void on_publication_matched(
                eprosima::safedds::dds::DataWriter& writer,
                const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept override;

    private:

        VehicleMockNode& owner_;
    };

<<<<<<< Updated upstream
=======
    class PolicyDecisionListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit PolicyDecisionListener(VehicleMockNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        VehicleMockNode& owner_;
    };

>>>>>>> Stashed changes
    bool initialize();
    bool create_participant();
    bool register_types();
    bool create_topics();
    bool create_endpoints();
<<<<<<< Updated upstream
=======
    bool create_subscriber();
>>>>>>> Stashed changes
    bool enable_entities();
    bool create_executor();

    void publish_frame();
    void republish_last_frame() noexcept;
<<<<<<< Updated upstream
=======
    void on_policy_decision_received(
            const safe_edge::internal::PolicyDecision& decision,
            const eprosima::safedds::execution::TimePoint& t_rx) noexcept;
>>>>>>> Stashed changes

    eprosima::safedds::dds::DomainParticipantFactory factory_;
    common::RuntimeConfig runtime_config_;
    common::HeaderFactory header_factory_;

    ParticipantListener participant_listener_;
<<<<<<< Updated upstream
=======
    PolicyDecisionListener policy_decision_listener_;

    eprosima::safedds::memory::container::StaticList<eprosima::safedds::transport::Locator, 2U> initial_peers_;
>>>>>>> Stashed changes

    safe_edge::internal::SafetyInputFrameTypeSupport safety_input_frame_type_support_;
    eprosima::safedds::dds::DomainParticipant* participant_ = nullptr;
    eprosima::safedds::dds::Publisher* publisher_ = nullptr;
    eprosima::safedds::execution::ISpinnable* executor_ = nullptr;

    eprosima::safedds::dds::Topic* safety_input_frame_topic_ = nullptr;
    eprosima::safedds::memory::container::StaticString256 safety_input_frame_topic_name_;
    eprosima::safedds::dds::DataWriter* safety_input_frame_datawriter_ = nullptr;
    eprosima::safedds::dds::TypedDataWriter<safe_edge::internal::SafetyInputFrameTypeSupport>*
        safety_input_frame_writer_ = nullptr;

    eprosima::safedds::execution::Timer publish_timer_;

    safe_edge::internal::SafetyInputFrame last_frame_{};
    bool have_last_frame_ = false;
<<<<<<< Updated upstream
=======

    safe_edge::internal::PolicyDecisionTypeSupport          policy_decision_type_support_;
    eprosima::safedds::dds::Subscriber*                     subscriber_                 = nullptr;
    eprosima::safedds::dds::Topic*                          policy_decision_topic_      = nullptr;
    eprosima::safedds::memory::container::StaticString256   policy_decision_topic_name_;
    eprosima::safedds::dds::DataReader*                     policy_decision_reader_     = nullptr;
    eprosima::safedds::dds::TypedDataReader<
        safe_edge::internal::PolicyDecisionTypeSupport>*    policy_decision_typed_      = nullptr;
>>>>>>> Stashed changes
};

} // namespace nodes
} // namespace safety_domain
} // namespace safe_edge

#endif // SAFE_EDGE_SAFETY_DOMAIN_NODES_VEHICLEMOCKNODE_HPP
