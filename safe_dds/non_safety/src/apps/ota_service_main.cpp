#include <safe_edge/non_safety_domain/common/RuntimeConfig.hpp>
#include <safe_edge/non_safety_domain/nodes/OtaServiceNode.hpp>

int main()
{
    const safe_edge::non_safety_domain::common::RuntimeConfig config =
        safe_edge::non_safety_domain::common::make_ota_service_runtime_config();

    safe_edge::non_safety_domain::nodes::OtaServiceNode node(config);
    return node.run();
}
