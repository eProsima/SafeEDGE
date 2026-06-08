#include <safe_edge/safety_domain/common/RuntimeConfig.hpp>

namespace safe_edge {
namespace safety_domain {
namespace common {

RuntimeConfig make_safety_io_adapters_runtime_config()
{
    RuntimeConfig config;
    config.participant_name = "SafeEdgeSafetyIoAdaptersParticipant";
    config.service_name = "safety_io_adapters";
    config.source_name = "safety_io_adapters";
    config.domain_id = 0U;
    config.participant_port = 8001U;
    config.initial_peer_port = 8002U;
    return config;
}

RuntimeConfig make_policy_engine_runtime_config()
{
    RuntimeConfig config;
    config.participant_name = "SafeEdgePolicyEngineParticipant";
    config.service_name = "policy_engine";
    config.source_name = "policy_engine";
    config.domain_id = 0U;
    config.participant_port = 8002U;
    config.initial_peer_port = 8001U;
    return config;
}

RuntimeConfig make_vehicle_mock_runtime_config()
{
    RuntimeConfig config;
    config.participant_name = "SafeEdgeVehicleMockParticipant";
    config.service_name = "vehicle_mock";
    config.source_name = "vehicle_mock";
    config.domain_id = 0U;
    config.participant_port = 8003U;
    config.initial_peer_port = 8001U;
    config.initial_peer_port_2 = 8002U;
    return config;
}

} // namespace common
} // namespace safety_domain
} // namespace safe_edge
