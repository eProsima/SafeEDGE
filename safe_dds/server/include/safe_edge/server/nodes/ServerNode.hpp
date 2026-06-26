#ifndef SAFE_EDGE_SERVER_NODES_SERVERNODE_HPP
#define SAFE_EDGE_SERVER_NODES_SERVERNODE_HPP

#include <safe_edge/server/common/PilotServerClient.hpp>
#include <safe_edge/server/common/RuntimeConfig.hpp>

#include <common.hpp>
#include <pilot_server.hpp>

#include <safedds/dds/DataReader.hpp>
#include <safedds/dds/DataReaderListener.hpp>
#include <safedds/dds/DataWriter.hpp>
#include <safedds/dds/DomainParticipant.hpp>
#include <safedds/dds/DomainParticipantFactory.hpp>
#include <safedds/dds/DomainParticipantListener.hpp>
#include <safedds/dds/Publisher.hpp>
#include <safedds/dds/Subscriber.hpp>
#include <safedds/dds/Topic.hpp>
#include <safedds/dds/TypedDataReader.hpp>
#include <safedds/dds/TypedDataWriter.hpp>
#include <safedds/dds/qos/DomainParticipantQos.hpp>
#include <safedds/execution/Timer.hpp>
#include <safedds/memory/container/StaticString.hpp>

#include <chrono>
#include <cstdint>

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
        public eprosima::safedds::dds::DomainParticipantListener
    {
    public:

        explicit ParticipantListener(ServerNode& owner);

        void on_subscription_matched(
                eprosima::safedds::dds::DataReader& reader,
                const eprosima::safedds::dds::SubscriptionMatchedStatus& info) noexcept override;

        void on_publication_matched(
                eprosima::safedds::dds::DataWriter& writer,
                const eprosima::safedds::dds::PublicationMatchedStatus& info) noexcept override;

    private:

        ServerNode& owner_;
    };

    class ServerQueryListener :
        public eprosima::safedds::dds::DataReaderListener
    {
    public:

        explicit ServerQueryListener(ServerNode& owner);

        void on_data_available(
                eprosima::safedds::dds::DataReader& reader) noexcept override;

    private:

        ServerNode& owner_;
    };

    bool initialize();
    bool create_participant();
    bool register_types();
    bool create_topics();
    bool create_endpoints();
    bool enable_entities();
    bool create_executor();

    void on_server_query_received(const safe_edge::pilot_server::ServerQuery& query);
    void check_pilot_server_liveliness() noexcept;
    void publish_heartbeat();

    void request_pilot_server_data(safe_edge::pilot_server::RequestedDataType resource) noexcept;
    void periodic_refresh_all_resources() noexcept;
    const char* resolve_endpoint(safe_edge::pilot_server::RequestedDataType resource) const noexcept;
    void log_uptime() const noexcept;

    void log_subscription_match(const char* topic_name, int32_t total_count) const;
    void log_publication_match(const char* topic_name, int32_t total_count) const;
    eprosima::safedds::execution::TimePoint next_wakeup_time() const noexcept;

    eprosima::safedds::dds::DomainParticipantFactory factory_;
    common::RuntimeConfig     runtime_config_;
    common::PilotServerClient pilot_client_;

    ParticipantListener participant_listener_;
    ServerQueryListener server_query_listener_;

    eprosima::safedds::execution::Timer heartbeat_timer_;
    eprosima::safedds::execution::Timer refresh_timer_;
    eprosima::safedds::execution::Timer uptime_timer_;
    eprosima::safedds::execution::Timer check_server_liveliness_timer_;
    std::chrono::steady_clock::time_point start_time_;
    bool pilot_server_available_ = true;

    eprosima::safedds::dds::DomainParticipant* participant_ = nullptr;
    eprosima::safedds::dds::Publisher* publisher_ = nullptr;
    eprosima::safedds::dds::Subscriber* subscriber_ = nullptr;
    eprosima::safedds::execution::ISpinnable* executor_ = nullptr;

    eprosima::safedds::dds::Topic* charger_locations_topic_ = nullptr;
    eprosima::safedds::dds::Topic* server_query_topic_ = nullptr;
    eprosima::safedds::dds::Topic* service_heartbeat_topic_ = nullptr;

    eprosima::safedds::memory::container::StaticString256 charger_locations_topic_name_;
    eprosima::safedds::memory::container::StaticString256 server_query_topic_name_;
    eprosima::safedds::memory::container::StaticString256 service_heartbeat_topic_name_;

    eprosima::safedds::dds::DataWriter* charger_locations_datawriter_ = nullptr;
    eprosima::safedds::dds::DataWriter* service_heartbeat_datawriter_ = nullptr;

    eprosima::safedds::dds::DataReader* server_query_datareader_ = nullptr;

    void configure_participant_qos(
            eprosima::safedds::dds::DomainParticipantQos& qos) noexcept;

protected:

    safe_edge::pilot_server::ServerQueryTypeSupport server_query_type_support_;

    eprosima::safedds::dds::TypedDataReader<safe_edge::pilot_server::ServerQueryTypeSupport>* server_query_reader_ = nullptr;

    safe_edge::pilot_server::ChargerLocationTypeSupport charger_locations_type_support_;

    eprosima::safedds::dds::TypedDataWriter<safe_edge::pilot_server::ChargerLocationTypeSupport>* charger_locations_writer_ = nullptr;

    safe_edge::common::ServiceHeartbeatTypeSupport service_heartbeat_type_support_;

    eprosima::safedds::dds::TypedDataWriter<safe_edge::common::ServiceHeartbeatTypeSupport>* service_heartbeat_writer_ = nullptr;
};

} // namespace nodes
} // namespace server
} // namespace safe_edge

#endif // SAFE_EDGE_SERVER_NODES_SERVERNODE_HPP
