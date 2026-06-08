#include <safe_edge/non_safety_domain/common/RuntimeConfig.hpp>
#include <safe_edge/non_safety_domain/nodes/CloudGatewayNode.hpp>

int main()
{
    const safe_edge::non_safety_domain::common::RuntimeConfig config =
        safe_edge::non_safety_domain::common::make_cloud_gateway_runtime_config();

    safe_edge::non_safety_domain::nodes::CloudGatewayNode node(config);
    return node.run();
}
