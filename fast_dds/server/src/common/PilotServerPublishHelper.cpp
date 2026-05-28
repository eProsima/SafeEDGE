#include <safe_edge/server/common/PilotServerPublishHelper.hpp>

#include <common.hpp>
#include <pilot_server.hpp>

#include <fastdds/dds/core/ReturnCode.hpp>

#include <iostream>

namespace safe_edge {
namespace server {
namespace common {

void PilotServerPublishHelper::publish_charger_locations(
        eprosima::fastdds::dds::DataWriter& writer,
        const std::vector<ParsedChargerLocation>& parsed)
{
    for (const auto& p : parsed)
    {
        safe_edge::pilot_server::ChargerLocation loc;
        loc.id(p.id);
        loc.name(p.name);
        safe_edge::common::GeoPoint pos;
        pos.latitude(p.latitude);
        pos.longitude(p.longitude);
        loc.position(pos);

        if (eprosima::fastdds::dds::RETCODE_OK !=
                writer.write(&loc, eprosima::fastdds::dds::HANDLE_NIL))
        {
            std::cerr << "[publish_helper] Failed to write ChargerLocation id="
                      << p.id << std::endl;
        }
    }
}

} // namespace common
} // namespace server
} // namespace safe_edge
