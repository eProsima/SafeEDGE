#ifndef SAFE_EDGE_SERVER_COMMON_PILOTSERVERPUBLISHHELPER_HPP
#define SAFE_EDGE_SERVER_COMMON_PILOTSERVERPUBLISHHELPER_HPP

#include <safe_edge/server/common/PilotServerPayloadParser.hpp>

#include <fastdds/dds/publisher/DataWriter.hpp>

#include <vector>

namespace safe_edge {
namespace server {
namespace common {

struct PilotServerPublishHelper
{
    static void publish_charger_locations(
            eprosima::fastdds::dds::DataWriter& writer,
            const std::vector<ParsedChargerLocation>& parsed);
};

} // namespace common
} // namespace server
} // namespace safe_edge

#endif // SAFE_EDGE_SERVER_COMMON_PILOTSERVERPUBLISHHELPER_HPP
