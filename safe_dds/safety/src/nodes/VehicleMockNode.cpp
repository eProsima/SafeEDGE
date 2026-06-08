#include <safe_edge/safety_domain/nodes/VehicleMockNode.hpp>

#include <safe_edge/safety_domain/common/TopicNames.hpp>

#include <safedds/dds/Publisher.hpp>
#include <safedds/dds/ReturnCode.hpp>
#include <safedds/dds/qos/DataWriterQos.hpp>
#include <safedds/dds/qos/DomainParticipantQos.hpp>
#include <safedds/dds/qos/PublisherQos.hpp>
#include <safedds/dds/qos/TopicQos.hpp>
#include <safedds/execution/TimePoint.hpp>
#include <safedds/transport.hpp>

#include <cstdio>
#include <iostream>

namespace safe_edge {
namespace safety_domain {
namespace nodes {

namespace {

constexpr eprosima::safedds::execution::TimePeriod TIMEOUT = {1, 0};

static constexpr const char* INPUT_FILE_PATH = "/data/safe-edge-stage2/input.txt";

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

    std::FILE* f = std::fopen(INPUT_FILE_PATH, "r");
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

VehicleMockNode::VehicleMockNode(
        const common::RuntimeConfig& runtime_config)
    : runtime_config_(runtime_config)
    , header_factory_(runtime_config.source_name)
    , participant_listener_(*this)
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

        executor_->spin(publish_timer_.next_trigger());
    }

    return 0;
}

bool VehicleMockNode::initialize()
{
    return create_participant() &&
           register_types() &&
           create_topics() &&
           create_endpoints() &&
           enable_entities() &&
           create_executor();
}

bool VehicleMockNode::create_participant()
{
    eprosima::safedds::dds::DomainParticipantQos participant_qos{};
    eprosima::safedds::memory::container::StaticString256 participant_name(runtime_config_.participant_name.c_str());
    participant_qos.participant_name() = participant_name;
    participant_qos.wire_protocol_qos().announced_locator = eprosima::safedds::transport::Locator::from_ipv4(
        {127, 0, 0, 1},
        runtime_config_.participant_port);

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

bool VehicleMockNode::enable_entities()
{
    bool enabled = true;
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == publisher_->enable());
    enabled = enabled && (eprosima::safedds::dds::ReturnCode::OK == safety_input_frame_datawriter_->enable());
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

    if (eprosima::safedds::dds::ReturnCode::OK !=
            safety_input_frame_writer_->write(frame, eprosima::safedds::dds::HANDLE_NIL))
    {
        std::cerr << "[vehicle_mock] Failed to publish SafetyInputFrame" << std::endl;
    }
    else
    {
        std::cout << "[vehicle_mock] Published SafetyInputFrame soc=" << frame.battery.soc_pct << std::endl;
    }
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
