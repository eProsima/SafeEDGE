#include <safe_edge/safety_domain/common/RuntimeConfig.hpp>
#include <safe_edge/safety_domain/nodes/VehicleMockNode.hpp>

int main()
{
    const safe_edge::safety_domain::common::RuntimeConfig runtime_config =
        safe_edge::safety_domain::common::make_vehicle_mock_runtime_config();
    safe_edge::safety_domain::nodes::VehicleMockNode node(runtime_config);
    return node.run();
}
