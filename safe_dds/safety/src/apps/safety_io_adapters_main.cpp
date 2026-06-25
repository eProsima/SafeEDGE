#include <safe_edge/safety_domain/common/RuntimeConfig.hpp>
#include <safe_edge/safety_domain/nodes/SafetyIoAdaptersNode.hpp>

int main()
{
    const safe_edge::safety_domain::common::RuntimeConfig runtime_config =
        safe_edge::safety_domain::common::make_safety_io_adapters_runtime_config();
    safe_edge::safety_domain::nodes::SafetyIoAdaptersNode node(runtime_config);
    return node.run();
}
