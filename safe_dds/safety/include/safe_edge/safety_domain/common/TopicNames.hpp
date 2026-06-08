#ifndef SAFE_EDGE_SAFETY_DOMAIN_COMMON_TOPICNAMES_HPP
#define SAFE_EDGE_SAFETY_DOMAIN_COMMON_TOPICNAMES_HPP

namespace safe_edge {
namespace safety_domain {
namespace common {
namespace topic_names {

const char* safety_input_frame() noexcept;

const char* policy_decision() noexcept;

const char* service_heartbeat() noexcept;

const char* energy_advisory() noexcept;

const char* edge_gateway_status() noexcept;

const char* vehicle_edge_summary() noexcept;

const char* charging_query() noexcept;

const char* charging_response() noexcept;

const char* route_context_query() noexcept;

const char* route_context_response() noexcept;

const char* edge_charger_query() noexcept;

const char* edge_charger_response() noexcept;

const char* server_availability_status() noexcept;

} // namespace topic_names
} // namespace common
} // namespace safety_domain
} // namespace safe_edge

#endif // SAFE_EDGE_SAFETY_DOMAIN_COMMON_TOPICNAMES_HPP
