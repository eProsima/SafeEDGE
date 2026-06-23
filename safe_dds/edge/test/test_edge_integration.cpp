// test_edge_integration.cpp — Safe DDS variant
//
// Integration tests for EdgeGatewayNode health state machine.
// Starts safe_edge_edge_gateway as a subprocess, runs all suites, stops it.
//
// Usage:
//   ./test_edge_integration
//   ./test_edge_integration --gtest_output=xml:results.xml
//
// Environment variables:
//   SAFE_EDGE_EDGE_BIN  path to safe_edge_edge_gateway (default: safe_edge_edge_gateway)

#include <common.hpp>
#include <edge.hpp>
#include <pilot_server.hpp>
#include <safe_edge/edge_module/common/TopicNames.hpp>

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
#include <safedds/execution/ISpinnable.hpp>
#include <safedds/execution/TimePoint.hpp>
#include <safedds/memory/container/StaticList.hpp>
#include <safedds/memory/container/StaticString.hpp>
#include <safedds/transport.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Global peers: point at the edge participant (port 8030)
// ---------------------------------------------------------------------------

static eprosima::safedds::memory::container::StaticList<
        eprosima::safedds::transport::Locator, 1U> g_peers;

static bool init_peers()
{
    g_peers.add(eprosima::safedds::transport::Locator::from_ipv4({127, 0, 0, 1}, 8030U));
    return true;
}

static const bool PEERS_INIT = init_peers();

static const char* EDGE_PID_FILE = "/tmp/safe_edge_edge_gateway_test.pid";
static const char* EDGE_LOG_FILE = "/tmp/safe_edge_edge_gateway_test.log";

// Each test gets a unique port so there is no binding conflict when the OS
// has not yet released the previous socket.
static uint16_t next_test_port()
{
    static std::atomic<uint16_t> next_port{8050U};
    return next_port.fetch_add(1U, std::memory_order_relaxed);
}

