#ifndef SAFE_EDGE_EDGE_MODULE_COMMON_RUNTIMECONFIG_HPP
#define SAFE_EDGE_EDGE_MODULE_COMMON_RUNTIMECONFIG_HPP

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace safe_edge {
namespace edge_module {
namespace common {

struct RuntimeConfig
{
    std::string participant_name;
    std::string service_name;
    std::string source_name;
    uint32_t domain_id = 0U;
    uint16_t participant_port = 0U;
    uint32_t status_interval_sec = 5U;
    std::string own_ip = "127.0.0.1";
    std::string safety_ip = "127.0.0.1";
    std::vector<std::pair<std::string, uint16_t>> initial_peers;
};

RuntimeConfig make_edge_gateway_runtime_config();

} // namespace common
} // namespace edge_module
} // namespace safe_edge

#endif // SAFE_EDGE_EDGE_MODULE_COMMON_RUNTIMECONFIG_HPP
