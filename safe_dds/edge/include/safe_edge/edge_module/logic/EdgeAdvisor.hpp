#ifndef SAFE_EDGE_EDGE_MODULE_LOGIC_EDGEADVISOR_HPP
#define SAFE_EDGE_EDGE_MODULE_LOGIC_EDGEADVISOR_HPP

#include <edge.hpp>
#include <pilot_server.hpp>

namespace safe_edge {
namespace edge_module {
namespace logic {

class EdgeAdvisor
{
public:

    static safe_edge::edge::EnergyAdvisory evaluate(
            const safe_edge::edge::VehicleEdgeSummary& summary,
            const safe_edge::pilot_server::ChargerLocation* chargers,
            int32_t charger_count);
};

} // namespace logic
} // namespace edge_module
} // namespace safe_edge

#endif // SAFE_EDGE_EDGE_MODULE_LOGIC_EDGEADVISOR_HPP
