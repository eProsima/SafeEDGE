#ifndef SAFE_EDGE_EDGE_MODULE_COMMON_TOPICNAMES_HPP
#define SAFE_EDGE_EDGE_MODULE_COMMON_TOPICNAMES_HPP

namespace safe_edge {
namespace edge_module {
namespace common {
namespace topic_names {

const char* vehicle_edge_summary() noexcept;
const char* energy_advisory() noexcept;
const char* edge_gateway_status() noexcept;
const char* charger_locations() noexcept;
const char* service_heartbeat() noexcept;

} // namespace topic_names
} // namespace common
} // namespace edge_module
} // namespace safe_edge

#endif // SAFE_EDGE_EDGE_MODULE_COMMON_TOPICNAMES_HPP
