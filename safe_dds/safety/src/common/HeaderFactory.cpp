#include <safe_edge/safety_domain/common/HeaderFactory.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <utility>

namespace safe_edge {
namespace safety_domain {
namespace common {

HeaderFactory::HeaderFactory(
        std::string source_name)
    : source_name_(std::move(source_name))
{
}

safe_edge::common::Header HeaderFactory::make_header(
        const char* trace_suffix)
{
    safe_edge::common::Header header;
    header.source = source_name_;
    header.timestamp_ms = now_ms();
    header.trace_id = source_name_;
    header.trace_id += "-";

    std::array<char, 32> counter_buffer{};
    std::snprintf(
        counter_buffer.data(),
        counter_buffer.size(),
        "%llu",
        static_cast<unsigned long long>(counter_++));
    header.trace_id += counter_buffer.data();

    if (nullptr != trace_suffix && trace_suffix[0] != '\0')
    {
        header.trace_id += "-";
        header.trace_id += trace_suffix;
    }

    return header;
}

uint64_t HeaderFactory::now_ms() noexcept
{
    const auto now = std::chrono::system_clock::now();
    const auto since_epoch = now.time_since_epoch();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count());
}

} // namespace common
} // namespace safety_domain
} // namespace safe_edge
