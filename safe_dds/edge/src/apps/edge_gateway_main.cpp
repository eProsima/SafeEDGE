#include <safe_edge/edge_module/common/RuntimeConfig.hpp>
#include <safe_edge/edge_module/nodes/EdgeGatewayNode.hpp>

int main()
{
    const safe_edge::edge_module::common::RuntimeConfig config =
        safe_edge::edge_module::common::make_edge_gateway_runtime_config();

    safe_edge::edge_module::nodes::EdgeGatewayNode node(config);
    return node.run();
}
