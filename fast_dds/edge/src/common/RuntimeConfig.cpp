#include <safe_edge/edge_module/common/RuntimeConfig.hpp>

#include <cstdlib>
#include <sstream>

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
    if (const char* v = std::getenv("SAFE_EDGE_OWN_IP"))    { config.own_ip    = v; }
    if (const char* v = std::getenv("SAFE_EDGE_SAFETY_IP")) { config.safety_ip = v; }
    if (const char* v = std::getenv("SAFE_EDGE_INITIAL_PEERS"))
    {
        std::istringstream ss(v);
        std::string token;
        while (std::getline(ss, token, ','))
        {
            const auto sep = token.rfind(':');
            if (sep != std::string::npos)
            {
                config.initial_peers.emplace_back(
                    token.substr(0, sep),
                    static_cast<uint16_t>(std::stoul(token.substr(sep + 1))));
            }
        }
    }
    return config;
}

} // namespace common
} // namespace edge_module
} // namespace safe_edge
