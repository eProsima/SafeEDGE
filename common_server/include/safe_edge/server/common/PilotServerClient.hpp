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
    std::string fetch(const std::string& endpoint) noexcept;

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
