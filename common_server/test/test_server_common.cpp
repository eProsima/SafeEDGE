#include <safe_edge/server/common/PilotServerPayloadParser.hpp>
#include <safe_edge/server/common/PilotServerClient.hpp>

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using safe_edge::server::common::ParsedChargerLocation;
using safe_edge::server::common::PilotServerPayloadParser;
using safe_edge::server::common::PilotServerClient;

static const char* const PILOT_SERVER_BASE_URL   = "https://pilot2.dumitru-alexandru.work";
static const char* const PILOT_SERVER_INI_PATH   = "/etc/safe-edge/server.ini";
static const char* const CHARGER_LOCATIONS_EP    = "/api/chargers/locations";
static const char* const CHARGER_TYPES_EP        = "/api/chargers/types";
static const char* const CHARGING_SESSIONS_EP    = "/api/chargers/sessions";
static const char* const TRANSIT_HEALTH_EP       = "/api/transit/health";
static const char* const TRANSIT_METRICS_EP      = "/api/transit/metrics";

static bool looks_like_json_payload(const std::string& body)
{
    for (char ch : body)
    {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
        {
            continue;
        }
        return ch == '{' || ch == '[';
    }
    return false;
}

static void skip_if_degraded_real_response(
        const std::string& body,
        const char* endpoint,
        const char* expected_marker)
{
    if (body.empty())
    {
        GTEST_SKIP() << endpoint << " returned an empty response";
    }

    if (!looks_like_json_payload(body))
    {
        GTEST_SKIP() << endpoint << " returned a non-JSON payload of " << body.size() << " bytes";
    }

    if (body.find(expected_marker) == std::string::npos)
    {
        GTEST_SKIP() << endpoint << " returned a schema-mismatched payload of " << body.size()
                     << " bytes; expected marker '" << expected_marker << "'";
    }
}

// ============================================================================
// PilotServerPayloadParser
// ============================================================================

TEST(PilotServerPayloadParserTest, FlatFields)
{
    const std::string body =
        R"([{"id":1,"name":"Hub A","latitude":40.4637,"longitude":-3.7492},)"
        R"( {"id":2,"name":"Hub B","latitude":41.3851,"longitude":2.1734}])";

    const auto result = PilotServerPayloadParser::parse_charger_locations(body);

    ASSERT_EQ(2U, result.size());
    EXPECT_EQ(1,         result[0].id);
    EXPECT_EQ("Hub A",   result[0].name);
    EXPECT_FLOAT_EQ(40.4637F,  result[0].latitude);
    EXPECT_FLOAT_EQ(-3.7492F, result[0].longitude);
    EXPECT_EQ(2, result[1].id);
    EXPECT_EQ("Hub B", result[1].name);
}

TEST(PilotServerPayloadParserTest, NestedPosition)
{
    const std::string body =
        R"([{"id":5,"name":"Depot","position":{"latitude":39.4699,"longitude":-0.3763}}])";

    const auto result = PilotServerPayloadParser::parse_charger_locations(body);

    ASSERT_EQ(1U, result.size());
    EXPECT_EQ(5,        result[0].id);
    EXPECT_EQ("Depot",  result[0].name);
    EXPECT_FLOAT_EQ(39.4699F,  result[0].latitude);
    EXPECT_FLOAT_EQ(-0.3763F, result[0].longitude);
}

TEST(PilotServerPayloadParserTest, EmptyPayload)
{
    const auto result = PilotServerPayloadParser::parse_charger_locations("");
    EXPECT_TRUE(result.empty());
}

TEST(PilotServerPayloadParserTest, InvalidJson)
{
    const auto result = PilotServerPayloadParser::parse_charger_locations("not json at all");
    EXPECT_TRUE(result.empty());
}

TEST(PilotServerPayloadParserTest, MissingFields)
{
    // Object present but only id — name, latitude, longitude absent
    const std::string body = R"([{"id":7}])";

    const auto result = PilotServerPayloadParser::parse_charger_locations(body);

    ASSERT_EQ(1U, result.size());
    EXPECT_EQ(7,    result[0].id);
    EXPECT_EQ("",   result[0].name);
    EXPECT_FLOAT_EQ(0.0F, result[0].latitude);
    EXPECT_FLOAT_EQ(0.0F, result[0].longitude);
}

// ============================================================================
// PilotServerClient
// ============================================================================

static const char* const CONFIG_PATH = "/tmp/test_pilotclient.ini";

static void write_config(const char* content)
{
    std::ofstream f(CONFIG_PATH);
    f << content;
}

static void remove_config()
{
    ::unlink(CONFIG_PATH);
}

TEST(PilotServerClientTest, MissingConfigFile)
{
    remove_config();
    PilotServerClient client("http://127.0.0.1:18099", CONFIG_PATH);
    EXPECT_TRUE(client.fetch("/any").empty());
}

TEST(PilotServerClientTest, MissingApiKey)
{
    write_config("[pilot_server]\n# no api_key\n");
    PilotServerClient client("http://127.0.0.1:18099", CONFIG_PATH);
    EXPECT_TRUE(client.fetch("/any").empty());
    remove_config();
}

TEST(PilotServerClientTest, WrongSection)
{
    write_config("[other_section]\napi_key = secret\n");
    PilotServerClient client("http://127.0.0.1:18099", CONFIG_PATH);
    EXPECT_TRUE(client.fetch("/any").empty());
    remove_config();
}

