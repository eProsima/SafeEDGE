#include <safe_edge/server/common/RuntimeConfig.hpp>
#include <safe_edge/server/nodes/ServerNode.hpp>

int main()
{
    const safe_edge::server::common::RuntimeConfig config =
        safe_edge::server::common::make_server_runtime_config();

    safe_edge::server::nodes::ServerNode node(config);
    return node.run();
}
