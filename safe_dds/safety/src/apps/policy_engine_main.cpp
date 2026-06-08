#include <safe_edge/safety_domain/common/RuntimeConfig.hpp>
#include <safe_edge/safety_domain/nodes/PolicyEngineNode.hpp>

int main()
{
    const safe_edge::safety_domain::common::RuntimeConfig runtime_config =
        safe_edge::safety_domain::common::make_policy_engine_runtime_config();
    safe_edge::safety_domain::nodes::PolicyEngineNode node(runtime_config);
    return node.run();
}
