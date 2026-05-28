#ifndef SAFE_EDGE_SERVER_COMMON_RUNTIMECONFIG_HPP
#define SAFE_EDGE_SERVER_COMMON_RUNTIMECONFIG_HPP

#include <cstdint>
#include <string>

namespace safe_edge {
namespace server {
namespace common {

struct RuntimeConfig
{
    std::string participant_name;
    uint32_t domain_id = 0U;
    uint16_t participant_port = 0U;

    std::string pilot_server_base_url;
    std::string pilot_server_api_key;
    std::string charger_locations_endpoint;
    std::string charger_types_endpoint;
    std::string charging_sessions_endpoint;
    std::string transit_health_endpoint;
    std::string transit_metrics_endpoint;
};

RuntimeConfig make_server_runtime_config();

} // namespace common
} // namespace server
} // namespace safe_edge

#endif // SAFE_EDGE_SERVER_COMMON_RUNTIMECONFIG_HPP
