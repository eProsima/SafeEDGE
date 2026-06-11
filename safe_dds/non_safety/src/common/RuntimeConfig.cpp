#include <safe_edge/non_safety_domain/common/RuntimeConfig.hpp>

#include <cstdlib>
#include <sstream>

namespace safe_edge {
namespace non_safety_domain {
namespace common {

namespace {

void resolve_ip_from_env(
        const char* env_name,
        eprosima::safedds::transport::Locator::IPv4& out,
        const eprosima::safedds::transport::Locator::IPv4& fallback) noexcept
{
    out[0] = fallback[0];
    out[1] = fallback[1];
    out[2] = fallback[2];
    out[3] = fallback[3];

    const char* value = std::getenv(env_name);
    if (nullptr == value || '\0' == value[0])
    {
        return;
    }

    std::stringstream ss(value);
    int octet[4] = {};
    char dot1 = '\0';
    char dot2 = '\0';
    char dot3 = '\0';
    if (!(ss >> octet[0] >> dot1 >> octet[1] >> dot2 >> octet[2] >> dot3 >> octet[3]))
    {
        return;
    }
    if (dot1 != '.' || dot2 != '.' || dot3 != '.')
    {
        return;
    }
    for (int i = 0; i < 4; ++i)
    {
        if (octet[i] < 0 || octet[i] > 255)
        {
            return;
        }
        out[i] = static_cast<uint8_t>(octet[i]);
    }
}

void parse_initial_peers(RuntimeConfig& config) noexcept
{
    const char* raw = std::getenv("SAFE_EDGE_INITIAL_PEERS");
    if (nullptr == raw || '\0' == raw[0]) { return; }

    std::string s(raw);
    std::size_t pos = 0U;
    while (pos < s.size() && config.initial_peer_locator_count < 8U)
    {
        const std::size_t comma = s.find(',', pos);
        const std::size_t end = (comma == std::string::npos) ? s.size() : comma;
        const std::string token = s.substr(pos, end - pos);
        pos = (comma == std::string::npos) ? s.size() : comma + 1U;

        const std::size_t colon = token.rfind(':');
        if (colon == std::string::npos) { continue; }

        eprosima::safedds::transport::Locator::IPv4 ip4 = {};
        {
            std::stringstream ss(token.substr(0, colon));
            int a = 0, b = 0, c = 0, d = 0;
            char p1 = '\0', p2 = '\0', p3 = '\0';
            if (!(ss >> a >> p1 >> b >> p2 >> c >> p3 >> d) || p1 != '.' || p2 != '.' || p3 != '.')
            { continue; }
            if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255)
            { continue; }
            ip4[0] = static_cast<uint8_t>(a);
            ip4[1] = static_cast<uint8_t>(b);
            ip4[2] = static_cast<uint8_t>(c);
            ip4[3] = static_cast<uint8_t>(d);
        }

        uint16_t port = 0U;
        {
            std::stringstream ss(token.substr(colon + 1U));
            unsigned int p = 0U;
            if (!(ss >> p) || p > 65535U) { continue; }
            port = static_cast<uint16_t>(p);
        }

        config.initial_peer_locators[config.initial_peer_locator_count++] =
            eprosima::safedds::transport::Locator::from_ipv4(ip4, port);
    }
}

void populate_common_network_config(
        RuntimeConfig& config) noexcept
{
    const eprosima::safedds::transport::Locator::IPv4 localhost = {127, 0, 0, 1};
    resolve_ip_from_env("SAFE_EDGE_OWN_IP", config.own_ip, localhost);
    resolve_ip_from_env("SAFE_EDGE_CROSS_DOMAIN_IP", config.cross_domain_peer_ip, localhost);
    parse_initial_peers(config);
}

} // namespace

RuntimeConfig make_cloud_gateway_runtime_config()
{
    RuntimeConfig config;
    config.participant_name = "SafeEdgeCloudGatewayParticipant";
    config.service_name = "cloud_gateway";
    config.source_name = "cloud_gateway";
    config.domain_id = 0U;
    config.participant_port = 8011U;
    populate_common_network_config(config);
    config.initial_peer_ports[0] = 8012U;
    config.initial_peer_ports[1] = 8013U;
    config.initial_peer_ports[2] = 8001U;
    config.initial_peer_ports[3] = 8002U;
    config.initial_peer_count = 4U;
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
    populate_common_network_config(config);
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
    populate_common_network_config(config);
    config.initial_peer_ports[0] = 8011U;
    config.initial_peer_ports[1] = 8012U;
    config.initial_peer_ports[2] = 8001U;
    config.initial_peer_ports[3] = 8002U;
    config.initial_peer_ports[4] = 8003U;
    config.initial_peer_count = 5U;
    return config;
}

} // namespace common
} // namespace non_safety_domain
} // namespace safe_edge
