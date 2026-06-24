#include <safe_edge/server/common/RuntimeConfig.hpp>

namespace safe_edge {
namespace server {
namespace common {

RuntimeConfig make_server_runtime_config()
{
    RuntimeConfig config;
    config.participant_name = "SafeEdgeServerParticipant";
    config.domain_id = 0U;
    config.participant_port = 8020U;

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
