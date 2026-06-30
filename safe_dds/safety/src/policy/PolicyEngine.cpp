#include <safe_edge/safety_domain/policy/PolicyEngine.hpp>

namespace safe_edge {
namespace safety_domain {
namespace policy {

PolicyOutputs PolicyEngine::evaluate(
        const PolicyInputs& inputs) const noexcept
{
    PolicyOutputs outputs;

    if (inputs.emergency_stop || inputs.adas_fault)
    {
        outputs.mode = PolicyModeValue::edge_autonomous;
        outputs.reason = "emergency_stop_or_adas_fault";
    }
    else if (!inputs.server_available && !inputs.edge_available)
    {
        outputs.mode = PolicyModeValue::degraded_complete;
        outputs.reason = "server_down_edge_unavailable";
    }
    else if (!inputs.server_available && inputs.edge_available)
    {
        outputs.mode = PolicyModeValue::degraded_server_down;
        outputs.reason = "server_down_edge_available";
    }
    else if (!inputs.edge_available)
    {
        outputs.mode = PolicyModeValue::degraded_edge_down;
        outputs.reason = "edge_unavailable";
    }
    else if (inputs.soc_pct < 20.0F)
    {
        outputs.mode = PolicyModeValue::low_soc;
        outputs.reason = "battery_soc_below_threshold";
    }
    else if (inputs.advisory.available && inputs.advisory.suggested_mode != PolicyModeValue::unknown)
    {
        outputs.mode = inputs.advisory.suggested_mode;
        outputs.reason = inputs.advisory.reason.empty() ? "edge_advisory" : inputs.advisory.reason;
    }
    else
    {
        outputs.mode = PolicyModeValue::nominal;
        outputs.reason = "nominal_vehicle_state";
    }

    outputs.allow_non_safety = outputs.mode == PolicyModeValue::nominal;
    outputs.allow_ota = outputs.mode == PolicyModeValue::nominal && inputs.gateway_health == GatewayHealth::ok;

    return outputs;
}

} // namespace policy
} // namespace safety_domain
} // namespace safe_edge
