#ifndef SAFE_EDGE_SAFETY_DOMAIN_COMMON_RUNTIMECONFIG_HPP
#define SAFE_EDGE_SAFETY_DOMAIN_COMMON_RUNTIMECONFIG_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include <safedds/transport/Locator.hpp>

namespace safe_edge {
namespace safety_domain {
namespace common {

struct RuntimeConfig
{
    std::string participant_name;
    std::string service_name;
    std::string source_name;
    uint32_t domain_id = 0U;
    uint16_t participant_port = 0U;
    eprosima::safedds::transport::Locator::IPv4 own_ip = {127, 0, 0, 1};
    eprosima::safedds::transport::Locator::IPv4 cross_domain_peer_ip = {127, 0, 0, 1};
    uint16_t initial_peer_ports[5] = {};
    std::size_t initial_peer_count = 0U;
};

RuntimeConfig make_safety_io_adapters_runtime_config();

RuntimeConfig make_policy_engine_runtime_config();

RuntimeConfig make_vehicle_mock_runtime_config();

} // namespace common
} // namespace safety_domain
} // namespace safe_edge

#endif // SAFE_EDGE_SAFETY_DOMAIN_COMMON_RUNTIMECONFIG_HPP
