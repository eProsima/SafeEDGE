// test_server_integration.cpp
//
// Single binary integration test for safe_edge_server.
// Starts the server as a subprocess, runs all test suites, stops the server.
//
// Usage:
//   ./test_server_integration
//   ./test_server_integration --gtest_output=xml:results.xml   (CI / JUnit)
//
// Environment variables:
//   SAFE_EDGE_SERVER_BIN  path to safe_edge_server binary (default: safe_edge_server)

#include <common.hpp>
#include <pilot_server.hpp>
#include <safe_edge/server/common/TopicNames.hpp>

#include <safedds/dds/DomainParticipant.hpp>
#include <safedds/dds/DomainParticipantFactory.hpp>
#include <safedds/dds/Publisher.hpp>
#include <safedds/dds/Subscriber.hpp>
#include <safedds/dds/SampleInfo.hpp>
#include <safedds/dds/TypedDataReader.hpp>
#include <safedds/dds/TypedDataWriter.hpp>
#include <safedds/dds/qos/DataReaderQos.hpp>
#include <safedds/dds/qos/DataWriterQos.hpp>
#include <safedds/dds/qos/DomainParticipantQos.hpp>
#include <safedds/dds/qos/PublisherQos.hpp>
#include <safedds/dds/qos/SubscriberQos.hpp>
#include <safedds/dds/qos/TopicQos.hpp>
#include <safedds/execution/TimePoint.hpp>
#include <safedds/memory/container/StaticList.hpp>
#include <safedds/memory/container/StaticString.hpp>
#include <safedds/transport.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

static eprosima::safedds::memory::container::StaticList<
        eprosima::safedds::transport::Locator, 1U> g_peers;

static bool init_peers()
{
    g_peers.add(eprosima::safedds::transport::Locator::from_ipv4({127, 0, 0, 1}, 8020U));
    return true;
}

static const bool PEERS_INIT = init_peers();
static const char* SERVER_PID_FILE = "/tmp/safe_edge_server_test.pid";
static const char* SERVER_LOG_FILE = "/tmp/safe_edge_server_test.log";
static const char* SERVER_INI_PATH = "/etc/safe-edge/server.ini";
static bool g_server_ini_created_by_test = false;

static bool pilot_server_config_ready()
{
    std::ifstream file(SERVER_INI_PATH);
    if (!file.is_open())
    {
        return false;
    }

    bool in_section = false;
    std::string line;
    while (std::getline(file, line))
    {
        const size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos || line[start] == '#' || line[start] == ';')
        {
            continue;
        }

        const std::string trimmed = line.substr(start);
        if (trimmed == "[pilot_server]")
        {
            in_section = true;
            continue;
        }
        if (trimmed[0] == '[')
        {
            in_section = false;
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

        const std::string raw_key = trimmed.substr(0, eq);
        const size_t key_end = raw_key.find_last_not_of(" \t");
        const std::string key = (key_end != std::string::npos)
            ? raw_key.substr(0, key_end + 1) : raw_key;
        if (key != "api_key")
        {
            continue;
        }

        const std::string raw_val = trimmed.substr(eq + 1);
        const size_t val_start = raw_val.find_first_not_of(" \t");
        if (val_start == std::string::npos)
        {
            return false;
        }

        const size_t val_end = raw_val.find_last_not_of(" \t\r\n");
        return val_end != std::string::npos && val_end >= val_start;
    }

    return false;
}

static bool ensure_pilot_server_config_available()
{
    if (pilot_server_config_ready())
    {
        return true;
    }

    const char* api_key = std::getenv("PILOT_API_KEY");
    if (nullptr == api_key || api_key[0] == '\0')
    {
        return false;
    }

    const int mkdir_rc = std::system("mkdir -p /etc/safe-edge");
    if (mkdir_rc != 0)
    {
        return false;
    }

    std::ofstream file(SERVER_INI_PATH);
    if (!file.is_open())
    {
        return false;
    }

    file << "[pilot_server]\napi_key = " << api_key << "\n";
    file.close();

    g_server_ini_created_by_test = pilot_server_config_ready();
    return g_server_ini_created_by_test;
}

