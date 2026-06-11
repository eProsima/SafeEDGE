#ifndef SAFE_EDGE_NON_SAFETY_DOMAIN_COMMON_RUNTIMECONFIG_HPP
#define SAFE_EDGE_NON_SAFETY_DOMAIN_COMMON_RUNTIMECONFIG_HPP

#include <cstdint>
#include <cstddef>
#include <string>

#include <safedds/transport/Locator.hpp>

namespace safe_edge {
namespace non_safety_domain {
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
    eprosima::safedds::transport::Locator initial_peer_locators[8] = {};
    std::size_t initial_peer_locator_count = 0U;
};

RuntimeConfig make_cloud_gateway_runtime_config();
RuntimeConfig make_ota_service_runtime_config();
RuntimeConfig make_infotainment_runtime_config();

} // namespace common
} // namespace non_safety_domain
} // namespace safe_edge

#endif // SAFE_EDGE_NON_SAFETY_DOMAIN_COMMON_RUNTIMECONFIG_HPP
