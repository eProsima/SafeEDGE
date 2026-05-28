#include <safe_edge/edge_module/logic/EdgeAdvisor.hpp>

namespace safe_edge {
namespace edge_module {
namespace logic {

safe_edge::edge::EnergyAdvisory EdgeAdvisor::evaluate(
        const safe_edge::edge::VehicleEdgeSummary& summary,
        const safe_edge::pilot_server::ChargerLocation* chargers,
        int32_t charger_count)
{
    safe_edge::edge::EnergyAdvisory advisory;

    if (summary.soc_pct() < 20.0F)
    {
        advisory.suggested_mode(safe_edge::common::PolicyMode::POLICY_LOW_SOC);
        advisory.advisory_reason("Low battery -- charge now");
        advisory.recommended_charger_id((charger_count > 0) ? chargers[0].id() : 1);
        advisory.target_soc_pct(80.0F);
    }
    else if (summary.v2g_ready())
    {
        advisory.suggested_mode(safe_edge::common::PolicyMode::POLICY_EDGE_AUTONOMOUS);
        advisory.advisory_reason("V2G available");
        advisory.recommended_charger_id(0);
        advisory.target_soc_pct(90.0F);
    }
    else
    {
        advisory.suggested_mode(safe_edge::common::PolicyMode::POLICY_NOMINAL);
        advisory.advisory_reason("Normal operation");
        advisory.recommended_charger_id(0);
        advisory.target_soc_pct(80.0F);
    }

    return advisory;
}

} // namespace logic
} // namespace edge_module
} // namespace safe_edge
