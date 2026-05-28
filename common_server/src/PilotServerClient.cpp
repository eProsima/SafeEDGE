#include <safe_edge/server/common/PilotServerClient.hpp>

#include <curl/curl.h>

#include <fstream>
#include <iostream>
#include <string>

namespace safe_edge {
namespace server {
namespace common {

namespace {

static size_t write_callback(void* ptr, size_t size, size_t nmemb, void* userdata)
{
    static_cast<std::string*>(userdata)->append(
        static_cast<const char*>(ptr), size * nmemb);
    return size * nmemb;
}

} // namespace

PilotServerClient::PilotServerClient(
        const std::string& base_url,
        const std::string& ini_path)
    : base_url_(base_url)
{
    // curl_global_init / curl_global_cleanup are called once per PilotServerClient instance.
    // This is safe because exactly one PilotServerClient exists in the process (owned by ServerNode).
    curl_global_init(CURL_GLOBAL_DEFAULT);
    ready_ = load_api_key(ini_path);
}

PilotServerClient::~PilotServerClient()
{
    curl_global_cleanup();
}

bool PilotServerClient::load_api_key(
        const std::string& ini_path)
{
    std::ifstream file(ini_path);
    if (!file.is_open())
    {
        std::cerr << "[pilot_client] Cannot open config file: " << ini_path << std::endl;
        return false;
    }

    bool in_section = false;
    std::string line;
    while (std::getline(file, line))
    {
        // Strip leading whitespace
        const size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos || line[start] == '#' || line[start] == ';')
        {
            continue; // blank line or comment
        }
        const std::string trimmed = line.substr(start);

        if (trimmed == "[pilot_server]")
        {
            in_section = true;
            continue;
        }
        if (trimmed[0] == '[')
        {
            in_section = false; // different section
            continue;
        }

        if (!in_section)
        {
            continue;
        }

        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos)
        {
            continue;
        }

        // Trim key
        const std::string raw_key = trimmed.substr(0, eq);
        const size_t key_end = raw_key.find_last_not_of(" \t");
        const std::string key = (key_end != std::string::npos)
            ? raw_key.substr(0, key_end + 1) : raw_key;

        // Trim value
        const std::string raw_val = trimmed.substr(eq + 1);
        const size_t val_start = raw_val.find_first_not_of(" \t");
        if (val_start == std::string::npos)
        {
            continue;
        }
        const size_t val_end = raw_val.find_last_not_of(" \t\r\n");
        const std::string value = raw_val.substr(val_start, val_end - val_start + 1);

        if (key == "api_key")
        {
            api_key_ = value;
            return true;
        }
    }

    std::cerr << "[pilot_client] api_key not found in [pilot_server] section of "
              << ini_path << std::endl;
    return false;
}

std::string PilotServerClient::fetch(
        const std::string& endpoint) noexcept
{
    if (!ready_)
    {
        std::cerr << "[pilot_client] Not ready — check config file" << std::endl;
        return {};
    }

    CURL* curl = curl_easy_init();
    if (nullptr == curl)
    {
        std::cerr << "[pilot_client] curl_easy_init failed" << std::endl;
        return {};
    }

    const std::string url = base_url_ + endpoint;
    // api_key_ must NOT appear in any log line
    const std::string auth = "X-API-KEY: " + api_key_;
    struct curl_slist* headers = curl_slist_append(nullptr, auth.c_str());

    std::string body;

    curl_easy_setopt(curl, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,    headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &body);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    const CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        std::cerr << "[pilot_client] GET " << endpoint
                  << " failed: " << curl_easy_strerror(res) << std::endl;
        return {};
    }

    std::cout << "[pilot_client] GET " << endpoint
              << " bytes=" << body.size() << std::endl;
    return body;
}

} // namespace common
} // namespace server
} // namespace safe_edge
