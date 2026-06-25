// test_server_integration.cpp
//
// Single binary integration test for safe_edge_server (Fast DDS variant).
// Starts the server as a subprocess, runs all test suites, stops the server.
//
// Usage:
//   ./test_server_integration
//   ./test_server_integration --gtest_output=xml:results.xml   (CI / JUnit)
//
// Environment variables:
//   SAFE_EDGE_FAST_SERVER_BIN  path to safe_edge_server binary (default: safe_edge_server)

#include <common.hpp>
#include <pilot_server.hpp>
#include <commonPubSubTypes.hpp>
#include <pilot_serverPubSubTypes.hpp>
#include <safe_edge/server/common/TopicNames.hpp>

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

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Helper: create a participant peered with the server at 8020
// ---------------------------------------------------------------------------

static eprosima::fastdds::dds::DomainParticipant* make_participant(
        const char* name,
        uint16_t port)
{
    eprosima::fastdds::dds::DomainParticipantQos qos{};
    qos.name(name);

    eprosima::fastdds::rtps::Locator_t announced;
    eprosima::fastdds::rtps::IPLocator::setIPv4(announced, "127.0.0.1");
    announced.port = port;
    qos.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(announced);

    eprosima::fastdds::rtps::Locator_t peer;
    eprosima::fastdds::rtps::IPLocator::setIPv4(peer, "127.0.0.1");
    peer.port = 8020U;
    qos.wire_protocol().builtin.initialPeersList.push_back(peer);

    return eprosima::fastdds::dds::DomainParticipantFactory::get_instance()
        ->create_participant(0U, qos, nullptr,
            eprosima::fastdds::dds::StatusMask::none());
}

// ---------------------------------------------------------------------------
// Helper: poll DataReader until a valid ChargerLocation sample arrives
// ---------------------------------------------------------------------------

