#include <safe_edge/non_safety_domain/common/RuntimeConfig.hpp>

namespace safe_edge {
namespace non_safety_domain {
namespace common {

RuntimeConfig make_cloud_gateway_runtime_config()
{
    RuntimeConfig config;
    config.participant_name = "SafeEdgeCloudGatewayParticipant";
    config.service_name = "cloud_gateway";
    config.source_name = "cloud_gateway";
    config.domain_id = 0U;
    config.participant_port = 8011U;
    config.initial_peer_ports[0] = 8012U;
    config.initial_peer_ports[1] = 8013U;
    config.initial_peer_ports[2] = 8002U;
    config.initial_peer_count = 3U;
    return config;
}

RuntimeConfig make_ota_service_runtime_config()
{
    RuntimeConfig config;
    config.participant_name = "SafeEdgeOtaServiceParticipant";
    config.service_name = "ota_service";
    config.source_name = "ota_service";
    config.domain_id = 0U;
    config.participant_port = 8012U;
    config.initial_peer_ports[0] = 8011U;
    config.initial_peer_ports[1] = 8013U;
    config.initial_peer_count = 2U;
    return config;
}

RuntimeConfig make_infotainment_runtime_config()
{
    RuntimeConfig config;
    config.participant_name = "SafeEdgeInfotainmentParticipant";
    config.service_name = "infotainment";
    config.source_name = "infotainment";
    config.domain_id = 0U;
    config.participant_port = 8013U;
    config.initial_peer_ports[0] = 8011U;
    config.initial_peer_ports[1] = 8012U;
    config.initial_peer_ports[2] = 8001U;
    config.initial_peer_ports[3] = 8002U;
    config.initial_peer_count = 4U;
    return config;
}

} // namespace common
} // namespace non_safety_domain
} // namespace safe_edge
