#include <safe_edge/non_safety_domain/common/RuntimeConfig.hpp>
#include <safe_edge/non_safety_domain/nodes/InfotainmentNode.hpp>

int main()
{
    const safe_edge::non_safety_domain::common::RuntimeConfig config =
        safe_edge::non_safety_domain::common::make_infotainment_runtime_config();

    safe_edge::non_safety_domain::nodes::InfotainmentNode node(config);
    return node.run();
}
