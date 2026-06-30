#ifndef SAFE_EDGE_SERVER_NODES_SERVERNODE_HPP
#define SAFE_EDGE_SERVER_NODES_SERVERNODE_HPP

#include <safe_edge/server/common/PilotServerClient.hpp>
#include <safe_edge/server/common/RuntimeConfig.hpp>

#include <common.hpp>
#include <pilot_server.hpp>

#include <fastdds/dds/core/status/StatusMask.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>

#include <chrono>
#include <cstdint>
#include <string>

namespace safe_edge {
namespace server {
namespace nodes {

class ServerNode
{
public:

    explicit ServerNode(const common::RuntimeConfig& runtime_config);

    int run();

private:

    class ParticipantListener :
        public eprosima::fastdds::dds::DomainParticipantListener
    {
    public:

        explicit ParticipantListener(ServerNode& owner);

        void on_subscription_matched(
                eprosima::fastdds::dds::DataReader* reader,
                const eprosima::fastdds::dds::SubscriptionMatchedStatus& info) noexcept override;

        void on_publication_matched(
                eprosima::fastdds::dds::DataWriter* writer,
                const eprosima::fastdds::dds::PublicationMatchedStatus& info) noexcept override;

    private:

        ServerNode& owner_;
    };

    class ServerQueryListener :
        public eprosima::fastdds::dds::DataReaderListener
    {
    public:

        explicit ServerQueryListener(ServerNode& owner);

        void on_data_available(
                eprosima::fastdds::dds::DataReader* reader) noexcept override;

    private:

        ServerNode& owner_;
    };

    bool initialize();
    bool create_participant();
    bool register_types();
    bool create_topics();
    bool create_endpoints();
    bool enable_entities();

    void on_server_query_received(const safe_edge::pilot_server::ServerQuery& query);
    void check_pilot_server_liveliness() noexcept;
    void publish_heartbeat();

    void request_pilot_server_data(safe_edge::pilot_server::RequestedDataType resource) noexcept;
    void periodic_refresh_all_resources() noexcept;
    const char* resolve_endpoint(safe_edge::pilot_server::RequestedDataType resource) const noexcept;
    void log_uptime() const noexcept;

    void log_subscription_match(const char* topic_name, int32_t total_count) const;
    void log_publication_match(const char* topic_name, int32_t total_count) const;

    common::RuntimeConfig     runtime_config_;
    common::PilotServerClient pilot_client_;

    ParticipantListener participant_listener_;
    ServerQueryListener server_query_listener_;

    std::chrono::steady_clock::time_point next_heartbeat_fire_;
    std::chrono::steady_clock::time_point next_refresh_fire_;
    std::chrono::steady_clock::time_point next_server_liveliness_fire_;
    std::chrono::steady_clock::time_point next_uptime_fire_;
    std::chrono::steady_clock::time_point start_time_;
    bool pilot_server_available_ = true;

    eprosima::fastdds::dds::DomainParticipant* participant_ = nullptr;
    eprosima::fastdds::dds::Publisher* publisher_ = nullptr;
    eprosima::fastdds::dds::Subscriber* subscriber_ = nullptr;

    eprosima::fastdds::dds::Topic* charger_locations_topic_ = nullptr;
    eprosima::fastdds::dds::Topic* server_query_topic_ = nullptr;
    eprosima::fastdds::dds::Topic* service_heartbeat_topic_ = nullptr;

    std::string charger_locations_topic_name_;
    std::string server_query_topic_name_;
    std::string service_heartbeat_topic_name_;

    eprosima::fastdds::dds::DataWriter* charger_locations_datawriter_ = nullptr;
    eprosima::fastdds::dds::DataWriter* service_heartbeat_datawriter_ = nullptr;
    eprosima::fastdds::dds::DataReader* server_query_datareader_ = nullptr;

    eprosima::fastdds::dds::TypeSupport charger_location_type_support_;
    eprosima::fastdds::dds::TypeSupport server_query_type_support_;
    eprosima::fastdds::dds::TypeSupport service_heartbeat_type_support_;
};

} // namespace nodes
} // namespace server
} // namespace safe_edge

#endif // SAFE_EDGE_SERVER_NODES_SERVERNODE_HPP
