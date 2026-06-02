#ifndef SAFE_EDGE_SERVER_COMMON_PILOTSERVERPAYLOADPARSER_HPP
#define SAFE_EDGE_SERVER_COMMON_PILOTSERVERPAYLOADPARSER_HPP

#include <string>
#include <vector>

namespace safe_edge {
namespace server {
namespace common {

// Neutral parsed representation — no DDS-stack types.
struct ParsedChargerLocation
{
    int         id        = 0;
    std::string name;
    float       latitude  = 0.0F;
    float       longitude = 0.0F;
};

class PilotServerPayloadParser
{
public:

    static std::vector<ParsedChargerLocation> parse_charger_locations(
            const std::string& body);
};

} // namespace common
} // namespace server
} // namespace safe_edge

#endif // SAFE_EDGE_SERVER_COMMON_PILOTSERVERPAYLOADPARSER_HPP
