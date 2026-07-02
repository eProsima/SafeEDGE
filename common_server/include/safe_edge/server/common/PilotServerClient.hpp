#ifndef SAFE_EDGE_SERVER_COMMON_PILOTSERVERCLIENT_HPP
#define SAFE_EDGE_SERVER_COMMON_PILOTSERVERCLIENT_HPP

#include <string>

namespace safe_edge {
namespace server {
namespace common {

class PilotServerClient
{
public:
    PilotServerClient(
            const std::string& base_url,
            const std::string& ini_path);
    ~PilotServerClient();

    // Executes HTTP GET base_url + endpoint with Authorization: Bearer <api_key>.
    // Returns the response body on success, empty string on any error.
    // The api_key never appears in any log output.
    // max_bytes: stop download after this many bytes (0 = unlimited).
    // timeout_ms: total transfer timeout in ms (0 = use default).
    std::string fetch(
            const std::string& endpoint,
            size_t max_bytes = 0,
            long timeout_ms = 0) noexcept;

    // Returns true only when the pilot server responds successfully with usable data.
    bool is_pilot_server_available() noexcept;

private:
    bool load_api_key(const std::string& ini_path);

    std::string base_url_;
    std::string api_key_;
    bool        ready_ = false;
};

} // namespace common
} // namespace server
} // namespace safe_edge

#endif // SAFE_EDGE_SERVER_COMMON_PILOTSERVERCLIENT_HPP