static eprosima::safedds::dds::DomainParticipant* make_participant(
        eprosima::safedds::dds::DomainParticipantFactory& factory,
        const char* name,
        uint16_t port)
{
    eprosima::safedds::dds::DomainParticipantQos qos{};
    eprosima::safedds::memory::container::StaticString256 sname(name);
    qos.participant_name() = sname;
    qos.wire_protocol_qos().announced_locator =
        eprosima::safedds::transport::Locator::from_ipv4({127, 0, 0, 1}, port);
    qos.wire_protocol_qos().use_multicast_discovery = false;
    qos.wire_protocol_qos().initial_peers = &g_peers;
    return factory.create_participant(0U, qos, nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
}

static void destroy_participant(
        eprosima::safedds::dds::DomainParticipantFactory& factory,
        eprosima::safedds::dds::DomainParticipant*& participant,
        eprosima::safedds::dds::Publisher*& publisher,
        eprosima::safedds::dds::Subscriber*& subscriber,
        eprosima::safedds::dds::Topic*& topic_a,
        eprosima::safedds::dds::Topic*& topic_b,
        eprosima::safedds::dds::DataWriter*& writer,
        eprosima::safedds::dds::DataReader*& reader) noexcept
{
    writer = nullptr;
    reader = nullptr;

    if (participant != nullptr)
    {
        (void)participant->delete_contained_entities();
        (void)factory.delete_participant(participant);
        participant = nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    publisher = nullptr;
    subscriber = nullptr;
    topic_a = nullptr;
    topic_b = nullptr;
}

class ServerEnvironment : public ::testing::Environment
{
public:
    void SetUp() override
    {
        ASSERT_TRUE(ensure_pilot_server_config_available())
            << "Missing pilot server configuration. "
               "Use host file " << SERVER_INI_PATH
            << " or provide PILOT_API_KEY in the environment.";

        const char* bin = std::getenv("SAFE_EDGE_SERVER_BIN");
        const std::string server_bin = (bin != nullptr ? bin : "safe_edge_server");
        const std::string cmd =
            "sh -c 'rm -f " + std::string(SERVER_PID_FILE) +
            "; " + server_bin + " >" + SERVER_LOG_FILE + " 2>&1 & echo $! > " +
            SERVER_PID_FILE + "'";
        ASSERT_EQ(0, std::system(cmd.c_str()))
            << "Failed to launch safe_edge_server. "
               "Set SAFE_EDGE_SERVER_BIN to the full path if needed.";

        std::cout << "[env] safe_edge_server started — waiting 5 s for init...\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::cout << "[env] Server ready.\n";
    }

    void TearDown() override
    {
        std::cout << "[env] safe_edge_server log:\n";
        const int dump_rc = std::system((std::string("sh -c 'if [ -f ") + SERVER_LOG_FILE +
            " ]; then cat " + SERVER_LOG_FILE + "; else echo missing log; fi'").c_str());
        (void)dump_rc;
        std::cout << "[env] Stopping safe_edge_server...\n";
        const std::string stop_cmd =
            "sh -c '"
            "if [ -f " + std::string(SERVER_PID_FILE) + " ]; then "
            "pid=$(cat " + std::string(SERVER_PID_FILE) + "); "
            "kill \"$pid\" 2>/dev/null || true; "
            "sleep 1; "
            "kill -9 \"$pid\" 2>/dev/null || true; "
            "rm -f " + std::string(SERVER_PID_FILE) + "; "
            "fi; "
            "pkill -f safe_edge_server 2>/dev/null || true"
            "'";
        const int stop_rc = std::system(stop_cmd.c_str());
        (void)stop_rc;
        if (g_server_ini_created_by_test)
        {
            const int rm_rc = std::remove(SERVER_INI_PATH);
            (void)rm_rc;
            g_server_ini_created_by_test = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
};

static bool wait_for_charger_location(
        eprosima::safedds::execution::ISpinnable& executor,
        eprosima::safedds::dds::TypedDataReader<
            safe_edge::pilot_server::ChargerLocationTypeSupport>& reader,
        int timeout_s)
{
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);

    while (std::chrono::steady_clock::now() < deadline)
    {
        while (executor.has_pending_work())
        {
            executor.spin(eprosima::safedds::execution::TIME_ZERO);
        }

        safe_edge::pilot_server::ChargerLocation sample{};
        eprosima::safedds::dds::SampleInfo info{};
        if (reader.take_next_sample(sample, info) ==
                eprosima::safedds::dds::ReturnCode::OK && info.valid_data)
        {
            std::cout << "[dds] Received ChargerLocation id=" << sample.id
                      << " name=" << sample.name << "\n";
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

static void drain_charger_locations(
        eprosima::safedds::execution::ISpinnable& executor,
        eprosima::safedds::dds::TypedDataReader<
            safe_edge::pilot_server::ChargerLocationTypeSupport>& reader)
{
    while (executor.has_pending_work())
    {
        executor.spin(eprosima::safedds::execution::TIME_ZERO);
    }

    safe_edge::pilot_server::ChargerLocation sample{};
    eprosima::safedds::dds::SampleInfo info{};
    while (reader.take_next_sample(sample, info) ==
            eprosima::safedds::dds::ReturnCode::OK)
    {
    }
}

static bool wait_for_query_response(
        eprosima::safedds::execution::ISpinnable& executor,
        eprosima::safedds::dds::TypedDataWriter<
            safe_edge::pilot_server::ServerQueryTypeSupport>& writer,
        eprosima::safedds::dds::TypedDataReader<
            safe_edge::pilot_server::ChargerLocationTypeSupport>& reader,
        safe_edge::pilot_server::RequestedDataType type,
        int timeout_s)
{
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);

    while (std::chrono::steady_clock::now() < deadline)
    {
        safe_edge::pilot_server::ServerQuery q{};
        q.requested_by = "test_query_dispatch";
        q.requested_data_type = type;
        if (writer.write(q, eprosima::safedds::dds::HANDLE_NIL) ==
                eprosima::safedds::dds::ReturnCode::OK &&
                wait_for_charger_location(executor, reader, 1))
        {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    return false;
}

static void spin_for(
        eprosima::safedds::execution::ISpinnable& executor,
        std::chrono::milliseconds duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;

    while (std::chrono::steady_clock::now() < deadline)
    {
        while (executor.has_pending_work())
        {
            executor.spin(eprosima::safedds::execution::TIME_ZERO);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

struct QueryDispatchContext
{
    eprosima::safedds::dds::DomainParticipantFactory factory;
    eprosima::safedds::dds::DomainParticipant* participant = nullptr;
    eprosima::safedds::dds::Publisher* publisher = nullptr;
    eprosima::safedds::dds::Subscriber* subscriber = nullptr;
    eprosima::safedds::dds::Topic* charger_topic = nullptr;
    eprosima::safedds::dds::Topic* query_topic = nullptr;
    eprosima::safedds::dds::DataWriter* query_writer_base = nullptr;
    eprosima::safedds::dds::DataReader* charger_reader_base = nullptr;
    eprosima::safedds::execution::ISpinnable* executor = nullptr;
    safe_edge::pilot_server::ChargerLocationTypeSupport charger_ts;
    safe_edge::pilot_server::ServerQueryTypeSupport query_ts;
    eprosima::safedds::dds::TypedDataWriter<
        safe_edge::pilot_server::ServerQueryTypeSupport>* query_writer = nullptr;
    eprosima::safedds::dds::TypedDataReader<
        safe_edge::pilot_server::ChargerLocationTypeSupport>* charger_reader = nullptr;
};

static QueryDispatchContext& query_dispatch_context()
{
    static QueryDispatchContext* ctx = new QueryDispatchContext();
    return *ctx;
}

static void create_query_dispatch_context(QueryDispatchContext& ctx);
static void destroy_query_dispatch_context(QueryDispatchContext& ctx) noexcept;

class QueryDispatchSuite : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        create_query_dispatch_context(query_dispatch_context());
    }

    static void TearDownTestSuite()
    {
        destroy_query_dispatch_context(query_dispatch_context());
    }
};

static void create_query_dispatch_context(
        QueryDispatchContext& ctx)
{
    (void)PEERS_INIT;

    ctx.participant = make_participant(ctx.factory, "TestQueryDispatch", 8011U);
    ASSERT_NE(nullptr, ctx.participant);

    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK,
        ctx.charger_ts.register_type(*ctx.participant, ctx.charger_ts.get_type_name()));
    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK,
        ctx.query_ts.register_type(*ctx.participant, ctx.query_ts.get_type_name()));

    eprosima::safedds::memory::container::StaticString256 cname(
        safe_edge::server::common::topic_names::charger_locations());
    eprosima::safedds::memory::container::StaticString256 qname(
        safe_edge::server::common::topic_names::server_query());

    ctx.charger_topic = ctx.participant->create_topic(
        cname, ctx.charger_ts.get_type_name(),
        eprosima::safedds::dds::TopicQos{}, nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    ctx.query_topic = ctx.participant->create_topic(
        qname, ctx.query_ts.get_type_name(),
        eprosima::safedds::dds::TopicQos{}, nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    ASSERT_NE(nullptr, ctx.charger_topic);
    ASSERT_NE(nullptr, ctx.query_topic);

    ctx.publisher = ctx.participant->create_publisher(
        eprosima::safedds::dds::PublisherQos{}, nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    ctx.subscriber = ctx.participant->create_subscriber(
        eprosima::safedds::dds::SubscriberQos{}, nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    ASSERT_NE(nullptr, ctx.publisher);
    ASSERT_NE(nullptr, ctx.subscriber);

    eprosima::safedds::dds::DataWriterQos wqos{};
    wqos.reliability().kind =
        eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;
    eprosima::safedds::dds::DataReaderQos rqos{};
    rqos.reliability().kind =
        eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    ctx.query_writer_base = ctx.publisher->create_datawriter(
        *ctx.query_topic, wqos, nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    ctx.charger_reader_base = ctx.subscriber->create_datareader(
        *ctx.charger_topic, rqos, nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    ASSERT_NE(nullptr, ctx.query_writer_base);
    ASSERT_NE(nullptr, ctx.charger_reader_base);

    ctx.query_writer = eprosima::safedds::dds::TypedDataWriter<
        safe_edge::pilot_server::ServerQueryTypeSupport>::downcast(*ctx.query_writer_base);
    ctx.charger_reader = eprosima::safedds::dds::TypedDataReader<
        safe_edge::pilot_server::ChargerLocationTypeSupport>::downcast(*ctx.charger_reader_base);
    ASSERT_NE(nullptr, ctx.query_writer);
    ASSERT_NE(nullptr, ctx.charger_reader);

    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, ctx.publisher->enable());
    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, ctx.subscriber->enable());
    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, ctx.query_writer_base->enable());
    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, ctx.charger_reader_base->enable());
    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, ctx.participant->enable());

    ctx.executor = ctx.factory.create_default_executor();
    ASSERT_NE(nullptr, ctx.executor);

    spin_for(*ctx.executor, std::chrono::seconds(3));
}

static void destroy_query_dispatch_context(
        QueryDispatchContext& ctx) noexcept
{
    if (nullptr == ctx.participant)
    {
        return;
    }

    destroy_participant(ctx.factory, ctx.participant, ctx.publisher, ctx.subscriber,
        ctx.charger_topic, ctx.query_topic, ctx.query_writer_base, ctx.charger_reader_base);
    ctx.executor = nullptr;
    ctx.query_writer = nullptr;
    ctx.charger_reader = nullptr;
}

static void send_query(
        eprosima::safedds::dds::TypedDataWriter<
            safe_edge::pilot_server::ServerQueryTypeSupport>& writer,
        safe_edge::pilot_server::RequestedDataType type)
{
    safe_edge::pilot_server::ServerQuery q{};
    q.requested_by = "test_query_dispatch";
    q.requested_data_type = type;
    EXPECT_EQ(eprosima::safedds::dds::ReturnCode::OK,
        writer.write(q, eprosima::safedds::dds::HANDLE_NIL));
}

TEST_F(QueryDispatchSuite, ChargerLocationReturnsData)
{
    auto& ctx = query_dispatch_context();
    drain_charger_locations(*ctx.executor, *ctx.charger_reader);
    EXPECT_TRUE(wait_for_query_response(
        *ctx.executor,
        *ctx.query_writer,
        *ctx.charger_reader,
        safe_edge::pilot_server::RequestedDataType::CHARGER_LOCATION,
        10)) << "No ChargerLocation received after query within 10 s";
}

TEST_F(QueryDispatchSuite, ChargerTypeHandledWithoutCrash)
{
    auto& ctx = query_dispatch_context();
    send_query(*ctx.query_writer,
        safe_edge::pilot_server::RequestedDataType::CHARGER_TYPE);
    spin_for(*ctx.executor, std::chrono::milliseconds(500));
}

TEST_F(QueryDispatchSuite, ChargingSessionHandledWithoutCrash)
{
    auto& ctx = query_dispatch_context();
    send_query(*ctx.query_writer,
        safe_edge::pilot_server::RequestedDataType::CHARGING_SESSION);
    spin_for(*ctx.executor, std::chrono::milliseconds(500));
}

TEST_F(QueryDispatchSuite, TransitHealthHandledWithoutCrash)
{
    auto& ctx = query_dispatch_context();
    send_query(*ctx.query_writer,
        safe_edge::pilot_server::RequestedDataType::TRANSIT_HEALTH);
    spin_for(*ctx.executor, std::chrono::milliseconds(500));
}

TEST_F(QueryDispatchSuite, TransitMetricsHandledWithoutCrash)
{
    auto& ctx = query_dispatch_context();
    send_query(*ctx.query_writer,
        safe_edge::pilot_server::RequestedDataType::TRANSIT_METRICS);
    spin_for(*ctx.executor, std::chrono::milliseconds(500));
}

TEST(PeriodicRefresh, TwoBurstsIn35Seconds)
{
    (void)PEERS_INIT;

    eprosima::safedds::dds::DomainParticipantFactory factory;
    auto* participant = make_participant(factory, "TestPeriodicRefresh", 8030U);
    ASSERT_NE(nullptr, participant);

    safe_edge::pilot_server::ChargerLocationTypeSupport charger_ts;
    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK,
        charger_ts.register_type(*participant, charger_ts.get_type_name()));

    eprosima::safedds::memory::container::StaticString256 tname(
        safe_edge::server::common::topic_names::charger_locations());
    auto* topic = participant->create_topic(
        tname, charger_ts.get_type_name(),
        eprosima::safedds::dds::TopicQos{}, nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    ASSERT_NE(nullptr, topic);

    auto* sub = participant->create_subscriber(
        eprosima::safedds::dds::SubscriberQos{}, nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    ASSERT_NE(nullptr, sub);

    eprosima::safedds::dds::DataReaderQos rqos{};
    rqos.reliability().kind =
        eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

    auto* reader_base = sub->create_datareader(
        *topic, rqos, nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    ASSERT_NE(nullptr, reader_base);

    auto* reader = eprosima::safedds::dds::TypedDataReader<
        safe_edge::pilot_server::ChargerLocationTypeSupport>::downcast(*reader_base);
    ASSERT_NE(nullptr, reader);

    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, sub->enable());
    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, reader_base->enable());
    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, participant->enable());

    auto* executor = factory.create_default_executor();
    ASSERT_NE(nullptr, executor);

    spin_for(*executor, std::chrono::seconds(3));

    int burst_count = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const auto end = t0 + std::chrono::seconds(45);
    auto last_sample = t0 - std::chrono::seconds(10);

    while (std::chrono::steady_clock::now() < end)
    {
        while (executor->has_pending_work())
        {
            executor->spin(eprosima::safedds::execution::TIME_ZERO);
        }

        safe_edge::pilot_server::ChargerLocation sample{};
        eprosima::safedds::dds::SampleInfo info{};
        if (reader->take_next_sample(sample, info) ==
                eprosima::safedds::dds::ReturnCode::OK && info.valid_data)
        {
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_sample).count() > 2000)
            {
                burst_count++;
                std::cout << "[dds] Burst " << burst_count << " at t="
                          << std::chrono::duration_cast<std::chrono::seconds>(
                                 now - t0).count() << " s\n";
            }
            last_sample = now;
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    EXPECT_GE(burst_count, 2)
        << "Expected >=2 bursts (publication_match + periodic refresh), "
        << "got " << burst_count;

    eprosima::safedds::dds::Publisher* unused_publisher = nullptr;
    eprosima::safedds::dds::DataWriter* unused_writer = nullptr;
    destroy_participant(factory, participant, unused_publisher, sub,
        topic, topic, unused_writer, reader_base);
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new ServerEnvironment());
    return RUN_ALL_TESTS();
}
