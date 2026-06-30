#ifndef SAFE_EDGE_NON_SAFETY_DOMAIN_COMMON_TOPICNAMES_HPP
#define SAFE_EDGE_NON_SAFETY_DOMAIN_COMMON_TOPICNAMES_HPP

namespace safe_edge {
namespace non_safety_domain {
namespace common {
namespace topic_names {

const char* charger_locations() noexcept;
const char* charger_types() noexcept;
const char* charging_sessions() noexcept;
const char* transit_health() noexcept;
const char* route_metrics() noexcept;
const char* transit_metrics() noexcept;
const char* service_heartbeat() noexcept;
const char* charging_query() noexcept;
const char* charging_response() noexcept;
const char* route_context_query() noexcept;
const char* route_context_response() noexcept;
const char* server_query() noexcept;
const char* server_availability_status() noexcept;
const char* policy_decision() noexcept;

} // namespace topic_names
} // namespace common
} // namespace non_safety_domain
} // namespace safe_edge

#endif // SAFE_EDGE_NON_SAFETY_DOMAIN_COMMON_TOPICNAMES_HPP
