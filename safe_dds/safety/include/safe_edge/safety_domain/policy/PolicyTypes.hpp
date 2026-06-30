#ifndef SAFE_EDGE_SAFETY_DOMAIN_POLICY_POLICYTYPES_HPP
#define SAFE_EDGE_SAFETY_DOMAIN_POLICY_POLICYTYPES_HPP

#include <string>

namespace safe_edge {
namespace safety_domain {
namespace policy {

enum class PolicyModeValue
{
    unknown,
    nominal,
    low_soc,
    edge_autonomous,
    degraded_server_down,
    degraded_edge_down,
    degraded_complete
};

enum class GatewayHealth
{
    unknown,
    ok,
    degraded,
    error
};

struct AdvisoryInput
{
    bool available = false;
    PolicyModeValue suggested_mode = PolicyModeValue::unknown;
    std::string reason;
};

struct PolicyInputs
{
    float soc_pct = 0.0F;
    bool emergency_stop = false;
    bool adas_fault = false;
    GatewayHealth gateway_health = GatewayHealth::unknown;
    AdvisoryInput advisory;
    bool server_available = true;
    bool edge_available = true;
};

struct PolicyOutputs
{
    PolicyModeValue mode = PolicyModeValue::unknown;
    bool allow_non_safety = false;
    bool allow_ota = false;
    std::string reason;
};

} // namespace policy
} // namespace safety_domain
} // namespace safe_edge

#endif // SAFE_EDGE_SAFETY_DOMAIN_POLICY_POLICYTYPES_HPP