static bool wait_for_charger_location(
        eprosima::fastdds::dds::DataReader* reader,
        int timeout_s)
{
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);

    while (std::chrono::steady_clock::now() < deadline)
    {
        safe_edge::pilot_server::ChargerLocation sample{};
        eprosima::fastdds::dds::SampleInfo info{};
        if (reader->take_next_sample(&sample, &info) ==
                eprosima::fastdds::dds::RETCODE_OK && info.valid_data)
        {
            std::cout << "  [dds] Received ChargerLocation id=" << sample.id()
                      << " name=" << sample.name() << "\n";
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

// ---------------------------------------------------------------------------
// Global environment: lifecycle of safe_edge_server subprocess
// ---------------------------------------------------------------------------

class ServerEnvironment : public ::testing::Environment
{
public:
    void SetUp() override
    {
        const char* bin = std::getenv("SAFE_EDGE_FAST_SERVER_BIN");
        const std::string cmd =
            std::string(bin != nullptr ? bin : "safe_edge_server") + " &";
        ASSERT_EQ(0, std::system(cmd.c_str()))
            << "Failed to launch safe_edge_server. "
               "Set SAFE_EDGE_FAST_SERVER_BIN to the full path if needed.";

        std::cout << "[env] safe_edge_server started — waiting 5 s for init...\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::cout << "[env] Server ready.\n";
    }

    void TearDown() override
    {
        std::cout << "[env] Stopping safe_edge_server...\n";
        cont int res = std::system("pkill -f safe_edge_server 2>/dev/null || true");
        (void) res;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
};

// ---------------------------------------------------------------------------
// Suite 1: QueryDispatch
// Verifies that for each RequestedDataType the server dispatches correctly.
// ---------------------------------------------------------------------------

class QueryDispatchTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        participant_ = make_participant("TestQueryDispatch", 8040U);
        ASSERT_NE(nullptr, participant_);

        eprosima::fastdds::dds::TypeSupport charger_ts(
            new safe_edge::pilot_server::ChargerLocationPubSubType());
        eprosima::fastdds::dds::TypeSupport query_ts(
            new safe_edge::pilot_server::ServerQueryPubSubType());

        ASSERT_EQ(eprosima::fastdds::dds::RETCODE_OK,
            charger_ts.register_type(participant_));
        ASSERT_EQ(eprosima::fastdds::dds::RETCODE_OK,
            query_ts.register_type(participant_));

        charger_topic_ = participant_->create_topic(
            safe_edge::server::common::topic_names::charger_locations(),
            charger_ts.get_type_name(),
            eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
        query_topic_ = participant_->create_topic(
            safe_edge::server::common::topic_names::server_query(),
            query_ts.get_type_name(),
            eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
        ASSERT_NE(nullptr, charger_topic_);
        ASSERT_NE(nullptr, query_topic_);

        publisher_ = participant_->create_publisher(
            eprosima::fastdds::dds::PUBLISHER_QOS_DEFAULT, nullptr,
            eprosima::fastdds::dds::StatusMask::none());
        subscriber_ = participant_->create_subscriber(
            eprosima::fastdds::dds::SUBSCRIBER_QOS_DEFAULT, nullptr,
            eprosima::fastdds::dds::StatusMask::none());
        ASSERT_NE(nullptr, publisher_);
        ASSERT_NE(nullptr, subscriber_);

        eprosima::fastdds::dds::DataWriterQos wqos = eprosima::fastdds::dds::DATAWRITER_QOS_DEFAULT;
        wqos.reliability().kind = eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;
        eprosima::fastdds::dds::DataReaderQos rqos = eprosima::fastdds::dds::DATAREADER_QOS_DEFAULT;
        rqos.reliability().kind = eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;

        query_writer_ = publisher_->create_datawriter(
            query_topic_, wqos, nullptr,
            eprosima::fastdds::dds::StatusMask::none());
        charger_reader_ = subscriber_->create_datareader(
            charger_topic_, rqos, nullptr,
            eprosima::fastdds::dds::StatusMask::none());
        ASSERT_NE(nullptr, query_writer_);
        ASSERT_NE(nullptr, charger_reader_);

        std::this_thread::sleep_for(std::chrono::seconds(3)); // discovery
    }

    void send_query(safe_edge::pilot_server::RequestedDataType type)
    {
        safe_edge::pilot_server::ServerQuery q{};
        q.requested_by("test_query_dispatch");
        q.requested_data_type(type);
        EXPECT_EQ(eprosima::fastdds::dds::RETCODE_OK,
            query_writer_->write(&q, eprosima::fastdds::dds::HANDLE_NIL));
    }

    eprosima::fastdds::dds::DomainParticipant* participant_   = nullptr;
    eprosima::fastdds::dds::Publisher*         publisher_     = nullptr;
    eprosima::fastdds::dds::Subscriber*        subscriber_    = nullptr;
    eprosima::fastdds::dds::Topic*             charger_topic_ = nullptr;
    eprosima::fastdds::dds::Topic*             query_topic_   = nullptr;
    eprosima::fastdds::dds::DataWriter*        query_writer_  = nullptr;
    eprosima::fastdds::dds::DataReader*        charger_reader_= nullptr;
};

TEST_F(QueryDispatchTest, ChargerLocationReturnsData)
{
    send_query(safe_edge::pilot_server::RequestedDataType::CHARGER_LOCATION);
    EXPECT_TRUE(wait_for_charger_location(charger_reader_, 5))
        << "No ChargerLocation received within 5 s";
}

TEST_F(QueryDispatchTest, ChargerTypeHandledWithoutCrash)
{
    send_query(safe_edge::pilot_server::RequestedDataType::CHARGER_TYPE);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

TEST_F(QueryDispatchTest, ChargingSessionHandledWithoutCrash)
{
    send_query(safe_edge::pilot_server::RequestedDataType::CHARGING_SESSION);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

TEST_F(QueryDispatchTest, TransitHealthHandledWithoutCrash)
{
    send_query(safe_edge::pilot_server::RequestedDataType::TRANSIT_HEALTH);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

TEST_F(QueryDispatchTest, TransitMetricsHandledWithoutCrash)
{
    send_query(safe_edge::pilot_server::RequestedDataType::TRANSIT_METRICS);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

// ---------------------------------------------------------------------------
// Suite 2: PeriodicRefresh
// Verifies the 30-second timer fires without any explicit query.
// ---------------------------------------------------------------------------

TEST(PeriodicRefresh, TwoBurstsIn35Seconds)
{
    auto* participant = make_participant("TestPeriodicRefresh", 8041U);
    ASSERT_NE(nullptr, participant);

    eprosima::fastdds::dds::TypeSupport charger_ts(
        new safe_edge::pilot_server::ChargerLocationPubSubType());
    ASSERT_EQ(eprosima::fastdds::dds::RETCODE_OK,
        charger_ts.register_type(participant));

    auto* topic = participant->create_topic(
        safe_edge::server::common::topic_names::charger_locations(),
        charger_ts.get_type_name(),
        eprosima::fastdds::dds::TOPIC_QOS_DEFAULT);
    ASSERT_NE(nullptr, topic);

    auto* sub = participant->create_subscriber(
        eprosima::fastdds::dds::SUBSCRIBER_QOS_DEFAULT, nullptr,
        eprosima::fastdds::dds::StatusMask::none());
    ASSERT_NE(nullptr, sub);

    eprosima::fastdds::dds::DataReaderQos rqos = eprosima::fastdds::dds::DATAREADER_QOS_DEFAULT;
    rqos.reliability().kind = eprosima::fastdds::dds::RELIABLE_RELIABILITY_QOS;

    auto* reader = sub->create_datareader(
        topic, rqos, nullptr,
        eprosima::fastdds::dds::StatusMask::none());
    ASSERT_NE(nullptr, reader);

    std::this_thread::sleep_for(std::chrono::seconds(3)); // discovery

    int burst_count = 0;
    const auto t0  = std::chrono::steady_clock::now();
    const auto end = t0 + std::chrono::seconds(35);
    auto last_sample = t0 - std::chrono::seconds(10);

    while (std::chrono::steady_clock::now() < end)
    {
        safe_edge::pilot_server::ChargerLocation sample{};
        eprosima::fastdds::dds::SampleInfo info{};
        if (reader->take_next_sample(&sample, &info) ==
                eprosima::fastdds::dds::RETCODE_OK && info.valid_data)
        {
            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_sample).count() > 2000)
            {
                burst_count++;
                std::cout << "  [dds] Burst " << burst_count << " at t="
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
        << "Expected >=2 bursts (publication_match trigger + 30 s periodic refresh), "
        << "got " << burst_count;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new ServerEnvironment());
    return RUN_ALL_TESTS();
}
