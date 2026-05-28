// test_edge_integration.cpp — Fast DDS variant
//
// Integration tests for EdgeGatewayNode health state machine.
// Starts safe_edge_edge_gateway as a subprocess, runs all suites, stops it.
//
// Usage:
//   ./test_edge_integration
//   ./test_edge_integration --gtest_output=xml:results.xml
//
// Environment variables:
//   SAFE_EDGE_FAST_EDGE_BIN  path to safe_edge_edge_gateway (default: safe_edge_edge_gateway)

#include <common.hpp>
#include <edge.hpp>
#include <pilot_server.hpp>
#include <commonPubSubTypes.hpp>
#include <edgePubSubTypes.hpp>
#include <pilot_serverPubSubTypes.hpp>
#include <safe_edge/edge_module/common/TopicNames.hpp>

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/dds/publisher/qos/PublisherQos.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/subscriber/qos/SubscriberQos.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/topic/qos/TopicQos.hpp>
#include <fastdds/rtps/common/Locator.hpp>
#include <fastdds/utils/IPLocator.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

// Edge participant port (from RuntimeConfig)
static constexpr uint16_t EDGE_PORT = 8030U;

// ---------------------------------------------------------------------------
// Helper: participant peered with edge at EDGE_PORT
// ---------------------------------------------------------------------------

static eprosima::fastdds::dds::DomainParticipant* make_participant(
        const char* name, uint16_t port)
{
    eprosima::fastdds::dds::DomainParticipantQos qos{};
    qos.name(name);

    eprosima::fastdds::rtps::Locator_t announced;
    eprosima::fastdds::rtps::IPLocator::setIPv4(announced, "127.0.0.1");
    announced.port = port;
    qos.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(announced);

    eprosima::fastdds::rtps::Locator_t peer;
    eprosima::fastdds::rtps::IPLocator::setIPv4(peer, "127.0.0.1");
    peer.port = EDGE_PORT;
    qos.wire_protocol().builtin.initialPeersList.push_back(peer);

    return eprosima::fastdds::dds::DomainParticipantFactory::get_instance()
        ->create_participant(0U, qos, nullptr,
            eprosima::fastdds::dds::StatusMask::none());
}

// ---------------------------------------------------------------------------
// Global environment: lifecycle of safe_edge_edge_gateway subprocess
// ---------------------------------------------------------------------------

class EdgeEnvironment : public ::testing::Environment
{
public:
    void SetUp() override
    {
        const char* bin = std::getenv("SAFE_EDGE_FAST_EDGE_BIN");
        const std::string cmd =
            std::string(bin != nullptr ? bin : "safe_edge_edge_gateway") + " &";
        ASSERT_EQ(0, std::system(cmd.c_str()))
            << "Failed to launch safe_edge_edge_gateway. "
               "Set SAFE_EDGE_FAST_EDGE_BIN to the full path if needed.";
        std::cout << "[env] safe_edge_edge_gateway started — waiting 5 s for init...\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::cout << "[env] Edge ready.\n";
    }

    void TearDown() override
    {
        std::cout << "[env] Stopping safe_edge_edge_gateway...\n";
        std::system("pkill -f safe_edge_edge_gateway 2>/dev/null || true");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
};

// ---------------------------------------------------------------------------
// Fixture: mock server participant (port 8050, peered with edge at 8030)
// Publishes: ServiceHeartbeat, ChargerLocation
// Reads:     EdgeGatewayStatus
// ---------------------------------------------------------------------------

class MockServerFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        participant_ = make_participant("TestMockServer", 8050U);
        ASSERT_NE(nullptr, participant_);

        eprosima::fastdds::dds::TypeSupport hb_ts(
            new safe_edge::common::ServiceHeartbeatPubSubType());
        eprosima::fastdds::dds::TypeSupport loc_ts(
            new safe_edge::pilot_server::ChargerLocationPubSubType());
        eprosima::fastdds::dds::TypeSupport status_ts(
            new safe_edge::edge::EdgeGatewayStatusPubSubType());

