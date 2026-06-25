#include <safe_edge/safety_domain/common/TopicNames.hpp>

namespace safe_edge {
namespace safety_domain {
namespace common {
namespace topic_names {

const char* safety_input_frame() noexcept
{
    return "safe_edge.internal.safety_input_frame";
}

const char* policy_decision() noexcept
{
    return "safe_edge.internal.policy_decision";
}

const char* service_heartbeat() noexcept
{
    return "safe_edge.common.service_heartbeat";
}

const char* energy_advisory() noexcept
{
    return "safe_edge.edge.energy_advisory";
}

const char* edge_gateway_status() noexcept
{
    return "safe_edge.edge.edge_gateway_status";
}

const char* vehicle_edge_summary() noexcept
{
    return "safe_edge.edge.vehicle_edge_summary";
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

const char* edge_charger_query() noexcept
{
    return "safe_edge.internal.edge_charger_query";
}

const char* edge_charger_response() noexcept
{
    return "safe_edge.internal.edge_charger_response";
}

const char* server_availability_status() noexcept
{
    return "safe_edge.internal.server_availability_status";
}

} // namespace topic_names
} // namespace common
} // namespace safety_domain
} // namespace safe_edge
