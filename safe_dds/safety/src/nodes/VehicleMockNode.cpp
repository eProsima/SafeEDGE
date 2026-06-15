#include <safe_edge/safety_domain/nodes/VehicleMockNode.hpp>

#include <safe_edge/safety_domain/common/TopicNames.hpp>

#include <safedds/dds/Publisher.hpp>
#include <safedds/dds/ReturnCode.hpp>
#include <safedds/dds/qos/DataWriterQos.hpp>
#include <safedds/dds/qos/DomainParticipantQos.hpp>
#include <safedds/dds/qos/PublisherQos.hpp>
#include <safedds/dds/qos/TopicQos.hpp>
#include <safedds/execution/TimePoint.hpp>
#include <safedds/dds/SampleInfo.hpp>
#include <safedds/dds/qos/DataReaderQos.hpp>
#include <safedds/dds/qos/DataWriterQos.hpp>
#include <safedds/dds/qos/DomainParticipantQos.hpp>
#include <safedds/dds/qos/PublisherQos.hpp>
#include <safedds/dds/qos/SubscriberQos.hpp>
#include <safedds/dds/qos/TopicQos.hpp>
#include <safedds/execution/TimePoint.hpp>
#include <safedds/platform.hpp>
#include <safedds/transport.hpp>

#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace safe_edge {
namespace safety_domain {
namespace nodes {

namespace {

constexpr eprosima::safedds::execution::TimePeriod TIMEOUT = {0, 250'000'000};

static constexpr const char* INPUT_FILE_PATH = "/data/safe-edge-stage2/input.txt";

static const char* resolve_input_file_path() noexcept
{
    const char* override_path = std::getenv("SAFE_EDGE_INPUT_FILE");
    return (nullptr != override_path && '\0' != override_path[0]) ? override_path : INPUT_FILE_PATH;
}

struct InputConfig
{
    float soc;
    bool  emergency_stop;
    bool  adas_fault;
    float available_charge_kw;
    float available_discharge_kw;
    bool  v2g_ready;
    float speed_mps;
    bool  braking_available;
    bool  steering_available;
};

static InputConfig read_input_file()
{
    InputConfig cfg;
    cfg.soc                  = 50.0F;
    cfg.emergency_stop       = false;
    cfg.adas_fault           = false;
    cfg.available_charge_kw  = 50.0F;
    cfg.available_discharge_kw = 50.0F;
    cfg.v2g_ready            = true;
    cfg.speed_mps            = 0.0F;
    cfg.braking_available    = true;
    cfg.steering_available   = true;

    std::FILE* f = std::fopen(resolve_input_file_path(), "r");
    if (nullptr != f)
    {
        char line[64];
        int  ival;

        if (nullptr != std::fgets(line, static_cast<int>(sizeof(line)), f))
        {
            std::sscanf(line, "soc=%f", &cfg.soc);
        }
        if (nullptr != std::fgets(line, static_cast<int>(sizeof(line)), f))
        {
            std::sscanf(line, "emergency_stop=%d", &ival);
            cfg.emergency_stop = (ival != 0);
        }
        if (nullptr != std::fgets(line, static_cast<int>(sizeof(line)), f))
        {
            std::sscanf(line, "adas_fault=%d", &ival);
            cfg.adas_fault = (ival != 0);
        }
        if (nullptr != std::fgets(line, static_cast<int>(sizeof(line)), f))
        {
            std::sscanf(line, "available_charge_kw=%f", &cfg.available_charge_kw);
        }
        if (nullptr != std::fgets(line, static_cast<int>(sizeof(line)), f))
        {
            std::sscanf(line, "available_discharge_kw=%f", &cfg.available_discharge_kw);
        }
        if (nullptr != std::fgets(line, static_cast<int>(sizeof(line)), f))
        {
            std::sscanf(line, "v2g_ready=%d", &ival);
            cfg.v2g_ready = (ival != 0);
        }
        if (nullptr != std::fgets(line, static_cast<int>(sizeof(line)), f))
        {
            std::sscanf(line, "speed_mps=%f", &cfg.speed_mps);
        }
        if (nullptr != std::fgets(line, static_cast<int>(sizeof(line)), f))
        {
            std::sscanf(line, "braking_available=%d", &ival);
            cfg.braking_available = (ival != 0);
        }
        if (nullptr != std::fgets(line, static_cast<int>(sizeof(line)), f))
        {
            std::sscanf(line, "steering_available=%d", &ival);
            cfg.steering_available = (ival != 0);
        }

        std::fclose(f);
    }
    return cfg;
}

} // namespace

VehicleMockNode::ParticipantListener::ParticipantListener(
        VehicleMockNode& owner)
    : owner_(owner)
{
}

void VehicleMockNode::ParticipantListener::on_publication_matched(
        eprosima::safedds::dds::DataWriter& writer,
        const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept
{
    if (&writer == owner_.safety_input_frame_datawriter_ &&
        info.total_count_change > 0 &&
        owner_.have_last_frame_)
    {
        owner_.republish_last_frame();
    }
}

VehicleMockNode::PolicyDecisionListener::PolicyDecisionListener(
        VehicleMockNode& owner)
    : owner_(owner)
{
}

void VehicleMockNode::PolicyDecisionListener::on_data_available(
        eprosima::safedds::dds::DataReader&) noexcept
{
    const eprosima::safedds::execution::TimePoint t_rx =
        eprosima::safedds::get_platform().get_current_timepoint();

    safe_edge::internal::PolicyDecision sample{};
    eprosima::safedds::dds::SampleInfo info{};

    if (owner_.policy_decision_typed_->take_next_sample(sample, info)
            == eprosima::safedds::dds::ReturnCode::OK && info.valid_data)
    {
        owner_.on_policy_decision_received(sample, t_rx);
    }
}

VehicleMockNode::VehicleMockNode(
        const common::RuntimeConfig& runtime_config)
    : runtime_config_(runtime_config)
    , header_factory_(runtime_config.source_name)
    , participant_listener_(*this)
    , policy_decision_listener_(*this)
    , publish_timer_(TIMEOUT)
{
}

int VehicleMockNode::run()
{
    if (!initialize())
    {
        return 1;
    }

    publish_timer_.start();
    std::cout << "[vehicle_mock] [START] Running with participant port " << runtime_config_.participant_port << std::endl;

    while (true)
    {
        while (executor_->has_pending_work())
        {
            executor_->spin(eprosima::safedds::execution::TIME_ZERO);
        }

        if (publish_timer_.is_triggered_and_reset())
        {
            publish_frame();
        }

        eprosima::safedds::execution::TimePoint next_work_timepoint = eprosima::safedds::execution::TimePoint::min(executor_->get_next_work_timepoint(), publish_timer_.next_trigger());

        executor_->spin(next_work_timepoint);
    }

    return 0;
}

bool VehicleMockNode::initialize()
{
    return create_participant() &&
           register_types() &&
           create_topics() &&
           create_endpoints() &&
           create_subscriber() &&
           enable_entities() &&
           create_executor();
}

bool VehicleMockNode::create_participant()
{
    eprosima::safedds::dds::DomainParticipantQos participant_qos{};
    eprosima::safedds::memory::container::StaticString256 participant_name(runtime_config_.participant_name.c_str());
    participant_qos.participant_name() = participant_name;
    participant_qos.wire_protocol_qos().announced_locator = eprosima::safedds::transport::Locator::from_ipv4(
        runtime_config_.own_ip,
        runtime_config_.participant_port);
    participant_qos.wire_protocol_qos().use_multicast_discovery = false;
    for (std::size_t i = 0U; i < runtime_config_.initial_peer_count; ++i)
    {
        const auto& peer_ip =
                (runtime_config_.initial_peer_ports[i] >= 8001U && runtime_config_.initial_peer_ports[i] <= 8003U) ?
                runtime_config_.own_ip :
                runtime_config_.cross_domain_peer_ip;
        initial_peers_.add(eprosima::safedds::transport::Locator::from_ipv4(peer_ip, runtime_config_.initial_peer_ports[i]));
    }
    participant_qos.wire_protocol_qos().initial_peers = &initial_peers_;
    participant_ = factory_.create_participant(
        runtime_config_.domain_id,
        participant_qos,
        &participant_listener_,
        eprosima::safedds::dds::PUBLICATION_MATCHED_STATUS);

    if (nullptr == participant_)
    {
        std::cerr << "[vehicle_mock] Failed to create participant" << std::endl;
        return false;
    }

    return true;
}

bool VehicleMockNode::register_types()
{
    if (eprosima::safedds::dds::ReturnCode::OK !=
            safety_input_frame_type_support_.register_type(
                *participant_, safety_input_frame_type_support_.get_type_name()))
    {
        std::cerr << "[vehicle_mock] Failed to register type: SafetyInputFrame" << std::endl;
        return false;
    }

    if (eprosima::safedds::dds::ReturnCode::OK !=
            policy_decision_type_support_.register_type(
                *participant_, policy_decision_type_support_.get_type_name()))
    {
        std::cerr << "[vehicle_mock] Failed to register type: PolicyDecision" << std::endl;
        return false;
    }
    return true;
}

bool VehicleMockNode::create_topics()
{
    eprosima::safedds::dds::TopicQos topic_qos{};
    safety_input_frame_topic_name_ = eprosima::safedds::memory::container::StaticString256(
        common::topic_names::safety_input_frame());

    safety_input_frame_topic_ = participant_->create_topic(
        safety_input_frame_topic_name_,
        safety_input_frame_type_support_.get_type_name(),
        topic_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);

    if (nullptr == safety_input_frame_topic_)
    {
        std::cerr << "[vehicle_mock] Failed to create topic: safety_input_frame" << std::endl;
        return false;
    }

    policy_decision_topic_name_ = eprosima::safedds::memory::container::StaticString256(
        common::topic_names::policy_decision());

    policy_decision_topic_ = participant_->create_topic(
        policy_decision_topic_name_,
        policy_decision_type_support_.get_type_name(),
        topic_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);

    if (nullptr == policy_decision_topic_)
    {
        std::cerr << "[vehicle_mock] Failed to create topic: policy_decision" << std::endl;
        return false;
    }

    return true;
}

bool VehicleMockNode::create_endpoints()
{
    eprosima::safedds::dds::PublisherQos publisher_qos{};
    publisher_ = participant_->create_publisher(
        publisher_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);

    if (nullptr == publisher_)
    {
        std::cerr << "[vehicle_mock] Failed to create publisher" << std::endl;
        return false;
    }

    eprosima::safedds::dds::DataWriterQos writer_qos{};
    writer_qos.reliability().kind = eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    safety_input_frame_datawriter_ = publisher_->create_datawriter(
        *safety_input_frame_topic_,
        writer_qos,
        nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);

    if (nullptr == safety_input_frame_datawriter_)
    {
        std::cerr << "[vehicle_mock] Failed to create safety_input_frame datawriter" << std::endl;
        return false;
    }

    safety_input_frame_writer_ = eprosima::safedds::dds::TypedDataWriter<
        safe_edge::internal::SafetyInputFrameTypeSupport>::downcast(*safety_input_frame_datawriter_);

    if (nullptr == safety_input_frame_writer_)
    {
        std::cerr << "[vehicle_mock] Failed to downcast safety_input_frame writer" << std::endl;
        return false;
    }

    return true;
}

bool VehicleMockNode::create_subscriber()
{
    eprosima::safedds::dds::SubscriberQos sub_qos{};
    subscriber_ = participant_->create_subscriber(
        sub_qos, nullptr, eprosima::safedds::dds::NONE_STATUS_MASK);

    if (nullptr == subscriber_)
    {
        std::cerr << "[vehicle_mock] Failed to create subscriber" << std::endl;
        return false;
    }

    eprosima::safedds::dds::DataReaderQos reader_qos{};
    reader_qos.reliability().kind =
        eprosima::safedds::dds::ReliabilityQosPolicyKind::BEST_EFFORT_RELIABILITY_QOS;

    policy_decision_reader_ = subscriber_->create_datareader(
        *policy_decision_topic_,
        reader_qos,
        &policy_decision_listener_,
        eprosima::safedds::dds::DATA_AVAILABLE_STATUS);

    if (nullptr == policy_decision_reader_)
    {
        std::cerr << "[vehicle_mock] Failed to create policy_decision datareader" << std::endl;
        return false;
    }

    policy_decision_typed_ = eprosima::safedds::dds::TypedDataReader<
        safe_edge::internal::PolicyDecisionTypeSupport>::downcast(*policy_decision_reader_);

    if (nullptr == policy_decision_typed_)
    {
        std::cerr << "[vehicle_mock] Failed to downcast policy_decision reader" << std::endl;
        return false;
    }

    return true;
}

bool VehicleMockNode::enable_entities()
{
    bool enabled = true;
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == publisher_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == safety_input_frame_datawriter_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == subscriber_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == policy_decision_reader_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == participant_->enable());

    if (!enabled)
    {
        std::cerr << "[vehicle_mock] Failed to enable DDS entities" << std::endl;
    }

    return enabled;
}

bool VehicleMockNode::create_executor()
{
    executor_ = factory_.create_default_executor();

    if (nullptr == executor_)
    {
        std::cerr << "[vehicle_mock] Failed to create executor" << std::endl;
        return false;
    }

    return true;
}

void VehicleMockNode::publish_frame()
{
    const InputConfig cfg = read_input_file();

    const safe_edge::common::Header frame_header = header_factory_.make_header("safety_input_frame");

    safe_edge::internal::SafetyInputFrame frame{};
    frame.header = frame_header;
    frame.battery.header                 = frame_header;
    frame.battery.soc_pct                = cfg.soc;
    frame.battery.available_charge_kw    = cfg.available_charge_kw;
    frame.battery.available_discharge_kw = cfg.available_discharge_kw;
    frame.battery.v2g_ready              = cfg.v2g_ready;

    frame.safety.header             = frame_header;
    frame.safety.speed_mps          = cfg.speed_mps;
    frame.safety.braking_available  = cfg.braking_available;
    frame.safety.steering_available = cfg.steering_available;
    frame.safety.emergency_stop     = cfg.emergency_stop;
    frame.safety.adas_fault         = cfg.adas_fault;

    last_frame_      = frame;
    have_last_frame_ = true;

    const eprosima::safedds::execution::TimePoint t_pub =
        eprosima::safedds::get_platform().get_current_timepoint();

    if (eprosima::safedds::dds::ReturnCode::OK !=
            safety_input_frame_writer_->write(frame, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[vehicle_mock] Failed to publish SafetyInputFrame" << std::endl;
    }
    else
    {
        std::cout << "[vehicle_mock] Published SafetyInputFrame"
                  << " t_pub=" << t_pub.seconds << "." << t_pub.nanoseconds
                  << " soc=" << frame.battery.soc_pct
                  << " emergency_stop=" << frame.safety.emergency_stop << std::endl;
    }
}

void VehicleMockNode::on_policy_decision_received(
        const safe_edge::internal::PolicyDecision& decision,
        const eprosima::safedds::execution::TimePoint& t_rx) noexcept
{
    std::cout << "[vehicle_mock] Received PolicyDecision"
              << " t_rx_dec=" << t_rx.seconds << "." << t_rx.nanoseconds
              << " mode=" << static_cast<int32_t>(decision.mode) << std::endl;
}

void VehicleMockNode::republish_last_frame() noexcept
{
    if (nullptr == safety_input_frame_writer_ || !have_last_frame_)
    {
        return;
    }

    safety_input_frame_writer_->write(last_frame_, eprosima::safedds::dds::HANDLE_NIL);
    std::cout << "[vehicle_mock] Republished SafetyInputFrame on match soc=" << last_frame_.battery.soc_pct << std::endl;
}

} // namespace nodes
} // namespace safety_domain
} // namespace safe_edge
