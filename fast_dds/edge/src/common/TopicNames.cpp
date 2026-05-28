#include <safe_edge/edge_module/common/TopicNames.hpp>

namespace safe_edge {
namespace edge_module {
namespace common {
namespace topic_names {

const char* vehicle_edge_summary() noexcept
{
    return "safe_edge.edge.vehicle_edge_summary";
}

const char* energy_advisory() noexcept
{
    return "safe_edge.edge.energy_advisory";
}

const char* edge_gateway_status() noexcept
{
    return "safe_edge.edge.edge_gateway_status";
}

const char* charger_locations() noexcept
{
    return "safe_edge.pilot_server.charger_locations";
}

const char* service_heartbeat() noexcept
{
    return "safe_edge.common.service_heartbeat";
}

} // namespace topic_names
} // namespace common
} // namespace edge_module
} // namespace safe_edge
