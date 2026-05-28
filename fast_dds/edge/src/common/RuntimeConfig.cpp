#include <safe_edge/edge_module/common/RuntimeConfig.hpp>

namespace safe_edge {
namespace edge_module {
namespace common {

RuntimeConfig make_edge_gateway_runtime_config()
{
    RuntimeConfig config;
    config.participant_name = "SafeEdgeEdgeGatewayParticipant";
    config.service_name = "edge_gateway";
    config.source_name = "edge_gateway";
    config.domain_id = 0U;
    config.participant_port = 8030U;
    config.status_interval_sec = 5U;
    return config;
}

} // namespace common
} // namespace edge_module
} // namespace safe_edge
