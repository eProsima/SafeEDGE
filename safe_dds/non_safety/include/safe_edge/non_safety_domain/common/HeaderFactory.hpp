#ifndef SAFE_EDGE_NON_SAFETY_DOMAIN_COMMON_HEADERFACTORY_HPP
#define SAFE_EDGE_NON_SAFETY_DOMAIN_COMMON_HEADERFACTORY_HPP

#include <common.hpp>

#include <cstdint>
#include <string>

namespace safe_edge {
namespace non_safety_domain {
namespace common {

class HeaderFactory
{
public:

    explicit HeaderFactory(std::string source_name);

    safe_edge::common::Header make_header(
            const char* trace_suffix = nullptr);

    static uint64_t now_ms() noexcept;

private:

    std::string source_name_;
    uint64_t counter_ = 0U;
};

} // namespace common
} // namespace non_safety_domain
} // namespace safe_edge

#endif // SAFE_EDGE_NON_SAFETY_DOMAIN_COMMON_HEADERFACTORY_HPP
