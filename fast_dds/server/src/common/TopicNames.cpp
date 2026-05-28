#include <safe_edge/server/common/TopicNames.hpp>

namespace safe_edge {
namespace server {
namespace common {
namespace topic_names {

const char* charger_locations() noexcept
{
    return "safe_edge.pilot_server.charger_locations";
}

const char* server_query() noexcept
{
    return "safe_edge.pilot_server.server_query";
}

const char* service_heartbeat() noexcept
{
    return "safe_edge.common.service_heartbeat";
}

} // namespace topic_names
} // namespace common
} // namespace server
} // namespace safe_edge
