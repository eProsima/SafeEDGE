#ifndef SAFE_EDGE_SAFETY_DOMAIN_POLICY_IDLADAPTERS_HPP
#define SAFE_EDGE_SAFETY_DOMAIN_POLICY_IDLADAPTERS_HPP

#include <safe_edge/safety_domain/policy/PolicyTypes.hpp>

#include <common.hpp>
#include <edge.hpp>
#include <internal.hpp>

namespace safe_edge {
namespace safety_domain {
namespace policy {

PolicyModeValue from_idl_policy_mode(
        safe_edge::common::PolicyMode mode) noexcept;

safe_edge::common::PolicyMode to_idl_policy_mode(
        PolicyModeValue mode) noexcept;

GatewayHealth from_idl_health_status(
        safe_edge::common::HealthStatus status) noexcept;

safe_edge::common::HealthStatus to_idl_health_status(
        GatewayHealth health) noexcept;

PolicyInputs to_policy_inputs(
        const safe_edge::internal::SafetyInputFrame& frame,
        const safe_edge::edge::EnergyAdvisory* advisory,
        const safe_edge::edge::EdgeGatewayStatus* gateway_status,
        bool server_available,
        bool edge_available) noexcept;

safe_edge::internal::PolicyDecision to_policy_decision(
        const PolicyOutputs& outputs,
        const safe_edge::common::Header& header);

safe_edge::edge::VehicleEdgeSummary to_vehicle_edge_summary(
        const safe_edge::internal::SafetyInputFrame& frame,
        const safe_edge::internal::PolicyDecision& decision,
        const safe_edge::common::Header& header) noexcept;

} // namespace policy
} // namespace safety_domain
} // namespace safe_edge

#endif // SAFE_EDGE_SAFETY_DOMAIN_POLICY_IDLADAPTERS_HPP
