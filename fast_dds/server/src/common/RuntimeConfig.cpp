#include <safe_edge/server/common/RuntimeConfig.hpp>

#include <cstdlib>
#include <cstring>
#include <sstream>

namespace safe_edge {
namespace server {
namespace common {

RuntimeConfig make_server_runtime_config()
{
    RuntimeConfig config;
    config.participant_name = "SafeEdgeServerParticipant";
    config.domain_id = 0U;
    config.participant_port = 8020U;
    if (const char* v = std::getenv("SAFE_EDGE_OWN_IP"))       { config.own_ip       = v; }
    if (const char* v = std::getenv("SAFE_EDGE_NON_SAFETY_IP")) { config.non_safety_ip = v; }
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

    config.pilot_server_base_url       = "https://pilot2.dumitru-alexandru.work";
    // api_key moved to /etc/safe-edge/server.ini — do not hardcode here
    config.charger_locations_endpoint  = "/api/chargers/locations";
    config.charger_types_endpoint      = "/api/chargers/types";
    config.charging_sessions_endpoint  = "/api/chargers/sessions";
    config.transit_health_endpoint     = "/api/transit/health";
    config.transit_metrics_endpoint    = "/api/transit/metrics";

    return config;
}

} // namespace common
} // namespace server
} // namespace safe_edge
