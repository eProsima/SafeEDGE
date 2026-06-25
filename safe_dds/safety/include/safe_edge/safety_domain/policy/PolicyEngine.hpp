#ifndef SAFE_EDGE_SAFETY_DOMAIN_POLICY_POLICYENGINE_HPP
#define SAFE_EDGE_SAFETY_DOMAIN_POLICY_POLICYENGINE_HPP

#include <safe_edge/safety_domain/policy/PolicyTypes.hpp>

namespace safe_edge {
namespace safety_domain {
namespace policy {

class PolicyEngine
{
public:

    PolicyOutputs evaluate(
            const PolicyInputs& inputs) const noexcept;
};

} // namespace policy
} // namespace safety_domain
} // namespace safe_edge

#endif // SAFE_EDGE_SAFETY_DOMAIN_POLICY_POLICYENGINE_HPP
