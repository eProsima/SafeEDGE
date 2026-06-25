#include <safe_edge/non_safety_domain/common/TopicNames.hpp>

namespace safe_edge {
namespace non_safety_domain {
namespace common {
namespace topic_names {

const char* charger_locations() noexcept
{
    return "safe_edge.pilot_server.charger_locations";
}

const char* charger_types() noexcept
{
    return "safe_edge.pilot_server.charger_types";
}

const char* charging_sessions() noexcept
{
    return "safe_edge.pilot_server.charging_sessions";
}

const char* transit_health() noexcept
{
    return "safe_edge.pilot_server.transit_health";
}

const char* route_metrics() noexcept
{
    return "safe_edge.pilot_server.route_metrics";
}

const char* transit_metrics() noexcept
{
    return "safe_edge.pilot_server.transit_metrics";
}

const char* service_heartbeat() noexcept
{
    return "safe_edge.common.service_heartbeat";
}

const char* charging_query() noexcept
{
    return "safe_edge.internal.charging_query";
}

const char* charging_response() noexcept
{
    return "safe_edge.internal.charging_response";
}

const char* route_context_query() noexcept
{
    return "safe_edge.internal.route_context_query";
}

const char* route_context_response() noexcept
{
    return "safe_edge.internal.route_context_response";
}

const char* server_query() noexcept
{
    return "safe_edge.pilot_server.server_query";
}

const char* server_availability_status() noexcept
{
    return "safe_edge.internal.server_availability_status";
}

} // namespace topic_names
} // namespace common
} // namespace non_safety_domain
} // namespace safe_edge