        ASSERT_EQ(eprosima::fastdds::dds::RETCODE_OK, hb_ts.register_type(participant_));
        ASSERT_EQ(eprosima::fastdds::dds::RETCODE_OK, loc_ts.register_type(participant_));
        ASSERT_EQ(eprosima::fastdds::dds::RETCODE_OK, status_ts.register_type(participant_));

        namespace TN = safe_edge::edge_module::common::topic_names;
        hb_topic_ = participant_->create_topic(
            TN::service_heartbeat(), hb_ts.get_type_name(),
            eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
        loc_topic_ = participant_->create_topic(
            TN::charger_locations(), loc_ts.get_type_name(),
            eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
        status_topic_ = participant_->create_topic(
            TN::edge_gateway_status(), status_ts.get_type_name(),
            eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
        ASSERT_NE(nullptr, hb_topic_);
        ASSERT_NE(nullptr, loc_topic_);
        ASSERT_NE(nullptr, status_topic_);

        publisher_ = participant_->create_publisher(
            eprosima::fastdds::dds::PUBLISHER_QOS_DEFAULT, nullptr,
            eprosima::fastdds::dds::StatusMask::none());
        subscriber_ = participant_->create_subscriber(
            eprosima::fastdds::dds::SUBSCRIBER_QOS_DEFAULT, nullptr,
            eprosima::fastdds::dds::StatusMask::none());
        ASSERT_NE(nullptr, publisher_);
        ASSERT_NE(nullptr, subscriber_);

        eprosima::fastdds::dds::DataWriterQos wqos =
            eprosima::fastdds::dds::DATAWRITER_QOS_DEFAULT;
        wqos.reliability().kind = eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;
        eprosima::fastdds::dds::DataReaderQos rqos =
            eprosima::fastdds::dds::DATAREADER_QOS_DEFAULT;
        rqos.reliability().kind = eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;

        hb_writer_ = publisher_->create_datawriter(
            hb_topic_, wqos, nullptr, eprosima::fastdds::dds::StatusMask::none());
        loc_writer_ = publisher_->create_datawriter(
            loc_topic_, wqos, nullptr, eprosima::fastdds::dds::StatusMask::none());
        status_reader_ = subscriber_->create_datareader(
            status_topic_, rqos, nullptr, eprosima::fastdds::dds::StatusMask::none());
        ASSERT_NE(nullptr, hb_writer_);
        ASSERT_NE(nullptr, loc_writer_);
        ASSERT_NE(nullptr, status_reader_);

        std::this_thread::sleep_for(std::chrono::seconds(3)); // discovery
    }

    void TearDown() override
    {
        if (participant_ != nullptr)
        {
            participant_->delete_contained_entities();
            eprosima::fastdds::dds::DomainParticipantFactory::get_instance()
                ->delete_participant(participant_);
            participant_ = nullptr;
        }
    }

    void send_heartbeat()
    {
        safe_edge::common::ServiceHeartbeat hb{};
        hb.service_name("server");
        EXPECT_EQ(eprosima::fastdds::dds::RETCODE_OK,
            hb_writer_->write(&hb, eprosima::fastdds::dds::HANDLE_NIL));
    }

    bool wait_for_status(safe_edge::common::HealthStatus expected, int timeout_s)
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
        while (std::chrono::steady_clock::now() < deadline)
        {
            safe_edge::edge::EdgeGatewayStatus sample{};
            eprosima::fastdds::dds::SampleInfo info{};
            if (status_reader_->take_next_sample(&sample, &info) ==
                    eprosima::fastdds::dds::RETCODE_OK && info.valid_data)
            {
                const bool is_ok =
                    sample.status() == safe_edge::common::HealthStatus::HEALTH_OK;
                std::cout << "[dds] EdgeGatewayStatus="
                          << (is_ok ? "OK" : "DEGRADED") << "\n";
                if (sample.status() == expected)
                {
                    return true;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        return false;
    }

    eprosima::fastdds::dds::DomainParticipant* participant_  = nullptr;
    eprosima::fastdds::dds::Publisher*         publisher_    = nullptr;
    eprosima::fastdds::dds::Subscriber*        subscriber_   = nullptr;
    eprosima::fastdds::dds::Topic*             hb_topic_     = nullptr;
    eprosima::fastdds::dds::Topic*             loc_topic_    = nullptr;
    eprosima::fastdds::dds::Topic*             status_topic_ = nullptr;
    eprosima::fastdds::dds::DataWriter*        hb_writer_    = nullptr;
    eprosima::fastdds::dds::DataWriter*        loc_writer_   = nullptr;
    eprosima::fastdds::dds::DataReader*        status_reader_= nullptr;
};

// ---------------------------------------------------------------------------
// 1. DEGRADED when no server present
// ---------------------------------------------------------------------------

TEST_F(MockServerFixture, EdgeStatusIsDegradedWithoutServer)
{
    // server_available_ starts false → first status (≤5 s) is DEGRADED
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
        std::this_thread::sleep_for(std::chrono::seconds(2));
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
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    EXPECT_TRUE(wait_for_status(safe_edge::common::HealthStatus::HEALTH_OK, 12))
        << "Expected OK after heartbeats sent";
}

// ---------------------------------------------------------------------------
// 4. Transitions from OK to DEGRADED when server stops
// ---------------------------------------------------------------------------

TEST_F(MockServerFixture, EdgeTransitionsOkToDegraded)
{
    std::atomic<bool> hb_running{true};
    std::thread hb_thread([&]()
    {
        while (hb_running.load())
        {
            send_heartbeat();
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    });

    ASSERT_TRUE(wait_for_status(safe_edge::common::HealthStatus::HEALTH_OK, 12))
        << "Expected OK while heartbeats are running";

    // Stop heartbeats — DEGRADED after SERVER_HB_TIMEOUT_MS (10 s) +
    // next status interval (≤5 s) → allow 20 s
    hb_running.store(false);
    hb_thread.join();

    EXPECT_TRUE(wait_for_status(safe_edge::common::HealthStatus::HEALTH_DEGRADED, 20))
        << "Expected DEGRADED within 20 s after heartbeats stopped";
}

// ---------------------------------------------------------------------------
// 5. Status published periodically (status_interval_sec = 5)
// ---------------------------------------------------------------------------

TEST(EdgePublishesStatusPeriodically, AtLeastTwoSamplesIn15Seconds)
{
    auto* participant = make_participant("TestPeriodic", 8051U);
    ASSERT_NE(nullptr, participant);

    eprosima::fastdds::dds::TypeSupport status_ts(
        new safe_edge::edge::EdgeGatewayStatusPubSubType());
    ASSERT_EQ(eprosima::fastdds::dds::RETCODE_OK, status_ts.register_type(participant));

    auto* topic = participant->create_topic(
        safe_edge::edge_module::common::topic_names::edge_gateway_status(),
        status_ts.get_type_name(),
        eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
    ASSERT_NE(nullptr, topic);

    auto* sub = participant->create_subscriber(
        eprosima::fastdds::dds::SUBSCRIBER_QOS_DEFAULT, nullptr,
        eprosima::fastdds::dds::StatusMask::none());
    ASSERT_NE(nullptr, sub);

    eprosima::fastdds::dds::DataReaderQos rqos =
        eprosima::fastdds::dds::DATAREADER_QOS_DEFAULT;
    rqos.reliability().kind = eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;
    auto* reader = sub->create_datareader(
        topic, rqos, nullptr, eprosima::fastdds::dds::StatusMask::none());
    ASSERT_NE(nullptr, reader);

    std::this_thread::sleep_for(std::chrono::seconds(3)); // discovery

    int count = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < end)
    {
        safe_edge::edge::EdgeGatewayStatus sample{};
        eprosima::fastdds::dds::SampleInfo info{};
        if (reader->take_next_sample(&sample, &info) ==
                eprosima::fastdds::dds::RETCODE_OK && info.valid_data)
        {
            ++count;
            std::cout << "[dds] Status sample #" << count << "\n";
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    EXPECT_GE(count, 2)
        << "Expected >=2 EdgeGatewayStatus samples in 15 s, got " << count;
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