static eprosima::safedds::dds::DomainParticipant* make_participant(
        eprosima::safedds::dds::DomainParticipantFactory& factory,
        const char* name, uint16_t port)
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
        eprosima::safedds::dds::DomainParticipant*& participant) noexcept
{
    if (participant != nullptr)
    {
        (void)participant->delete_contained_entities();
        (void)factory.delete_participant(participant);
        participant = nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

// ---------------------------------------------------------------------------
// Global environment: lifecycle of safe_edge_edge_gateway subprocess
// ---------------------------------------------------------------------------

class EdgeEnvironment : public ::testing::Environment
{
public:
    void SetUp() override
    {
        const char* bin = std::getenv("SAFE_EDGE_EDGE_BIN");
        const std::string edge_bin = (bin != nullptr ? bin : "safe_edge_edge_gateway");
        const std::string cmd =
            "sh -c 'rm -f " + std::string(EDGE_PID_FILE) +
            "; " + edge_bin + " >" + EDGE_LOG_FILE + " 2>&1 & echo $! > " +
            EDGE_PID_FILE + "'";
        ASSERT_EQ(0, std::system(cmd.c_str()))
            << "Failed to launch safe_edge_edge_gateway. "
               "Set SAFE_EDGE_EDGE_BIN to the full path if needed.";
        std::cout << "[env] safe_edge_edge_gateway started — waiting 5 s for init...\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::cout << "[env] Edge ready.\n";
    }

    void TearDown() override
    {
        std::cout << "[env] safe_edge_edge_gateway log:\n";
        const int dump_rc = std::system((std::string("sh -c 'if [ -f ") + EDGE_LOG_FILE +
            " ]; then cat " + EDGE_LOG_FILE + "; else echo missing log; fi'").c_str());
        (void)dump_rc;
        std::cout << "[env] Stopping safe_edge_edge_gateway...\n";
        const std::string stop_cmd =
            "sh -c '"
            "if [ -f " + std::string(EDGE_PID_FILE) + " ]; then "
            "pid=$(cat " + std::string(EDGE_PID_FILE) + "); "
            "kill \"$pid\" 2>/dev/null || true; "
            "sleep 1; "
            "kill -9 \"$pid\" 2>/dev/null || true; "
            "rm -f " + std::string(EDGE_PID_FILE) + "; "
            "fi; "
            "pkill -f safe_edge_edge_gateway 2>/dev/null || true"
            "'";
        const int stop_rc = std::system(stop_cmd.c_str());
        (void)stop_rc;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
};

// ---------------------------------------------------------------------------
// Fixture: mock server participant
// Publishes: ServiceHeartbeat, ChargerLocation
// Reads:     EdgeGatewayStatus
// ---------------------------------------------------------------------------

class MockServerFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        (void)PEERS_INIT;
        participant_ = make_participant(factory_, "TestMockServer", next_test_port());
        ASSERT_NE(nullptr, participant_);

        ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK,
            hb_ts_.register_type(*participant_, hb_ts_.get_type_name()));
        ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK,
            loc_ts_.register_type(*participant_, loc_ts_.get_type_name()));
        ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK,
            status_ts_.register_type(*participant_, status_ts_.get_type_name()));

        namespace TN = safe_edge::edge_module::common::topic_names;
        eprosima::safedds::memory::container::StaticString256
            hb_name(TN::service_heartbeat()),
            loc_name(TN::charger_locations()),
            status_name(TN::edge_gateway_status());

        hb_topic_ = participant_->create_topic(hb_name, hb_ts_.get_type_name(),
            eprosima::safedds::dds::TopicQos{}, nullptr,
            eprosima::safedds::dds::NONE_STATUS_MASK);
        loc_topic_ = participant_->create_topic(loc_name, loc_ts_.get_type_name(),
            eprosima::safedds::dds::TopicQos{}, nullptr,
            eprosima::safedds::dds::NONE_STATUS_MASK);
        status_topic_ = participant_->create_topic(status_name, status_ts_.get_type_name(),
            eprosima::safedds::dds::TopicQos{}, nullptr,
            eprosima::safedds::dds::NONE_STATUS_MASK);
        ASSERT_NE(nullptr, hb_topic_);
        ASSERT_NE(nullptr, loc_topic_);
        ASSERT_NE(nullptr, status_topic_);

        publisher_ = participant_->create_publisher(
            eprosima::safedds::dds::PublisherQos{}, nullptr,
            eprosima::safedds::dds::NONE_STATUS_MASK);
        subscriber_ = participant_->create_subscriber(
            eprosima::safedds::dds::SubscriberQos{}, nullptr,
            eprosima::safedds::dds::NONE_STATUS_MASK);
        ASSERT_NE(nullptr, publisher_);
        ASSERT_NE(nullptr, subscriber_);

        eprosima::safedds::dds::DataWriterQos wqos{};
        wqos.reliability().kind =
            eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;
        eprosima::safedds::dds::DataReaderQos rqos{};
        rqos.reliability().kind =
            eprosima::safedds::dds::ReliabilityQosPolicyKind::RELIABLE_RELIABILITY_QOS;

        hb_wb_   = publisher_->create_datawriter(*hb_topic_,    wqos, nullptr,
            eprosima::safedds::dds::NONE_STATUS_MASK);
        loc_wb_  = publisher_->create_datawriter(*loc_topic_,   wqos, nullptr,
            eprosima::safedds::dds::NONE_STATUS_MASK);
        status_rb_ = subscriber_->create_datareader(*status_topic_, rqos, nullptr,
            eprosima::safedds::dds::NONE_STATUS_MASK);
        ASSERT_NE(nullptr, hb_wb_);
        ASSERT_NE(nullptr, loc_wb_);
        ASSERT_NE(nullptr, status_rb_);

        hb_writer_ = eprosima::safedds::dds::TypedDataWriter<
            safe_edge::common::ServiceHeartbeatTypeSupport>::downcast(*hb_wb_);
        loc_writer_ = eprosima::safedds::dds::TypedDataWriter<
            safe_edge::pilot_server::ChargerLocationTypeSupport>::downcast(*loc_wb_);
        status_reader_ = eprosima::safedds::dds::TypedDataReader<
            safe_edge::edge::EdgeGatewayStatusTypeSupport>::downcast(*status_rb_);
        ASSERT_NE(nullptr, hb_writer_);
        ASSERT_NE(nullptr, loc_writer_);
        ASSERT_NE(nullptr, status_reader_);

        // Enable participant FIRST so the transport starts before sub-entities bind.
        ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, participant_->enable());
        ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, publisher_->enable());
        ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, subscriber_->enable());
        ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, hb_wb_->enable());
        ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, loc_wb_->enable());
        ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, status_rb_->enable());

        // Create executor so we can drive SafeDDS I/O from the test thread.
        executor_ = factory_.create_default_executor();
        ASSERT_NE(nullptr, executor_);

        // Wait for discovery while spinning so SPDP exchange completes.
        spin_for(std::chrono::seconds(3));
    }

    void TearDown() override
    {
        executor_     = nullptr;
        hb_writer_    = nullptr;
        loc_writer_   = nullptr;
        status_reader_= nullptr;
        hb_wb_        = nullptr;
        loc_wb_       = nullptr;
        status_rb_    = nullptr;
        publisher_    = nullptr;
        subscriber_   = nullptr;
        hb_topic_     = nullptr;
        loc_topic_    = nullptr;
        status_topic_ = nullptr;
        destroy_participant(factory_, participant_);
    }

    // Drive the SafeDDS executor for the given duration (single-threaded I/O).
    void spin_for(std::chrono::milliseconds dur)
    {
        const auto end = std::chrono::steady_clock::now() + dur;
        while (std::chrono::steady_clock::now() < end)
        {
            if (executor_ != nullptr)
            {
                while (executor_->has_pending_work())
                {
                    executor_->spin(eprosima::safedds::execution::TIME_ZERO);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void send_heartbeat()
    {
        safe_edge::common::ServiceHeartbeat hb{};
        hb.service_name = "server";
        hb_writer_->write(hb, eprosima::safedds::dds::HANDLE_NIL);
    }

    bool wait_for_status(safe_edge::common::HealthStatus expected, int timeout_s)
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (executor_ != nullptr)
            {
                while (executor_->has_pending_work())
                {
                    executor_->spin(eprosima::safedds::execution::TIME_ZERO);
                }
            }

            safe_edge::edge::EdgeGatewayStatus sample{};
            eprosima::safedds::dds::SampleInfo info{};
            if (status_reader_->take_next_sample(sample, info) ==
                    eprosima::safedds::dds::ReturnCode::OK && info.valid_data)
            {
                const bool is_ok =
                    sample.status == safe_edge::common::HealthStatus::HEALTH_OK;
                std::cout << "[dds] EdgeGatewayStatus="
                          << (is_ok ? "OK" : "DEGRADED") << "\n";
                if (sample.status == expected)
                {
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

    eprosima::safedds::dds::DomainParticipantFactory factory_;
    eprosima::safedds::execution::ISpinnable*        executor_    = nullptr;
    eprosima::safedds::dds::DomainParticipant*  participant_  = nullptr;
    eprosima::safedds::dds::Publisher*           publisher_   = nullptr;
    eprosima::safedds::dds::Subscriber*          subscriber_  = nullptr;
    eprosima::safedds::dds::Topic*               hb_topic_    = nullptr;
    eprosima::safedds::dds::Topic*               loc_topic_   = nullptr;
    eprosima::safedds::dds::Topic*               status_topic_= nullptr;
    eprosima::safedds::dds::DataWriter*          hb_wb_       = nullptr;
    eprosima::safedds::dds::DataWriter*          loc_wb_      = nullptr;
    eprosima::safedds::dds::DataReader*          status_rb_   = nullptr;

    safe_edge::common::ServiceHeartbeatTypeSupport        hb_ts_;
    safe_edge::pilot_server::ChargerLocationTypeSupport   loc_ts_;
    safe_edge::edge::EdgeGatewayStatusTypeSupport         status_ts_;

    eprosima::safedds::dds::TypedDataWriter<
        safe_edge::common::ServiceHeartbeatTypeSupport>*      hb_writer_    = nullptr;
    eprosima::safedds::dds::TypedDataWriter<
        safe_edge::pilot_server::ChargerLocationTypeSupport>* loc_writer_   = nullptr;
    eprosima::safedds::dds::TypedDataReader<
        safe_edge::edge::EdgeGatewayStatusTypeSupport>*       status_reader_= nullptr;
};

// ---------------------------------------------------------------------------
// 1. DEGRADED when no server present
// ---------------------------------------------------------------------------

TEST_F(MockServerFixture, EdgeStatusIsDegradedWithoutServer)
{
    EXPECT_TRUE(wait_for_status(safe_edge::common::HealthStatus::HEALTH_DEGRADED, 12))
        << "Expected DEGRADED within 12 s with no server heartbeat";
}

// ---------------------------------------------------------------------------
// 2. OK when server publishes heartbeats
// ---------------------------------------------------------------------------

TEST_F(MockServerFixture, EdgeStatusIsOkWithServer)
{
    for (int i = 0; i < 3; ++i)
    {
        send_heartbeat();
        spin_for(std::chrono::seconds(2));
    }
    EXPECT_TRUE(wait_for_status(safe_edge::common::HealthStatus::HEALTH_OK, 12))
        << "Expected OK within 12 s after sending heartbeats";
}

// ---------------------------------------------------------------------------
// 3. Recovers from DEGRADED to OK when server starts
// ---------------------------------------------------------------------------

TEST_F(MockServerFixture, EdgeRecoversDegradedToOk)
{
    ASSERT_TRUE(wait_for_status(safe_edge::common::HealthStatus::HEALTH_DEGRADED, 12))
        << "Expected initial DEGRADED state";

    for (int i = 0; i < 3; ++i)
    {
        send_heartbeat();
        spin_for(std::chrono::seconds(2));
    }
    EXPECT_TRUE(wait_for_status(safe_edge::common::HealthStatus::HEALTH_OK, 12))
        << "Expected OK after heartbeats sent";
}

// ---------------------------------------------------------------------------
// 4. Transitions from OK to DEGRADED when server stops
// ---------------------------------------------------------------------------
// Single-threaded design: heartbeat sending and status polling are interleaved
// in the same spin loop to avoid concurrent access to the DDS writer.

TEST_F(MockServerFixture, EdgeTransitionsOkToDegraded)
{
    // Phase 1: send heartbeats every 2 s for up to 12 s; expect HEALTH_OK.
    const auto hb_phase_end =
        std::chrono::steady_clock::now() + std::chrono::seconds(12);
    auto last_hb = std::chrono::steady_clock::now() - std::chrono::seconds(3);
    bool ok_seen = false;

    while (std::chrono::steady_clock::now() < hb_phase_end)
    {
        if (executor_ != nullptr)
        {
            while (executor_->has_pending_work())
            {
                executor_->spin(eprosima::safedds::execution::TIME_ZERO);
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_hb).count() >= 2000)
        {
            send_heartbeat();
            last_hb = now;
        }

        safe_edge::edge::EdgeGatewayStatus sample{};
        eprosima::safedds::dds::SampleInfo info{};
        if (status_reader_->take_next_sample(sample, info) ==
                eprosima::safedds::dds::ReturnCode::OK && info.valid_data &&
                sample.status == safe_edge::common::HealthStatus::HEALTH_OK)
        {
            ok_seen = true;
            std::cout << "[dds] EdgeGatewayStatus=OK\n";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    ASSERT_TRUE(ok_seen) << "Expected HEALTH_OK while heartbeats running";

    // Phase 2: stop sending; edge should go DEGRADED after 10 s HB timeout + ≤5 s status interval.
    EXPECT_TRUE(wait_for_status(safe_edge::common::HealthStatus::HEALTH_DEGRADED, 20))
        << "Expected DEGRADED within 20 s after heartbeats stopped";
}

// ---------------------------------------------------------------------------
// 5. Status published periodically (status_interval_sec = 5)
// ---------------------------------------------------------------------------

TEST(EdgePublishesStatusPeriodically, AtLeastTwoSamplesIn15Seconds)
{
    (void)PEERS_INIT;
    eprosima::safedds::dds::DomainParticipantFactory factory;
    auto* participant = make_participant(factory, "TestPeriodic", next_test_port());
    ASSERT_NE(nullptr, participant);

    safe_edge::edge::EdgeGatewayStatusTypeSupport status_ts;
    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK,
        status_ts.register_type(*participant, status_ts.get_type_name()));

    eprosima::safedds::memory::container::StaticString256 tname(
        safe_edge::edge_module::common::topic_names::edge_gateway_status());
    auto* topic = participant->create_topic(tname, status_ts.get_type_name(),
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
    auto* rb = sub->create_datareader(*topic, rqos, nullptr,
        eprosima::safedds::dds::NONE_STATUS_MASK);
    ASSERT_NE(nullptr, rb);

    auto* reader = eprosima::safedds::dds::TypedDataReader<
        safe_edge::edge::EdgeGatewayStatusTypeSupport>::downcast(*rb);
    ASSERT_NE(nullptr, reader);

    // Enable participant FIRST.
    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, participant->enable());
    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, sub->enable());
    ASSERT_EQ(eprosima::safedds::dds::ReturnCode::OK, rb->enable());

    auto* executor = factory.create_default_executor();
    ASSERT_NE(nullptr, executor);

    // Discovery wait (spin so SPDP exchange completes).
    {
        const auto disc_end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < disc_end)
        {
            while (executor->has_pending_work())
            {
                executor->spin(eprosima::safedds::execution::TIME_ZERO);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    int count = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < end)
    {
        while (executor->has_pending_work())
        {
            executor->spin(eprosima::safedds::execution::TIME_ZERO);
        }

        safe_edge::edge::EdgeGatewayStatus sample{};
        eprosima::safedds::dds::SampleInfo info{};
        if (reader->take_next_sample(sample, info) ==
                eprosima::safedds::dds::ReturnCode::OK && info.valid_data)
        {
            ++count;
            std::cout << "[dds] Status sample #" << count << "\n";
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    EXPECT_GE(count, 2)
        << "Expected >=2 EdgeGatewayStatus samples in 15 s, got " << count;

    executor = nullptr;
    destroy_participant(factory, participant);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new EdgeEnvironment());
    return RUN_ALL_TESTS();
}
