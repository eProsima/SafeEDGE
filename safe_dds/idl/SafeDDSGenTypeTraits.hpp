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
 * @file SafeDDSGenTypeTraits.hpp
 */

#ifndef SAFEDDS_SAFEDDSGEN_SAFEDDSGENTYPETRAITS_HPP
#define SAFEDDS_SAFEDDSGEN_SAFEDDSGENTYPETRAITS_HPP

#include <safedds/memory/Optional.hpp>

#include <array>
#include <vector>
#include <string>
#include <map>

namespace eprosima {
namespace safedds {
namespace gen {

/*
 * @section Basic type traits
 */

//! Is arithmetic or enum
template<typename T>
struct is_arithmetic_or_enum :
    std::integral_constant<bool, std::is_arithmetic<T>::value || std::is_enum<T>::value> {};

//! Is array (false by default)
template<typename T>
struct is_array :
    std::false_type {};

//! Is array (true for std::array)
template<typename U, size_t N>
struct is_array<std::array<U, N>> :
    std::true_type {};

//! Is sequence (false by default)
template<typename T>
struct is_sequence :
    std::false_type {};

//! Is sequence (true for std::vector)
template<typename U>
struct is_sequence<std::vector<U>> :
    std::true_type {};

//! Is map (false by default)
template<typename T>
struct is_map :
    std::false_type {};

//! Is map (true for std::map)
template<typename K, typename V>
struct is_map<std::map<K, V>> :
    std::true_type {};

//! Is string
template<typename T>
struct is_string :
    std::is_same<T, std::string> {};

//! Is optional (false by default)
template<typename T>
struct is_optional :
    std::false_type {};

//! Is optional (true for safedds::memory::Optional)
template<typename T>
struct is_optional<safedds::memory::Optional<T>> :
    std::true_type {};

//! Is external type (false by default)
template<typename T>
struct is_external :
    std::false_type {};

//! Is external type
template<typename T>
struct External;
template<typename T>
struct is_external<External<T>> :
    std::true_type {};

//! Is external array type (false by default)
template<typename T>
struct is_external_array :
    std::false_type {};

//! Is external array type
template<typename T, uint32_t N>
struct ExternalArray;
template<typename T, uint32_t N>
struct is_external_array<ExternalArray<T, N>> :
    std::true_type {};

//! Is external sequence type (false by default)
template<typename T>
struct is_external_sequence :
    std::false_type {};

//! Is external sequence type
template<typename T>
struct ExternalSequence;
template<typename T>
struct is_external_sequence<ExternalSequence<T>> :
    std::true_type {};

//! Is external string type (false by default)
template<typename T>
struct is_external_string :
    std::false_type {};

//! Is external string type
struct ExternalString;
template<>
struct is_external_string<ExternalString> :
    std::true_type {};

//! Is IDLUnion (not C/C++ union, false by default)
struct IDLUnion;
template<typename T>
struct is_idl_union :
    std::is_base_of<IDLUnion, T> {};

/*
 * @section Dheader type traits
 */

//! By default all types require dheader (included sequences of sequences)
template<typename T,
        typename = typename std::enable_if<is_sequence<T>::value || is_array<T>::value || is_external_sequence<T>::value || is_external_array<T>::value>::type,
        typename Enable = void>
struct sequence_or_array_require_dheader :
    std::true_type {};

//! Specialization for arithmetic types and enums do not require dheader
template<typename T>
struct sequence_or_array_require_dheader<T, typename std::enable_if<
            is_arithmetic_or_enum<typename T::value_type>::value>::type> :
    std::false_type {};

//! Specialization for array types
template<typename T>
struct sequence_or_array_require_dheader<T, typename std::enable_if<
            is_array<typename T::value_type>::value>::type> :
    sequence_or_array_require_dheader<typename T::value_type> {};

//! Specialization for maps
template<typename K, typename V, typename Enable = void>
struct map_require_dheader :
    std::true_type {};

//! Specialization for maps with arithmetic types
template<typename K, typename V>
struct map_require_dheader<K, V,
        typename std::enable_if<is_arithmetic_or_enum<K>::value && is_arithmetic_or_enum<V>::value>::type> :
    std::false_type {};

/*
 * @section IDL extensibility kind type traits
 */

//! @brief IDL extensibility kind
enum class IDLExtensibilityKind : uint8_t
{
    FINAL,
    APPENDABLE,
    MUTABLE
};

//! By default get the IDL extensibility kind and check if it is FINAL
template<typename T, typename Enable = void>
struct is_idl_extensibility_final
{
    static constexpr bool value = T::IDL_EXTENSIBILITY == IDLExtensibilityKind::FINAL;
};

/*
 * @brief Specialization for primitive and container types that are considered extensibility FINAL
 *
 * @note The following types are considered extensibility FINAL:
 * - Arithmetic types
 * - Enums
 * - Arrays
 * - Sequences
 * - Maps
 * - Strings
 * - Optional
 * - External types
 */
template<typename V>
struct is_idl_extensibility_final<V,
        typename std::enable_if<is_arithmetic_or_enum<V>::value || is_array<V>::value || is_sequence<V>::value || is_map<V>::value || is_string<V>::value || is_optional<V>::value || is_external<V>::value || is_external_array<V>::value || is_external_sequence<V>::value || is_external_string<V>::value>::type>
{
    static constexpr bool value = true;
};

//! Is Safe DDS serialization basic type
template<typename T, typename Enable = void>
struct is_safe_dds_serialization_basic_type :
    std::false_type {};

//! Is Safe DDS serialization basic type (true for all char, bool, uint8_t, uint16_t, uint32_t, uint64_t, int8_t, int16_t, int32_t, int64_t, float and double)
template<typename T>
struct is_safe_dds_serialization_basic_type<T,
        typename std::enable_if<
            std::is_same<T, char>::value ||
            std::is_same<T, bool>::value ||
            std::is_same<T, uint8_t>::value ||
            std::is_same<T, uint16_t>::value ||
            std::is_same<T, uint32_t>::value ||
            std::is_same<T, uint64_t>::value ||
            std::is_same<T, int8_t>::value ||
            std::is_same<T, int16_t>::value ||
            std::is_same<T, int32_t>::value ||
            std::is_same<T, int64_t>::value ||
            std::is_same<T, float>::value ||
            std::is_same<T, double>::value>::type
        > :
    std::true_type {};

template<typename T,
        typename std::enable_if<is_sequence<T>::value || is_array<T>::value || is_external_sequence<T>::value || is_external_array<T>::value,
        int>::type = 0>
struct safe_dds_serialization_container_utils
{
    // Serialize complex types (except bool vectors) by element
    static constexpr bool serialize_container_by_element =
            !is_safe_dds_serialization_basic_type<typename T::value_type>::value &&
            !std::is_same<T, std::vector<bool>>::value;

    // Serialize bool vectors by element
    static constexpr bool serialize_bool_vector_by_element =
            std::is_same<T, std::vector<bool>>::value;

    // Serialize basic types
    static constexpr bool serialize_basic_type =
            !serialize_container_by_element && !serialize_bool_vector_by_element;

    // Deserialize complex types (except bool vectors) by element
    static constexpr bool deserialize_container_by_element =
            !is_safe_dds_serialization_basic_type<typename T::value_type>::value &&
            !std::is_same<T, std::vector<bool>>::value;

    // Deserialize bool vectors by element
    static constexpr bool deserialize_bool_vector_by_element =
            std::is_same<T, std::vector<bool>>::value;

    // Deserilize basic types
    static constexpr bool deserialize_basic_type =
            !deserialize_container_by_element && !deserialize_bool_vector_by_element;
};

} // namespace gen
} // namespace safedds
} // namespace eprosima

#endif // SAFEDDS_SAFEDDSGEN_SAFEDDSGENTYPETRAITS_HPP
