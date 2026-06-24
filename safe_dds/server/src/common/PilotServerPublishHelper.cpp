#include <safe_edge/server/common/PilotServerPublishHelper.hpp>

#include <safedds/dds/InstanceHandle.hpp>

#include <iostream>

namespace safe_edge {
namespace server {
namespace common {

void PilotServerPublishHelper::publish_charger_locations(
        eprosima::safedds::dds::TypedDataWriter<
            safe_edge::pilot_server::ChargerLocationTypeSupport>& writer,
        const std::vector<ParsedChargerLocation>& parsed)
{
    for (size_t i = 0U; i < parsed.size(); ++i)
    {
        safe_edge::pilot_server::ChargerLocation loc;
        loc.id                 = parsed[i].id;
        loc.name               = parsed[i].name.c_str();
        loc.position.latitude  = parsed[i].latitude;
        loc.position.longitude = parsed[i].longitude;

        if (eprosima::safedds::dds::ReturnCode::OK !=
                writer.write(loc, eprosima::safedds::dds::HANDLE_NIL))
        {
            std::cerr << "[publish_helper] Failed to write ChargerLocation id="
                      << parsed[i].id << std::endl;
        }
    }
}

} // namespace common
} // namespace server
} // namespace safe_edge
