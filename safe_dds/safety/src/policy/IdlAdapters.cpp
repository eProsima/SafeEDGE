#include <safe_edge/safety_domain/policy/IdlAdapters.hpp>

namespace safe_edge {
namespace safety_domain {
namespace policy {

namespace {

safe_edge::common::HealthStatus infer_vehicle_health(
        const safe_edge::internal::SafetyInputFrame& frame) noexcept
{
    if (frame.safety.emergency_stop || frame.safety.adas_fault)
    {
        return safe_edge::common::HealthStatus::HEALTH_ERROR;
    }

    return safe_edge::common::HealthStatus::HEALTH_OK;
}

} // namespace

PolicyModeValue from_idl_policy_mode(
        safe_edge::common::PolicyMode mode) noexcept
{
    switch (mode)
    {
        case safe_edge::common::PolicyMode::POLICY_NOMINAL:
            return PolicyModeValue::nominal;
        case safe_edge::common::PolicyMode::POLICY_LOW_SOC:
            return PolicyModeValue::low_soc;
        case safe_edge::common::PolicyMode::POLICY_EDGE_AUTONOMOUS:
            return PolicyModeValue::edge_autonomous;
        case safe_edge::common::PolicyMode::POLICY_DEGRADED_SERVER_DOWN:
            return PolicyModeValue::degraded_server_down;
        case safe_edge::common::PolicyMode::POLICY_DEGRADED_COMPLETE:
            return PolicyModeValue::degraded_complete;
        case safe_edge::common::PolicyMode::POLICY_UNKNOWN:
        default:
            return PolicyModeValue::unknown;
    }
}

safe_edge::common::PolicyMode to_idl_policy_mode(
        PolicyModeValue mode) noexcept
{
    switch (mode)
    {
        case PolicyModeValue::nominal:
            return safe_edge::common::PolicyMode::POLICY_NOMINAL;
        case PolicyModeValue::low_soc:
            return safe_edge::common::PolicyMode::POLICY_LOW_SOC;
        case PolicyModeValue::edge_autonomous:
            return safe_edge::common::PolicyMode::POLICY_EDGE_AUTONOMOUS;
        case PolicyModeValue::degraded_server_down:
            return safe_edge::common::PolicyMode::POLICY_DEGRADED_SERVER_DOWN;
        case PolicyModeValue::degraded_complete:
            return safe_edge::common::PolicyMode::POLICY_DEGRADED_COMPLETE;
        case PolicyModeValue::unknown:
        default:
            return safe_edge::common::PolicyMode::POLICY_UNKNOWN;
    }
}

GatewayHealth from_idl_health_status(
        safe_edge::common::HealthStatus status) noexcept
{
    switch (status)
    {
        case safe_edge::common::HealthStatus::HEALTH_OK:
            return GatewayHealth::ok;
        case safe_edge::common::HealthStatus::HEALTH_DEGRADED:
            return GatewayHealth::degraded;
        case safe_edge::common::HealthStatus::HEALTH_ERROR:
            return GatewayHealth::error;
        case safe_edge::common::HealthStatus::HEALTH_UNKNOWN:
        default:
            return GatewayHealth::unknown;
    }
}

safe_edge::common::HealthStatus to_idl_health_status(
        GatewayHealth health) noexcept
{
    switch (health)
    {
        case GatewayHealth::ok:
            return safe_edge::common::HealthStatus::HEALTH_OK;
        case GatewayHealth::degraded:
            return safe_edge::common::HealthStatus::HEALTH_DEGRADED;
        case GatewayHealth::error:
            return safe_edge::common::HealthStatus::HEALTH_ERROR;
        case GatewayHealth::unknown:
        default:
            return safe_edge::common::HealthStatus::HEALTH_UNKNOWN;
    }
}

PolicyInputs to_policy_inputs(
        const safe_edge::internal::SafetyInputFrame& frame,
        const safe_edge::edge::EnergyAdvisory* advisory,
        const safe_edge::edge::EdgeGatewayStatus* gateway_status,
        bool server_available,
        bool edge_available) noexcept
{
    PolicyInputs inputs;
    inputs.soc_pct = frame.battery.soc_pct;
    inputs.emergency_stop = frame.safety.emergency_stop;
    inputs.adas_fault = frame.safety.adas_fault;
    inputs.server_available = server_available;
    inputs.edge_available = edge_available;

    if (nullptr != advisory)
    {
        inputs.advisory.available = true;
        inputs.advisory.suggested_mode = from_idl_policy_mode(advisory->suggested_mode);
        inputs.advisory.reason = advisory->advisory_reason;
    }

    if (nullptr != gateway_status)
    {
        inputs.gateway_health = from_idl_health_status(gateway_status->status);
    }

    return inputs;
}

safe_edge::internal::PolicyDecision to_policy_decision(
        const PolicyOutputs& outputs,
        const safe_edge::common::Header& header)
{
    safe_edge::internal::PolicyDecision decision;
    decision.header = header;
    decision.mode = to_idl_policy_mode(outputs.mode);
    decision.reason = outputs.reason;
    decision.allow_non_safety = outputs.allow_non_safety;
    decision.allow_ota = outputs.allow_ota;
    return decision;
}

safe_edge::edge::VehicleEdgeSummary to_vehicle_edge_summary(
        const safe_edge::internal::SafetyInputFrame& frame,
        const safe_edge::internal::PolicyDecision& decision,
        const safe_edge::common::Header& header) noexcept
{
    safe_edge::edge::VehicleEdgeSummary summary;
    summary.header = header;
    summary.soc_pct = frame.battery.soc_pct;
    summary.current_mode = decision.mode;
    summary.vehicle_health = infer_vehicle_health(frame);
    summary.v2g_ready = frame.battery.v2g_ready;
    return summary;
}

} // namespace policy
} // namespace safety_domain
} // namespace safe_edge
