/**
 * Copyright (C) 2024, Proyectos y Sistemas de Mantenimiento SL (eProsima)
 *
 * This program is commercial software licensed under the terms of the
 * eProsima Software License Agreement Rev 03 (the "License")
 *
 * You may obtain a copy of the License at
 * https://www.eprosima.com/licenses/LICENSE-REV03
 *
 */

/**
 * @file SafeDDSGenUtils.hpp
 */

#ifndef SAFEDDS_SAFEDDSGEN_SAFEDDSGENUTILS_HPP
#define SAFEDDS_SAFEDDSGEN_SAFEDDSGENUTILS_HPP

#include <SafeDDSGenTypeTraits.hpp>

#include <safedds/ReturnCode.hpp>
#include <safedds/portable/StdInt.hpp>

namespace eprosima {
namespace safedds {
namespace gen {

template<typename T>
struct ExternalSequence
{
    static_assert(is_safe_dds_serialization_basic_type<T>::value || std::is_enum<T>::value,
            "T must be a basic or enum type");

    using value_type = T;

    uint32_t size() const
    {
        return len;
    }

    T* data() const
    {
        return member;
    }

    bool empty() const
    {
        return (member == nullptr) || (len == 0);
    }

    T* member = nullptr;
    uint32_t len = 0;
    uint32_t capacity  = 0;
};

template<typename T, uint32_t N>
struct ExternalArray
{
    static_assert(is_safe_dds_serialization_basic_type<T>::value || std::is_enum<T>::value,
            "T must be a basic or enum type");

    using value_type = T;

    uint32_t size() const
    {
        return N;
    }

    T* data() const
    {
        return member;
    }

    bool empty() const
    {
        return (member == nullptr) || (N == 0);
    }

    T* member = nullptr;
};

template<typename T>
struct External
{
    static_assert(is_safe_dds_serialization_basic_type<T>::value || std::is_enum<T>::value,
            "T must be a basic or enum type");

    using value_type = T;

    T* member = nullptr;
};

struct ExternalString
{
    uint32_t size() const
    {
        return len;
    }

    const char* data() const
    {
        return member;
    }

    bool empty() const
    {
        return (member == nullptr) || (len == 0);
    }

    const char* member = nullptr;
    uint32_t len = 0;
    uint32_t capacity  = 0;
};

//! @brief Union type marker
struct IDLUnion
{
    virtual uint32_t get_current_union_size() const = 0;
};

/**
 * @brief EncodingAlgorithm enumeration.
 */
enum class EncodingAlgorithm
{
    PLAIN_CDR,
    PL_CDR,
    PLAIN_CDR2,
    DELIMITED_CDR,
    PL_CDR2
};

//! @brief DDS-XTypes Reserved Parameter IDs
constexpr uint16_t PID_EXTENDED = 0x3F01;
constexpr uint16_t PID_LIST_END = 0x3F02;
constexpr uint16_t PID_IGNORE = 0x3F03;

//! @brief Maximum short Parameter ID
constexpr uint16_t PID_MAX_SHORT = 0x3F00;
constexpr uint32_t PID_MAX_CDRV2 = 0x0FFFFFFF;

/**
 * @brief Retrieves the value of the parameter ID.
 *
 * @param pid The parameter ID.
 * @param extended Flag indicating if the parameter ID is extended.
 *
 * @return The value of the parameter ID.
 */
constexpr uint32_t get_pid_value(
        uint32_t pid,
        bool extended)
{
    // Take into account the size of the parameter ID
    return extended ? (pid & 0x3FFFFFFF) : (pid & 0x3FFF);
}

/**
 * @brief Checks if the parameter ID is an implementation extension.
 *
 * @param pid The parameter ID.
 * @param extended Flag indicating if the parameter ID is extended.
 *
 * @return true if the parameter ID is an implementation extension, false otherwise.
 */
constexpr bool is_pid_implementation_extension(
        uint32_t pid,
        bool extended)
{
    return extended ? (pid & 0x80000000) : 0 != (pid & 0x8000);
}

/**
 * @brief Checks if the parameter ID must be understood.
 *
 * @param pid The parameter ID.
 * @param extended Flag indicating if the parameter ID is extended.
 *
 * @return true if the parameter ID must be understood, false otherwise.
 */
constexpr bool is_pid_must_understand(
        uint16_t pid,
        bool extended)
{
    return extended ? (pid & 0x40000000) : 0 != (pid & 0x4000);
}

/**
 * @brief Resize a sequence to a given size.
 */
template<typename V>
typename std::enable_if<is_sequence<V>::value || is_string<V>::value, safedds::ReturnCode>::type resize_container(
        V& container,
        const uint32_t& size)
{
    container.resize(size);

    return safedds::ReturnCode::OK;
}

template<typename V>
typename std::enable_if<is_external_sequence<V>::value || is_external_string<V>::value,
        safedds::ReturnCode>::type resize_container(
        V& container,
        const uint32_t& size)
{
    safedds::ReturnCode ret = safedds::ReturnCode::OK;

    if (size > container.capacity)
    {
        ret = safedds::ReturnCode::ERROR;
    }
    else
    {
        container.len = size;
    }

    return ret;
}

} // namespace gen
} // namespace safedds
} // namespace eprosima

#endif // SAFEDDS_SAFEDDSGEN_SAFEDDSGENUTILS_HPP