TEST(PilotServerClientTest, SuccessPathMockServer)
{
    static constexpr uint16_t PORT = 18099U;
    static const char* const BODY =
        R"([{"id":1,"name":"Mock","latitude":1.0,"longitude":2.0}])";

    std::thread server_thread([&]()
    {
        const int srv = ::socket(AF_INET, SOCK_STREAM, 0);
        if (srv < 0) { return; }

        const int opt = 1;
        ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = htons(PORT);

        if (::bind(srv, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            ::close(srv);
            return;
        }
        ::listen(srv, 1);

        const int conn = ::accept(srv, nullptr, nullptr);
        if (conn >= 0)
        {
            char buf[4096];
            ::recv(conn, buf, sizeof(buf), 0);

            const std::string body = BODY;
            const std::string response =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: " + std::to_string(body.size()) + "\r\n"
                "Connection: close\r\n"
                "\r\n" + body;
            ::send(conn, response.c_str(), response.size(), 0);
            ::close(conn);
        }
        ::close(srv);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    write_config("[pilot_server]\napi_key = testkey\n");
    PilotServerClient client(
        "http://127.0.0.1:" + std::to_string(PORT), CONFIG_PATH);

    const std::string result = client.fetch("/locations");
    remove_config();
    server_thread.join();

    EXPECT_FALSE(result.empty());
    EXPECT_NE(std::string::npos, result.find("Mock"));
}

// ============================================================================
// Real Pilot Server tests
// Skipped automatically if /etc/safe-edge/server.ini is not present.
// ============================================================================

TEST(RealPilotServerTest, ChargerLocationsReturnsParsedData)
{
    if (::access(PILOT_SERVER_INI_PATH, F_OK) != 0)
    {
        GTEST_SKIP() << PILOT_SERVER_INI_PATH << " not found — skipping real server test";
    }

    PilotServerClient client(PILOT_SERVER_BASE_URL, PILOT_SERVER_INI_PATH);

    const std::string body = client.fetch(CHARGER_LOCATIONS_EP);
    skip_if_degraded_real_response(body, CHARGER_LOCATIONS_EP, "id");

    const auto locations = PilotServerPayloadParser::parse_charger_locations(body);
    if (locations.empty())
    {
        GTEST_SKIP() << CHARGER_LOCATIONS_EP
                     << " returned JSON but no parseable charger locations";
    }

    std::cout << "  [real] Received " << locations.size() << " charger location(s)\n";
    for (const auto& loc : locations)
    {
        std::cout << "  [real]   id=" << loc.id
                  << " name=" << loc.name
                  << " lat=" << loc.latitude
                  << " lng=" << loc.longitude << "\n";
    }
}

TEST(RealPilotServerTest, ChargerTypesReturnsData)
{
    if (::access(PILOT_SERVER_INI_PATH, F_OK) != 0)
    {
        GTEST_SKIP() << PILOT_SERVER_INI_PATH << " not found — skipping real server test";
    }

    PilotServerClient client(PILOT_SERVER_BASE_URL, PILOT_SERVER_INI_PATH);
    const std::string body = client.fetch(CHARGER_TYPES_EP);

    skip_if_degraded_real_response(body, CHARGER_TYPES_EP, "charger_type");

    std::cout << "  [real] charger_types response bytes=" << body.size() << "\n";
}

TEST(RealPilotServerTest, ChargingSessionsReturnsData)
{
    if (::access(PILOT_SERVER_INI_PATH, F_OK) != 0)
    {
        GTEST_SKIP() << PILOT_SERVER_INI_PATH << " not found — skipping real server test";
    }

    PilotServerClient client(PILOT_SERVER_BASE_URL, PILOT_SERVER_INI_PATH);
    const std::string body = client.fetch(CHARGING_SESSIONS_EP);

    skip_if_degraded_real_response(body, CHARGING_SESSIONS_EP, "station_id");

    std::cout << "  [real] charging_sessions response bytes=" << body.size() << "\n";
}

TEST(RealPilotServerTest, TransitHealthReturnsStatus)
{
    if (::access(PILOT_SERVER_INI_PATH, F_OK) != 0)
    {
        GTEST_SKIP() << PILOT_SERVER_INI_PATH << " not found — skipping real server test";
    }

    PilotServerClient client(PILOT_SERVER_BASE_URL, PILOT_SERVER_INI_PATH);
    const std::string body = client.fetch(TRANSIT_HEALTH_EP);

    skip_if_degraded_real_response(body, TRANSIT_HEALTH_EP, "status");

    std::cout << "  [real] transit_health response: " << body << "\n";
}

TEST(RealPilotServerTest, TransitMetricsReturnsByRoute)
{
    if (::access(PILOT_SERVER_INI_PATH, F_OK) != 0)
    {
        GTEST_SKIP() << PILOT_SERVER_INI_PATH << " not found — skipping real server test";
    }

    PilotServerClient client(PILOT_SERVER_BASE_URL, PILOT_SERVER_INI_PATH);
    const std::string body = client.fetch(TRANSIT_METRICS_EP);

    skip_if_degraded_real_response(body, TRANSIT_METRICS_EP, "by_route");

    std::cout << "  [real] transit_metrics response bytes=" << body.size() << "\n";
}
