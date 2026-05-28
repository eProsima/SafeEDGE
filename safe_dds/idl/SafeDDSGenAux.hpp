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
 * @file SafeDDSGenAux.hpp
 */

#ifndef SAFEDDS_SAFEDDSGEN_SAFEDDSGENAUX_HPP
#define SAFEDDS_SAFEDDSGEN_SAFEDDSGENAUX_HPP

#include <safedds/platform/Endianness.hpp>
#include <safedds/serialization/cdr/Serializer.hpp>
#include <safedds/serialization/cdr/Deserializer.hpp>
#include <safedds/serialization/cdr/rtps/ParameterHeader.hpp>
#include <safedds/memory/byte_array/ByteArrayView.hpp>
#include <safedds/memory/Optional.hpp>
#include <safedds/portable/SafeCasts.hpp>

#include <SafeDDSGenTypeTraits.hpp>
#include <SafeDDSGenUtils.hpp>

#include <array>
#include <vector>
#include <string>
#include <map>

namespace eprosima {
namespace safedds {
namespace gen {

//! Forward declarations
class SafeDDSTypeSupportSerializer;
class SafeDDSTypeSupportDeserializer;

/**
 * @brief SafeDDSTypeSupportSerializer class.
 */
class SafeDDSTypeSupportSerializer :
    public safedds::serialization::cdr::Serializer
{
public:

    /**
     * @brief Constructor
     *
     * @param buffer The byte array view used for serialization.
     * @param endianness The endianness to use for serialization.
     * @param xcdr_version The XCDR version to use for serialization.
     */
    SafeDDSTypeSupportSerializer(
            safedds::memory::IByteArrayView& buffer,
            safedds::platform::Endianness endianness,
            safedds::serialization::cdr::XCDRVersion xcdr_version) noexcept
        : safedds::serialization::cdr::Serializer(buffer, endianness, xcdr_version)
        , endianness_(endianness)
        , encoding_algorithm_(
            safedds::serialization::cdr::XCDRVersion::XCDRV1 == xcdr_version ?
            EncodingAlgorithm::PLAIN_CDR :
            EncodingAlgorithm::PLAIN_CDR2)
    {
    }

    /**
     * @brief Constructor for emulated serialization.
     *
     * @param endianness The endianness to use for serialization.
     * @param xcdr_version The XCDR version to use for serialization.
     */
    SafeDDSTypeSupportSerializer(
            safedds::platform::Endianness endianness,
            safedds::serialization::cdr::XCDRVersion xcdr_version) noexcept
        : safedds::serialization::cdr::Serializer(endianness, xcdr_version)
        , endianness_(endianness)
        , encoding_algorithm_(
            safedds::serialization::cdr::XCDRVersion::XCDRV1 == xcdr_version ?
            EncodingAlgorithm::PLAIN_CDR :
            EncodingAlgorithm::PLAIN_CDR2)
    {
    }

    /**
     * @brief Get XCDR version
     *
     * @return XCDRVersion
     */
    safedds::serialization::cdr::XCDRVersion get_xcdr_version() const
    {
        return xcdr_version_;
    }

    /**
     * @brief Retrieves the encoding algorithm.
     *
     * @return EncodingAlgorithm
     */
    EncodingAlgorithm get_encoding_algorithm() const
    {
        return encoding_algorithm_;
    }

    /**
     * @brief Sets the encoding algorithm.
     *
     * @param encoding_algorithm The encoding algorithm to use for serialization.
     *
     * @return ReturnCode
     */
    safedds::ReturnCode set_encoding_algorithm(
            const EncodingAlgorithm& encoding_algorithm)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        // Check compatibility of XCDR version and encoding algorithm
        if (safedds::serialization::cdr::XCDRVersion::XCDRV1 == xcdr_version_)
        {
            ret = (encoding_algorithm == EncodingAlgorithm::PLAIN_CDR) ||
                    (encoding_algorithm == EncodingAlgorithm::PL_CDR) ?
                    safedds::ReturnCode::OK :
                    safedds::ReturnCode::ERROR;
        }
        else if (safedds::serialization::cdr::XCDRVersion::XCDRV2 == xcdr_version_)
        {
            ret = (encoding_algorithm == EncodingAlgorithm::PLAIN_CDR2) ||
                    (encoding_algorithm == EncodingAlgorithm::PL_CDR2) ||
                    (encoding_algorithm == EncodingAlgorithm::DELIMITED_CDR) ?
                    safedds::ReturnCode::OK :
                    safedds::ReturnCode::ERROR;
        }
        else
        {
            ret = safedds::ReturnCode::ERROR;
        }

        if (safedds::ReturnCode::OK == ret)
        {
            encoding_algorithm_ = encoding_algorithm;
        }

        return ret;
    }

    /**
     * @brief Begins the serialization of a type.
     *
     * @return ReturnCode
     */
    safedds::ReturnCode begin_serialize_type()
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        if (EncodingAlgorithm::PL_CDR2 == encoding_algorithm_ ||
                EncodingAlgorithm::DELIMITED_CDR == encoding_algorithm_)
        {
            ret = serializer_align_to(4U);

            if (safedds::ReturnCode::OK == ret)
            {
                if (!emulated_serialization_)
                {
                    object_dheader_view_.mutable_set(&buffer_[position_], sizeof(uint32_t));
                }

                ret = serializer_skip(sizeof(uint32_t));
            }

            object_dheader_begin_position_ = position_;
        }

        return ret;
    }

    /**
     * @brief Ends the serialization of a type.
     *
     * @return ReturnCode
     */
    safedds::ReturnCode end_serialize_type()
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        if (EncodingAlgorithm::PL_CDR == encoding_algorithm_)
        {
            uint8_t* dummy = nullptr;
            ret = serialize_parameter(PID_LIST_END, dummy);
        }

        if (safedds::ReturnCode::OK == ret && !emulated_serialization_ &&
                (EncodingAlgorithm::PL_CDR2 == encoding_algorithm_ ||
                EncodingAlgorithm::DELIMITED_CDR == encoding_algorithm_))
        {
            uint32_t dheader_length = position_ - object_dheader_begin_position_;
            SafeDDSTypeSupportSerializer dheader_length_serializer(object_dheader_view_, endianness_, xcdr_version_);
            ret = dheader_length_serializer.serialize(dheader_length);
        }

        return ret;
    }

    /**
     * @brief Serializes a type.
     *
     * @param id The parameter ID.
     * @param value The value to serialize.
     *
     * @return ReturnCode
     */
    template<typename T>
    safedds::ReturnCode serialize_type(
            const uint32_t& id,
            const T& value)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        if (EncodingAlgorithm::PL_CDR == encoding_algorithm_ || EncodingAlgorithm::PL_CDR2 == encoding_algorithm_)
        {
            ret = serialize_parameter(id, &value);
        }
        else if (EncodingAlgorithm::PLAIN_CDR == encoding_algorithm_ ||
                EncodingAlgorithm::PLAIN_CDR2 == encoding_algorithm_ ||
                EncodingAlgorithm::DELIMITED_CDR == encoding_algorithm_)
        {
            ret = serialize_plain_type(id, value);
        }
        else
        {
            ret = safedds::ReturnCode::ERROR;
        }

        return ret;
    }

    /**
     * @brief Specialization for Optional type.
     */
    template<typename V>
    safedds::ReturnCode serialize_type(
            const uint32_t& id,
            const memory::Optional<V>& value)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        if ((EncodingAlgorithm::PL_CDR == encoding_algorithm_ || EncodingAlgorithm::PL_CDR2 == encoding_algorithm_) &&
                value.has_value())
        {
            ret = serialize_parameter(id, &value.value());
        }
        else if (EncodingAlgorithm::PLAIN_CDR == encoding_algorithm_)
        {
            ret = serialize_parameter(id, value.has_value() ? &value.value() : nullptr);
        }
        else if (EncodingAlgorithm::PLAIN_CDR2 == encoding_algorithm_ ||
                EncodingAlgorithm::DELIMITED_CDR == encoding_algorithm_)
        {
            ret = serialize(value.has_value());

            if (safedds::ReturnCode::OK == ret && value.has_value())
            {
                ret = serialize_type(id, value.value());
            }
        }
        else if (value.has_value())
        {
            ret = safedds::ReturnCode::ERROR;
        }

        return ret;
    }

    /**
     * @brief Serializes a type.
     *
     * @param id The parameter ID.
     * @param value The value to serialize.
     *
     * @return ReturnCode
     */
    template<typename T>
    safedds::ReturnCode serialize_type_key(
            const uint32_t& id,
            const T& value)
    {
        safedds::ReturnCode ret =
                (EncodingAlgorithm::PLAIN_CDR2 ==
                encoding_algorithm_) ? safedds::ReturnCode::OK : safedds::ReturnCode::ERROR;

        if (safedds::ReturnCode::OK == ret)
        {
            serializing_key_ = true;
            ret = serialize_plain_type(id, value);
            serializing_key_ = false;
        }

        return ret;
    }

private:

    /**
     * @brief SerializerState struct.
     */
    struct SerializerState
    {
        //! Attributes from base class
        uint32_t position;                                          //!< Position in the view.
        uint32_t alignment_position;                                //!< Alignment position in the view.
        bool native_endianness;                                     //!< Working on native endianness
        safedds::serialization::cdr::XCDRVersion xcdr_version;      //!< XCDR version to use.
        bool emulated_serialization;                                //!< Emulated serialization flag.

        //! Attributes from derived class
        EncodingAlgorithm encoding_algorithm_;                      //!< The encoding algorithm to use.
        safedds::platform::Endianness endianness_;                  //!< The endianness to use.
        memory::byte_array::ByteArrayView object_dheader_view_;     //!< The view for the dheader.
        uint32_t object_dheader_begin_position_;                    //!< Initial position for the dheader.
        bool serializing_key_;                                      //!< Flag indicating if the serializer is serializing a key.
    };

    /**
     * @brief Get the current state of the serializer.
     *
     * @return SerializerState
     */
    SerializerState get_state() const
    {
        SerializerState state;

        state.position = position_;
        state.alignment_position = alignment_position_;
        state.native_endianness = native_endianness_;
        state.xcdr_version = xcdr_version_;
        state.emulated_serialization = emulated_serialization_;
        state.encoding_algorithm_ = encoding_algorithm_;
        state.endianness_ = endianness_;
        state.object_dheader_view_ = object_dheader_view_;
        state.object_dheader_begin_position_ = object_dheader_begin_position_;
        state.serializing_key_ = serializing_key_;

        return state;
    }

    /**
     * @brief Set the state of the serializer.
     *
     * @param state The state to set.
     */
    void set_state(
            const SerializerState& state)
    {
        position_ = state.position;
        alignment_position_ = state.alignment_position;
        native_endianness_ = state.native_endianness;
        xcdr_version_ = state.xcdr_version;
        emulated_serialization_ = state.emulated_serialization;
        encoding_algorithm_ = state.encoding_algorithm_;
        endianness_ = state.endianness_;
        object_dheader_view_ = state.object_dheader_view_;
        object_dheader_begin_position_ = state.object_dheader_begin_position_;
        serializing_key_ = state.serializing_key_;
    }

    /**
     * @brief Serializes a plain type, generic implementation.
     *
     * @param id The parameter ID.
     * @param value The value to serialize.
     *
     * @return ReturnCode
     */
    template<typename T>
    typename std::enable_if<!std::is_enum<T>::value && !is_safe_dds_serialization_basic_type<T>::value,
            safedds::ReturnCode>::type serialize_plain_type(
            const uint32_t& /* id */,
            const T& value)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        // Store the state of the serializer to use it as a copy
        SerializerState state = get_state();

        if (serializing_key_)
        {
            ret = T::SerializationStruct::serialize_key(*this, value);
        }
        else
        {
            // Serialize the value
            ret = T::SerializationStruct::serialize(*this, value);
        }

        if (safedds::ReturnCode::OK == ret)
        {
            state.position = position_;

            // IMPORTANT: This reset at parameter payload is related with an open discussion about PUSH/POP(Origin=0) in OMG Standard
            state.alignment_position = alignment_position_;
        }

        // Restore the state of the serializer
        set_state(state);

        return ret;
    }

    /**
     * @brief Serializes a plain type, specialization for optional types.
     */
    template<typename V>
    safedds::ReturnCode serialize_plain_type(
            const uint32_t& id,
            const memory::Optional<V>& value)
    {
        return serialize_parameter(id, value.has_value() ? &value.value() : nullptr);
    }

    /**
     * @brief Serializes a plain type, specialization for map types.
     */
    template<typename K, typename V>
    safedds::ReturnCode serialize_plain_type(
            const uint32_t& id,
            const std::map<K, V>& value)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        memory::byte_array::ByteArrayView local_map_dheader_view = {};

        if (safedds::serialization::cdr::XCDRVersion::XCDRV2 == xcdr_version_ && map_require_dheader<K, V>::value)
        {
            ret = serializer_align_to(4U);

            if (safedds::ReturnCode::OK == ret && !emulated_serialization_)
            {
                local_map_dheader_view.mutable_set(&buffer_[position_], sizeof(uint32_t));
            }

            if (safedds::ReturnCode::OK == ret)
            {
                ret = serializer_skip(sizeof(uint32_t));
            }
        }

        const uint32_t begin_position = position_;

        if (safedds::ReturnCode::OK == ret)
        {
            ret = serialize(static_cast<uint32_t>(value.size()));
        }

        if (safedds::ReturnCode::OK == ret)
        {
            for (const auto& element : value)
            {
                ret = serialize_type(id, element.first);

                if (ret != safedds::ReturnCode::OK)
                {
                    break;
                }

                ret = serialize_type(id, element.second);

                if (ret != safedds::ReturnCode::OK)
                {
                    break;
                }
            }
        }

        uint32_t actual_size = position_ - begin_position;

        if (safedds::ReturnCode::OK == ret && !emulated_serialization_ &&
                safedds::serialization::cdr::XCDRVersion::XCDRV2 == xcdr_version_ &&
                map_require_dheader<K, V>::value)
        {
            SafeDDSTypeSupportSerializer dheader_length_serializer(local_map_dheader_view, endianness_,
                    xcdr_version_);
            ret = dheader_length_serializer.serialize(actual_size);
        }

        return ret;
    }

    /**
     * @brief Util for serializing a raw container. Generic container implementation.
     */
    template<typename V>
    typename std::enable_if<
        safe_dds_serialization_container_utils<V>::serialize_container_by_element,
        safedds::ReturnCode>::type serialize_raw_container(
            const uint32_t& id,
            const V& container)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        for (uint32_t i = 0; i < container.size(); ++i)
        {
            ret = serialize_type(id, container.data()[i]);

            if (ret != safedds::ReturnCode::OK)
            {
                break;
            }
        }

        return ret;
    }

    /**
     * @brief Util for serializing a raw container. Specialization for vectors of bool types.
     */
    template<typename V>
    typename std::enable_if<
        safe_dds_serialization_container_utils<V>::serialize_bool_vector_by_element,
        safedds::ReturnCode>::type serialize_raw_container(
            const uint32_t& id,
            const V& container)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        for (const auto& element : container)
        {
            ret = serialize_type(id, element);

            if (ret != safedds::ReturnCode::OK)
            {
                break;
            }
        }

        return ret;
    }

    /**
     * @brief Util for serializing a raw container. Specialization for basic type container.
     */
    template<typename V>
    typename std::enable_if<
        safe_dds_serialization_container_utils<V>::serialize_basic_type,
        safedds::ReturnCode>::type serialize_raw_container(
            const uint32_t& /* id */,
            const V& container)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        if (!container.empty())
        {
            ret = serialize_array(container.data(), static_cast<uint32_t>(container.size()));
        }

        return ret;
    }

    /**
     * @brief Serializes a plain type, specialization for array types.
     */
    template<typename V, size_t N>
    safedds::ReturnCode serialize_plain_type(
            const uint32_t& id,
            const std::array<V, N>& value)
    {
        return serialize_plain_array_type(id, value);
    }

    /**
     * @brief Serializes a plain array type.
     *
     * @note Array can be defined by a pointer to the first element and a size.
     */
    template<typename V>
    typename std::enable_if<
        is_array<V>::value || is_external_array<V>::value,
        safedds::ReturnCode>::type serialize_plain_array_type(
            const uint32_t& id,
            const V value)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        memory::byte_array::ByteArrayView local_array_dheader_view = {};

        if (safedds::serialization::cdr::XCDRVersion::XCDRV2 == xcdr_version_ &&
                sequence_or_array_require_dheader<V>::value)
        {
            ret = serializer_align_to(4U);

            if (safedds::ReturnCode::OK == ret && !emulated_serialization_)
            {
                local_array_dheader_view.mutable_set(&buffer_[position_], sizeof(uint32_t));
            }

            if (safedds::ReturnCode::OK == ret)
            {
                ret = serializer_skip(sizeof(uint32_t));
            }
        }

        const uint32_t begin_position = position_;

        // Use templatized raw container serialization
        ret = serialize_raw_container(id, value);

        uint32_t actual_size = position_ - begin_position;

        if (safedds::ReturnCode::OK == ret && !emulated_serialization_ &&
                safedds::serialization::cdr::XCDRVersion::XCDRV2 == xcdr_version_ &&
                sequence_or_array_require_dheader<V>::value)
        {
            SafeDDSTypeSupportSerializer dheader_length_serializer(local_array_dheader_view, endianness_,
                    xcdr_version_);
            ret = dheader_length_serializer.serialize(actual_size);
        }

        return ret;
    }

    /**
     * @brief Serializes a plain type, specialization for vector types.
     */
    template<typename V>
    safedds::ReturnCode serialize_plain_type(
            const uint32_t& id,
            const std::vector<V>& value)
    {
        return serialize_plain_sequence_type(id, value);
    }

    /**
     * @brief Serializes a plain sequence type
     *
     * @note Sequence can be defined by a pointer to the first element and a size.
     */
    template<typename V>
    typename std::enable_if<
        is_sequence<V>::value || is_external_sequence<V>::value,
        safedds::ReturnCode>::type serialize_plain_sequence_type(
            const uint32_t& id,
            const V value)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        memory::byte_array::ByteArrayView local_sequence_dheader_view = {};

        if (safedds::serialization::cdr::XCDRVersion::XCDRV2 == xcdr_version_ &&
                sequence_or_array_require_dheader<V>::value)
        {
            ret = serializer_align_to(4U);

            if (safedds::ReturnCode::OK == ret && !emulated_serialization_)
            {
                local_sequence_dheader_view.mutable_set(&buffer_[position_], sizeof(uint32_t));
            }

            if (safedds::ReturnCode::OK == ret)
            {
                ret = serializer_skip(sizeof(uint32_t));
            }
        }

        const uint32_t begin_position = position_;

        if (safedds::ReturnCode::OK == ret)
        {
            ret = serialize(static_cast<uint32_t>(value.size()));
        }

        if (safedds::ReturnCode::OK == ret)
        {
            // Use templatized raw container serialization
            ret = serialize_raw_container(id, value);
        }

        uint32_t actual_size = position_ - begin_position;

        if (safedds::ReturnCode::OK == ret && !emulated_serialization_ &&
                safedds::serialization::cdr::XCDRVersion::XCDRV2 == xcdr_version_ &&
                sequence_or_array_require_dheader<V>::value)
        {
            SafeDDSTypeSupportSerializer dheader_length_serializer(local_sequence_dheader_view, endianness_,
                    xcdr_version_);
            ret = dheader_length_serializer.serialize(actual_size);
        }

        return ret;
    }

    /**
     * @brief Serializes a plain type, specialization for enum types.
     */
    template<class E>
    typename std::enable_if<std::is_enum<E>::value, safedds::ReturnCode>::type serialize_plain_type(
            const uint32_t& /* id */,
            const E& value)
    {
        return serialize(static_cast<typename std::underlying_type<E>::type>(value));
    }

    /**
     * @brief Serializes a Safe DDS serialization basic types
     */
    template<typename T>
    typename std::enable_if<is_safe_dds_serialization_basic_type<T>::value,
            safedds::ReturnCode>::type serialize_plain_type(
            const uint32_t& /* id */,
            const T& value)
    {
        return serialize(value);
    }

    /**
     * @brief Serializes a plain type, specialization for strings
     */
    safedds::ReturnCode serialize_plain_type(
            const uint32_t& id,
            const std::string& value)
    {
        return serialize_plain_string_type(id, value);
    }

    /**
     * @brief Serializes a plain string type.
     */
    template<typename T>
    typename std::enable_if<is_string<T>::value || is_external_string<T>::value,
            safedds::ReturnCode>::type serialize_plain_string_type(
            const uint32_t& /* id */,
            const T& value)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        ret = serialize(static_cast<uint32_t>(value.size() + 1));

        if (ret == safedds::ReturnCode::OK && value.size() > 0)
        {
            ret = serialize_array(const_cast<char*>(value.data()), static_cast<uint32_t>(value.size()));
        }

        if (ret == safedds::ReturnCode::OK)
        {
            ret = serialize(static_cast<uint8_t>(0U));
        }

        return ret;
    }

    /**
     * @brief Serializes an external type.
     */
    template<typename T>
    safedds::ReturnCode serialize_plain_type(
            const uint32_t& id,
            const External<T>& value)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::ERROR;

        if (nullptr != value.member)
        {
            ret = serialize_plain_type(id, *(value.member));
        }

        return ret;
    }

    /**
     * @brief Serializes an external sequence.
     */
    template<typename T>
    safedds::ReturnCode serialize_plain_type(
            const uint32_t& id,
            const ExternalSequence<T>& value)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::ERROR;

        if (nullptr != value.data())
        {
            ret = serialize_plain_sequence_type(id, value);
        }

        return ret;
    }

    /**
     * @brief Serializes an external array.
     */
    template<typename T, uint32_t N>
    safedds::ReturnCode serialize_plain_type(
            const uint32_t& id,
            const ExternalArray<T, N>& value)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::ERROR;

        if (nullptr != value.data())
        {
            ret = serialize_plain_array_type(id, value);
        }

        return ret;
    }

    /**
     * @brief Serializes an external string.
     */
    safedds::ReturnCode serialize_plain_type(
            const uint32_t& id,
            const ExternalString& value)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::ERROR;

        if (nullptr != value.data())
        {
            ret = serialize_plain_string_type(id, value);
        }

        return ret;
    }

    /**
     * @brief Serializes a parameter header.
     *
     * @note Will not serialize the length field.
     *
     * @param id The parameter ID.
     *
     * @return ReturnCode
     */
    safedds::ReturnCode serialize_parameter_header(
            const uint32_t& id)
    {
        // Align to 4 bytes
        safedds::ReturnCode ret = serializer_align_to(4U);

        // Serialize param_id
        if (safedds::ReturnCode::OK == ret)
        {
            ret = serialize(static_cast<uint16_t>(id));
        }

        return ret;
    }

    /**
     * @brief Serializes an extended parameter header.
     *
     * @note Will not serialize the extended length field.
     *
     * @param id The parameter ID.
     *
     * @return ReturnCode
     */
    safedds::ReturnCode serialize_parameter_header_extended(
            const uint32_t& id)
    {
        // Align to 4 bytes
        safedds::ReturnCode ret = serializer_align_to(4U);

        // Serialize extension header
        if (safedds::ReturnCode::OK == ret)
        {
            ret = serialize_parameter_header(PID_EXTENDED);
        }

        // Serialize extension header length
        if (safedds::ReturnCode::OK == ret)
        {
            ret = serialize(static_cast<uint16_t>(8U));
        }

        // Serialize 32 bits id
        if (safedds::ReturnCode::OK == ret)
        {
            ret = serialize(id);
        }

        return ret;
    }

    /**
     * @brief Serializes a parameter assuming XCDR1 encoding.
     *
     * @param id The parameter ID.
     * @param value The value to serialize.
     *
     * @return ReturnCode
     */
    template<typename T>
    safedds::ReturnCode serialize_parameter_xcdr1(
            const uint32_t& id,
            const T* value)
    {
        const bool extended_parameter = (id > PID_MAX_SHORT) && (id != PID_LIST_END);

        // Check preconditions
        safedds::ReturnCode ret = safedds::serialization::cdr::XCDRVersion::XCDRV1 == xcdr_version_ ?
                safedds::ReturnCode::OK :
                safedds::ReturnCode::ERROR;

        // Parameter header is aligned to 4 bytes
        if (safedds::ReturnCode::OK == ret)
        {
            ret = serializer_align_to(4U);
        }

        // Serialize param_id
        if (safedds::ReturnCode::OK == ret)
        {
            if (extended_parameter)
            {
                ret = serialize_parameter_header_extended(id);
            }
            else
            {
                ret = serialize_parameter_header(id);
            }
        }

        // Prepare actual parameter length serialization taking into account extensible parameters
        const uint32_t length_field_size = extended_parameter ?
                safedds::portable::safe_sizeof<uint32_t>() :
                safedds::portable::safe_sizeof<uint16_t>();

        uint8_t* length_position = emulated_serialization_ ? nullptr : &buffer_[position_];

        // Skip the length field size
        if (safedds::ReturnCode::OK == ret)
        {
            ret = serializer_skip(length_field_size);
        }

        // Serialize the payload if there is enough space
        uint32_t payload_size = 0;

        // IMPORTANT: This reset at parameter payload is related with an open discussion about PUSH/POP(Origin=0) in OMG Standard
        reset_alignment();

        // Create a clean serializer for the parameter payload taking into account the emulation mode
        if (safedds::ReturnCode::OK == ret && (nullptr != value))
        {
            if (emulated_serialization_)
            {
                SafeDDSTypeSupportSerializer payload_serializer(endianness_, xcdr_version_);
                ret = payload_serializer.serialize_plain_type(id, *value);
                payload_size = payload_serializer.serializer_used_size();
            }
            else
            {
                memory::byte_array::ByteArrayView payload_view(&buffer_[position_], serializer_remaining_size());
                SafeDDSTypeSupportSerializer payload_serializer(payload_view, endianness_, xcdr_version_);
                ret = payload_serializer.serialize_plain_type(id, *value);
                payload_size = payload_serializer.serializer_used_size();
            }
        }

        // Skip the used size in the current serializer
        if (safedds::ReturnCode::OK == ret)
        {
            ret = serializer_skip(payload_size);
        }

        // Serialize length
        if (safedds::ReturnCode::OK == ret && !emulated_serialization_)
        {
            // Create a serializer for the length field stored previously
            memory::byte_array::ByteArrayView length_view{length_position, length_field_size};
            safedds::serialization::cdr::Serializer length_serializer(length_view, endianness_, xcdr_version_);

            if (extended_parameter)
            {
                ret = length_serializer.serialize(static_cast<uint32_t>(payload_size));
            }
            else
            {
                ret = length_serializer.serialize(static_cast<uint16_t>(payload_size));
            }
        }

        return ret;
    }

    /**
     * @brief Serializes a parameter assuming XCDR2 encoding.
     *
     * @param id The parameter ID.
     * @param value The value to serialize.
     *
     * @return ReturnCode
     */
    template<typename T>
    safedds::ReturnCode serialize_parameter_xcdr2(
            const uint32_t& id,
            const T* value)
    {
        // Check preconditions
        safedds::ReturnCode ret = safedds::serialization::cdr::XCDRVersion::XCDRV2 == xcdr_version_ ?
                safedds::ReturnCode::OK :
                safedds::ReturnCode::ERROR;

        // Parameter header is aligned to 4 bytes
        if (safedds::ReturnCode::OK == ret)
        {
            ret = serializer_align_to(4U);
        }

        // Check max bound of id
        if (id > PID_MAX_CDRV2)
        {
            ret = safedds::ReturnCode::ERROR;
        }

        // Serialize emheader
        const uint8_t lc = get_xcdr2_emheader_length_code<T>(*value);
        // Explicit nextint field is only needed if the LC == 4.
        // LC > 4 is for sequences and strings which already have their associated length at the beginning of the payload, which is the same as the nextint field.
        const bool needs_explicit_nextint = (lc == 4);

        if (safedds::ReturnCode::OK == ret)
        {
            const bool must_understand = false;
            const uint32_t member_id = id & 0x0FFFFFFF;
            uint32_t emheader = (must_understand ? 0x80000000 : 0) | (lc << 28) | member_id;

            ret = serialize(emheader);
        }

        // Store nextint position
        uint8_t* nextint_position =
                (!needs_explicit_nextint || emulated_serialization_) ? nullptr : &buffer_[position_];

        // Skip the nextint field size if needed
        if ((safedds::ReturnCode::OK == ret) && needs_explicit_nextint)
        {
            ret = serializer_skip(sizeof(uint32_t));
        }

        // Serialize the payload if there is enough space
        uint32_t payload_size = 0;

        // Create a clean serializer for the parameter payload taking into account the emulation mode
        if (safedds::ReturnCode::OK == ret && (nullptr != value))
        {
            if (emulated_serialization_)
            {
                SafeDDSTypeSupportSerializer payload_serializer(endianness_, xcdr_version_);
                ret = payload_serializer.serialize_plain_type(id, *value);
                payload_size = payload_serializer.serializer_used_size();
            }
            else
            {
                memory::byte_array::ByteArrayView payload_view(&buffer_[position_], serializer_remaining_size());
                SafeDDSTypeSupportSerializer payload_serializer(payload_view, endianness_, xcdr_version_);
                ret = payload_serializer.serialize_plain_type(id, *value);
                payload_size = payload_serializer.serializer_used_size();
            }
        }

        // Skip the used size in the current serializer
        if (safedds::ReturnCode::OK == ret)
        {
            ret = serializer_skip(payload_size);
        }

        // Serialize nextint
        if (safedds::ReturnCode::OK == ret && needs_explicit_nextint && !emulated_serialization_)
        {
            // Create a serializer for the nextint field stored previously
            memory::byte_array::ByteArrayView nextint_view{nextint_position, sizeof(uint32_t)};
            safedds::serialization::cdr::Serializer nextint_serializer(nextint_view, endianness_, xcdr_version_);

            ret = nextint_serializer.serialize(payload_size);
        }

        return ret;
    }

    /**
     * @brief Serializes a parameter.
     *
     * @param id The parameter ID.
     * @param value The value to serialize.
     *
     * @return ReturnCode
     */
    template<typename T>
    safedds::ReturnCode serialize_parameter(
            const uint32_t& id,
            const T* value)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        if (safedds::serialization::cdr::XCDRVersion::XCDRV1 == xcdr_version_)
        {
            ret = serialize_parameter_xcdr1(id, value);
        }
        else if (safedds::serialization::cdr::XCDRVersion::XCDRV2 == xcdr_version_)
        {
            ret = serialize_parameter_xcdr2(id, value);
        }
        else
        {
            ret = safedds::ReturnCode::ERROR;
        }

        return ret;
    }

private:

    /**
     * @brief Get XCDR2 EMHEADER length code (LC) generic implementation.
     *
     * @note If the size of the type is not supported, it will default to 4.
     *
     * @param value The value to get the length code from.
     *
     * @return length code value
     */
    template<typename T>
    const typename std::enable_if<!is_sequence<T>::value && !is_external_sequence<T>::value && !is_array<T>::value && !is_external_array<T>::value && !is_map<T>::value && !is_string<T>::value && !is_external_string<T>::value && !is_idl_union<T>::value && !is_external<T>::value && is_idl_extensibility_final<T>::value,
            uint8_t>::type get_xcdr2_emheader_length_code(
            const T& /* value */)
    {
        const uint32_t element_size = portable::safe_sizeof<T>();

        return (element_size == 1 ? 0 :
               element_size == 2 ? 1 :
               element_size == 4 ? 2 :
               element_size == 8 ? 3 :
               4) & 0x07;
    }

    /**
     * @brief Get XCDR2 EMHEADER length code (LC) specialization for external types.
     *
     * @note If the size of the type is not supported, it will default to 4.
     *
     * @param value The value to get the length code from.
     *
     * @return length code value
     */
    template<typename T>
    const typename std::enable_if<is_external<T>::value,
            uint8_t>::type get_xcdr2_emheader_length_code(
            const T& value)
    {
        return get_xcdr2_emheader_length_code(*value.member);
    }

    /**
     * @brief Get XCDR2 EMHEADER length code (LC) specialization for sequences (are always final).
     *
     * @param value The sequence to get the length code from.
     *
     * @return length code value
     */
    template<typename T>
    const typename std::enable_if<is_sequence<T>::value || is_external_sequence<T>::value,
            uint8_t>::type get_xcdr2_emheader_length_code(
            const T& value)
    {
        uint8_t length_code = 0;

        if (!is_arithmetic_or_enum<typename T::value_type>::value)
        {
            // Sequences's dheader is always 4 bytes
            length_code = 5 & 0x07;
        }
        // When having arithmetic and enum inner types the dheader can be optimized
        else
        {
            const uint32_t element_size = portable::safe_sizeof<typename T::value_type>();
            const uint32_t sequence_size = value.size();

            length_code = (element_size == 1 ? 5 :
                    element_size == 4 ? 6 :
                    element_size == 8 ? 7 :
                    sequence_size == 0 ? 2 :
                    4) & 0x07;
        }

        return length_code;
    }

    /**
     * @brief Get XCDR2 EMHEADER length code (LC) specialization for arrays (are always final).
     *
     * @param value The array to get the length code from.
     *
     * @return length code value
     */
    template<typename T>
    const typename std::enable_if<is_array<T>::value || is_external_array<T>::value,
            uint8_t>::type get_xcdr2_emheader_length_code(
            const T& value)
    {
        uint8_t length_code = 0;

        if (sequence_or_array_require_dheader<T>::value)
        {
            // Array's dheader is always 4 bytes and it specifies the size of the array
            length_code = 5 & 0x07;
        }
        else
        {
            const uint32_t element_size = portable::safe_sizeof<typename T::value_type>();
            const uint32_t array_size = value.size();
            const uint32_t total_size = array_size * element_size;
            length_code = (total_size == 1 ? 0 :
                    total_size == 2 ? 1 :
                    total_size == 4 ? 2 :
                    total_size == 8 ? 3 :
                    4) & 0x07;
        }

        return length_code;
    }

    /**
     * @brief Get XCDR2 EMHEADER length code (LC) specialization for maps (are always final).
     *
     * @param value The map to get the length code from.
     *
     * @return length code value
     */
    template<typename T>
    const typename std::enable_if<is_map<T>::value, uint8_t>::type get_xcdr2_emheader_length_code(
            const T& value)
    {
        uint8_t length_code = 0;

        if (map_require_dheader<typename T::key_type, typename T::mapped_type>::value)
        {
            // Map's dheader is always 4 bytes and it specifies the size of the map
            length_code = 5 & 0x07;
        }
        else
        {
            const uint32_t element_size = portable::safe_sizeof<typename T::key_type>() +
                    portable::safe_sizeof<typename T::mapped_type>();
            const uint32_t map_size = value.size();
            const uint32_t total_size = map_size * element_size + portable::safe_sizeof<uint32_t>();

            length_code = (map_size == 0 ? 2 :
                    total_size == 2 ? 1 :
                    total_size == 4 ? 2 :
                    total_size == 8 ? 3 :
                    4) & 0x07;
        }

        return length_code;
    }

    /**
     * @brief Get XCDR2 EMHEADER length code (LC) specialization for strings (are always final).
     *
     * @param value The string to get the length code from.
     *
     * @return length code value
     */
    template<typename T>
    const typename std::enable_if<is_string<T>::value || is_external_string<T>::value,
            uint8_t>::type get_xcdr2_emheader_length_code(
            const T& /* value */)
    {
        return 5 & 0x07;
    }

    /**
     * @brief Get XCDR2 EMHEADER length code (LC) specialization for non-final types.
     *
     * @note Always returns 5.
     *
     * @return length code value
     */
    template<typename T>
    const typename std::enable_if<!is_idl_extensibility_final<T>::value, uint8_t>::type get_xcdr2_emheader_length_code(
            const T& /* value */)
    {
        return 5 & 0x07;
    }

    /**
     * @brief Get XCDR2 EMHEADER length code (LC) specialization for union types.
     *
     * @note If the size of the type is not supported, it will default to 4.
     *
     * @param value The value to get the length code from.
     *
     * @return length code value
     */
    template<typename T>
    const typename std::enable_if<is_idl_extensibility_final<T>::value && is_idl_union<T>::value,
            uint8_t>::type get_xcdr2_emheader_length_code(
            const T& value)
    {
        const uint32_t element_size = value.get_current_union_size();

        return (element_size == 1 ? 0 :
               element_size == 2 ? 1 :
               element_size == 4 ? 2 :
               element_size == 8 ? 3 :
               4) & 0x07;
    }

    safedds::platform::Endianness endianness_;                  //!< The endianness to use.
    EncodingAlgorithm encoding_algorithm_;                      //!< The encoding algorithm to use.
    memory::byte_array::ByteArrayView object_dheader_view_;     //!< The view for the dheader.
    uint32_t object_dheader_begin_position_;                    //!< Initial position for the dheader.
    bool serializing_key_ = false;                              //!< Flag indicating if the serializer is serializing a key.
};

/**
 * @brief SafeDDSTypeSupportDeserializer class.
 */
class SafeDDSTypeSupportDeserializer :
    public safedds::serialization::cdr::Deserializer
{

public:

    /**
     * @brief Constructs a SafeDDSTypeSupportDeserializer object.
     *
     * @param buffer The memory buffer containing the data to be deserialized.
     * @param endianness The endianness to use for deserialization.
     * @param xcdr_version The XCDR version to use for deserialization.
     */
    SafeDDSTypeSupportDeserializer(
            const memory::IConstByteArrayView& buffer,
            safedds::platform::Endianness endianness,
            safedds::serialization::cdr::XCDRVersion xcdr_version) noexcept
        : safedds::serialization::cdr::Deserializer(buffer, endianness, xcdr_version)
        , current_encoding_algorithm_(
            safedds::serialization::cdr::XCDRVersion::XCDRV1 == xcdr_version ?
            EncodingAlgorithm::PLAIN_CDR :
            EncodingAlgorithm::PLAIN_CDR2)
    {
    }

    /**
     * @brief Get XCDR version
     *
     * @return XCDRVersion
     */
    safedds::serialization::cdr::XCDRVersion get_xcdr_version() const
    {
        return xcdr_version_;
    }

    /**
     * @brief Deserializes a type.
     *
     * @param encoding_algorithm The encoding algorithm to use for deserialization.
     * @param callback The callback to be called for each member.
     *
     * @return ReturnCode
     */
    template<typename Callback>
    safedds::ReturnCode deserialize_type(
            const safedds::gen::EncodingAlgorithm& encoding_algorithm,
            Callback callback)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        const EncodingAlgorithm previous_encoding_algorithm = current_encoding_algorithm_;
        current_encoding_algorithm_ = encoding_algorithm;

        if (EncodingAlgorithm::PLAIN_CDR == encoding_algorithm)
        {
            ret = deserialize_type_plaincdr(callback);
        }
        else if (EncodingAlgorithm::PL_CDR == encoding_algorithm)
        {
            ret = deserialize_type_parameterlistcdr(callback);
        }
        else if (EncodingAlgorithm::PLAIN_CDR2 == encoding_algorithm)
        {
            ret = deserialize_type_plaincdr(callback);
        }
        else if (EncodingAlgorithm::DELIMITED_CDR == encoding_algorithm)
        {
            ret = deserialize_type_delimitedcdr(callback);
        }
        else if (EncodingAlgorithm::PL_CDR2 == encoding_algorithm)
        {
            ret = deserialize_type_parameterlistcdr2(callback);
        }

        current_encoding_algorithm_ = previous_encoding_algorithm;

        return ret;
    }

    /**
     * @brief Deserializes a member. Generic implementation.
     *
     * @param member The member to deserialize.
     *
     * @return ReturnCode
     */
    template<typename T>
    typename std::enable_if<!std::is_enum<T>::value && !is_safe_dds_serialization_basic_type<T>::value,
            safedds::ReturnCode>::type deserialize_member(
            T& member)
    {
        return T::SerializationStruct::deserialize(*this, member);
    }

    /**
     * @brief Deserializes a member. Specialization for optional types.
     */
    template<typename V>
    safedds::ReturnCode deserialize_member(
            memory::Optional<V>& member)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        member.reset();

        // When dealing with an optional member, if we are in PLAIN_CDR mode, we need to check if the parameter is present
        // by means of the parameter header.
        if (EncodingAlgorithm::PLAIN_CDR == current_encoding_algorithm_)
        {
            uint32_t id = 0;
            uint32_t length = 0;

            ret = deserialize_parameter_header(id, length);

            const bool extended_header = get_pid_value(id, false) == PID_EXTENDED;

            if (safedds::ReturnCode::OK == ret && extended_header)
            {
                ret = deserialize_parameter_header_extended(id, length);
            }

            // IMPORTANT: This reset at parameter payload is related with an open discussion about PUSH/POP(Origin=0) in OMG Standard
            reset_alignment();

            if (0U != length && safedds::ReturnCode::OK == ret)
            {
                // Create a new deserializer for the payload in order to take into account parameter payload alignment
                memory::byte_array::ByteArrayView payload_view;
                ret = deserializer_skip(length, payload_view);
                SafeDDSTypeSupportDeserializer payload_deserializer(payload_view, endianness_, xcdr_version_);
                payload_deserializer.current_encoding_algorithm_ = current_encoding_algorithm_;

                V& value = member.inner_reference();

                if (safedds::ReturnCode::OK == ret)
                {
                    ret = payload_deserializer.deserialize_member(value);
                }

                if (safedds::ReturnCode::OK == ret)
                {
                    member.set(value);
                }
            }
        }
        // In the case of being in a PL_CDR mode or PL_CDR2, we can assume that the parameter header has been already processed and only the
        // payload needs to be deserialized.
        else if (EncodingAlgorithm::PL_CDR == current_encoding_algorithm_ ||
                EncodingAlgorithm::PL_CDR2 == current_encoding_algorithm_)
        {
            V& value = member.inner_reference();
            ret = deserialize_member(value);

            if (safedds::ReturnCode::OK == ret)
            {
                member.set(value);
            }
        }
        // In the case of being in a PLAIN_CDR2 or DELIMITED_CDR mode, we need to check if the parameter is present
        else if (EncodingAlgorithm::PLAIN_CDR2 == current_encoding_algorithm_ ||
                EncodingAlgorithm::DELIMITED_CDR == current_encoding_algorithm_)
        {
            bool is_set = false;
            ret = deserialize(is_set);

            if (safedds::ReturnCode::OK == ret && is_set)
            {
                V& value = member.inner_reference();
                ret = deserialize_member(value);

                if (safedds::ReturnCode::OK == ret)
                {
                    member.set(value);
                }
            }
        }
        else
        {
            ret = safedds::ReturnCode::ERROR;
        }

        return ret;
    }

    /**
     * @brief Util for deserializing a raw container. Generic container implementation.
     */
    template<typename V>
    typename std::enable_if<
        safe_dds_serialization_container_utils<V>::deserialize_container_by_element,
        safedds::ReturnCode>::type deserialize_raw_container(
            V& container)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        for (uint32_t i = 0; i < container.size(); ++i)
        {
            ret = deserialize_member(container.data()[i]);

            if (ret != safedds::ReturnCode::OK)
            {
                break;
            }
        }

        return ret;
    }

    /**
     * @brief Util for deserializing a raw container. Specialization for vectors of bool types.
     */
    template<typename V>
    typename std::enable_if<
        safe_dds_serialization_container_utils<V>::deserialize_bool_vector_by_element,
        safedds::ReturnCode>::type deserialize_raw_container(
            V& container)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        for (size_t i = 0; i < container.size(); ++i)
        {
            bool temp = false;
            ret = deserialize(temp);

            if (ret != safedds::ReturnCode::OK)
            {
                break;
            }

            container[i] = temp;
        }

        return ret;
    }

    /**
     * @brief Util for deserializing a raw container. Specialization for basic type containers.
     */
    template<typename V>
    typename std::enable_if<
        safe_dds_serialization_container_utils<V>::deserialize_basic_type,
        safedds::ReturnCode>::type deserialize_raw_container(
            V& container)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        if (!container.empty())
        {
            ret = deserialize_array(container.data(), static_cast<uint32_t>(container.size()));
        }

        return ret;
    }

    /**
     * @brief Deserializes a member. Specialization for array types.
     */
    template<typename V, size_t N>
    safedds::ReturnCode deserialize_member(
            std::array<V, N>& member)
    {
        return deserialize_array_member(member);
    }

    /**
     * @brief Deserializes an array member.
     *
     * @note Array can also be defined by a pointer to the first element and a size.
     */
    template<typename V>
    typename std::enable_if<
        is_array<V>::value || is_external_array<V>::value,
        safedds::ReturnCode>::type deserialize_array_member(
            V& member)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        // Deserialize dheader if required
        uint32_t dheader = 0;

        if (safedds::serialization::cdr::XCDRVersion::XCDRV2 == xcdr_version_ &&
                sequence_or_array_require_dheader<V>::value)
        {
            ret = deserialize(dheader);
        }

        const uint32_t begin_position = position_;

        if (safedds::ReturnCode::OK == ret)
        {
            // Use templatized raw container deserialization
            ret = deserialize_raw_container(member);
        }

        uint32_t actual_size = position_ - begin_position;

        // Check if the dheader is present and skip the remaining bytes
        if (safedds::ReturnCode::OK == ret && safedds::serialization::cdr::XCDRVersion::XCDRV2 == xcdr_version_ &&
                sequence_or_array_require_dheader<V>::value && dheader > actual_size)
        {
            ret = deserializer_skip(dheader - actual_size);
        }

        return ret;
    }

    /**
     * @brief Deserializes a member. Specialization for vector types.
     */
    template<typename V>
    safedds::ReturnCode deserialize_member(
            std::vector<V>& member)
    {
        member.clear();

        return deserialize_sequence_member(member);
    }

    /**
     * @brief Deserializes a sequence member.
     *
     * @note Sequence can be defined by a pointer to the first element and a size.
     */
    template<typename V>
    typename std::enable_if<
        is_sequence<V>::value || is_external_sequence<V>::value,
        safedds::ReturnCode>::type deserialize_sequence_member(
            V& member)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        // Deserialize dheader if required
        uint32_t dheader = 0;

        if (safedds::serialization::cdr::XCDRVersion::XCDRV2 == xcdr_version_ &&
                sequence_or_array_require_dheader<V>::value)
        {
            ret = deserialize(dheader);
        }

        const uint32_t begin_position = position_;
        uint32_t size = 0;

        if (safedds::ReturnCode::OK == ret)
        {
            ret = deserialize(size);
        }

        if (safedds::ReturnCode::OK == ret)
        {
            ret = resize_container(member, size);
        }

        if (safedds::ReturnCode::OK == ret)
        {
            // Use templatized raw container deserialization
            ret = deserialize_raw_container(member);
        }

        uint32_t actual_size = position_ - begin_position;

        // Check if the dheader is present and skip the remaining bytes
        if (safedds::ReturnCode::OK == ret && safedds::serialization::cdr::XCDRVersion::XCDRV2 == xcdr_version_ &&
                sequence_or_array_require_dheader<V>::value && dheader > actual_size)
        {
            ret = deserializer_skip(dheader - actual_size);
        }

        return ret;
    }

    /**
     * @brief Deserializes a member. Specialization for map types.
     */
    template<typename K, typename V>
    safedds::ReturnCode deserialize_member(
            std::map<K, V>& member)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::OK;

        member.clear();

        // Deserialize dheader if required
        uint32_t dheader = 0;

        if (safedds::serialization::cdr::XCDRVersion::XCDRV2 == xcdr_version_ && map_require_dheader<K, V>::value)
        {
            ret = deserialize(dheader);
        }

        uint32_t size = 0;

        if (safedds::ReturnCode::OK == ret)
        {
            ret = deserialize(size);
        }

        if (safedds::ReturnCode::OK == ret)
        {
            while (size-- > 0)
            {
                K key;
                V value;

                ret = deserialize_member(key);

                if (ret != safedds::ReturnCode::OK)
                {
                    break;
                }

                ret = deserialize_member(value);

                if (ret != safedds::ReturnCode::OK)
                {
                    break;
                }

                member[key] = value;
            }
        }

        return ret;
    }

    /**
     * @brief Deserializes a member. Specialization for enum types.
     */
    template<class E>
    typename std::enable_if<std::is_enum<E>::value, safedds::ReturnCode>::type deserialize_member(
            E& member)
    {
        typename std::underlying_type<E>::type value;
        safedds::ReturnCode ret = deserialize(value);

        if (safedds::ReturnCode::OK == ret)
        {
            member = static_cast<E>(value);
        }

        return ret;
    }

    /**
     * @brief Deserializes a member. Specialization for Safe DDS serialization basic types.
     */
    template<class T>
    typename std::enable_if<is_safe_dds_serialization_basic_type<T>::value,
            safedds::ReturnCode>::type deserialize_member(
            T& member)
    {
        return deserialize(member);
    }

    /**
     * @brief Deserializes a member. Specialization for strings.
     */
    safedds::ReturnCode deserialize_member(
            std::string& member)
    {
        member.clear();

        return deserialize_string_member(member);
    }

    /**
     * @brief Deserializes a string member.
     */
    template<class T>
    typename std::enable_if<is_string<T>::value || is_external_string<T>::value,
            safedds::ReturnCode>::type deserialize_string_member(
            T& member)
    {
        uint32_t length = 0;
        safedds::ReturnCode ret = deserialize(length);

        if ((safedds::ReturnCode::OK == ret) && (length > 1))
        {
            ret = resize_container(member, length - 1);

            if (safedds::ReturnCode::OK == ret)
            {
                ret = deserialize_array(const_cast<char*>(member.data()), length - 1);
            }
        }

        if (safedds::ReturnCode::OK == ret)
        {
            char null_char = 0;
            ret = deserialize(null_char);
        }

        return ret;
    }

    /**
     * @brief Deserializes a member. Specialization for external types.
     */
    template<typename T>
    safedds::ReturnCode deserialize_member(
            External<T>& member)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::ERROR;

        if (nullptr != member.member)
        {
            ret = deserialize_member(*(member.member));
        }

        return ret;
    }

    /**
     * @brief Deserializes a member. Specialization for external sequences.
     */
    template<typename T>
    safedds::ReturnCode deserialize_member(
            ExternalSequence<T>& member)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::ERROR;

        if (nullptr != member.data())
        {
            ret = deserialize_sequence_member(member);
        }

        return ret;
    }

    /**
     * @brief Deserializes a member. Specialization for external arrays.
     */
    template<typename T, uint32_t N>
    safedds::ReturnCode deserialize_member(
            ExternalArray<T, N>& member)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::ERROR;

        if (nullptr != member.data())
        {
            ret = deserialize_array_member(member);
        }

        return ret;
    }

    /**
     * @brief Deserializes a member. Specialization for external strings.
     */
    safedds::ReturnCode deserialize_member(
            ExternalString& member)
    {
        safedds::ReturnCode ret = safedds::ReturnCode::ERROR;

        if (nullptr != member.data())
        {
            ret = deserialize_string_member(member);
        }

        return ret;
    }

private:

    /**
     * @brief Deserializes plain CDRv1 and plain CDRv2 type
     *
     * @param callback The callback to be called for each element.
     *
     * @return ReturnCode
     */
    template<typename Callback>
    safedds::ReturnCode deserialize_type_plaincdr(
            Callback callback)
    {
        safedds::ReturnCode ret =
                (EncodingAlgorithm::PLAIN_CDR == current_encoding_algorithm_ ||
                EncodingAlgorithm::PLAIN_CDR2 == current_encoding_algorithm_) ?
                safedds::ReturnCode::OK : safedds::ReturnCode::ERROR;

        if (safedds::ReturnCode::OK == ret)
        {
            uint32_t index = 0;

            while (deserializer_remaining_size() > 0 && safedds::ReturnCode::OK == ret)
            {
                ret = callback(*this, index++);
            }

            // Unimplemented means end of the list
            if (safedds::ReturnCode::UNIMPLEMENTED == ret)
            {
                ret = safedds::ReturnCode::OK;
            }
        }

        return ret;
    }

    /**
     * @brief Deserializes a Parameter list CDRv1 type
     *
     * @param callback The callback to be called for each element.
     *
     * @return ReturnCode
     */
    template<typename Callback>
    safedds::ReturnCode deserialize_type_parameterlistcdr(
            Callback callback)
    {
        safedds::ReturnCode ret =
                (EncodingAlgorithm::PL_CDR == current_encoding_algorithm_) ?
                safedds::ReturnCode::OK : safedds::ReturnCode::ERROR;

        if (safedds::ReturnCode::OK == ret)
        {
            bool list_end = false;

            while (deserializer_remaining_size() > 0 && safedds::ReturnCode::OK == ret && !list_end)
            {
                uint32_t id = 0;
                uint32_t length = 0;

                ret = deserialize_parameter_header(id, length);

                const bool extended_header = get_pid_value(id, false) == PID_EXTENDED;

                if (safedds::ReturnCode::OK == ret && extended_header)
                {
                    ret = deserialize_parameter_header_extended(id, length);
                }

                // Reset alginment after header deserialization
                reset_alignment();

                // Get id flags
                const bool implementation_extension = is_pid_implementation_extension(id, extended_header);
                const bool must_understand = is_pid_must_understand(id, extended_header);

                id = get_pid_value(id, extended_header);
                const bool ignore = implementation_extension || (id == PID_IGNORE);
                list_end = id == PID_LIST_END;

                if (safedds::ReturnCode::OK == ret && !list_end && length > 0)
                {
                    // Create an aux deserializer for the payload
                    memory::byte_array::ByteArrayView payload_view;
                    ret = deserializer_skip(length, payload_view);
                    SafeDDSTypeSupportDeserializer payload_deserializer(payload_view, endianness_, xcdr_version_);
                    payload_deserializer.current_encoding_algorithm_ = current_encoding_algorithm_;

                    if (!ignore && safedds::ReturnCode::OK == ret)
                    {
                        ret = callback(payload_deserializer, id);
                    }

                    // Handle unknown parameters
                    if (safedds::ReturnCode::UNIMPLEMENTED == ret)
                    {
                        ret = !must_understand ? safedds::ReturnCode::OK : safedds::ReturnCode::ERROR;
                    }
                }
            }
        }

        return ret;
    }

    /**
     * @brief Deserializes a delimitated CDRv2 type
     *
     * @param callback The callback to be called for each element.
     *
     * @return ReturnCode
     */
    template<typename Callback>
    safedds::ReturnCode deserialize_type_delimitedcdr(
            Callback callback)
    {
        safedds::ReturnCode ret =
                (EncodingAlgorithm::DELIMITED_CDR == current_encoding_algorithm_) ?
                safedds::ReturnCode::OK : safedds::ReturnCode::ERROR;

        if (safedds::ReturnCode::OK == ret)
        {
            // Deserialize dheader
            uint32_t dheader = 0;

            ret = deserialize(dheader);

            // Check remaining size
            if (safedds::ReturnCode::OK == ret && deserializer_remaining_size() < dheader)
            {
                ret = safedds::ReturnCode::SERIALIZATION_INVALID_BUFFER_LENGTH;
            }

            const uint32_t begin_position = position_;

            if (safedds::ReturnCode::OK == ret)
            {
                uint32_t index = 0;

                while ((position_ - begin_position) < dheader && safedds::ReturnCode::OK == ret)
                {
                    ret = callback(*this, index++);
                }

                // Unimplemented means end of the list
                if (safedds::ReturnCode::UNIMPLEMENTED == ret)
                {
                    ret = safedds::ReturnCode::OK;
                }
            }

            uint32_t actual_size = position_ - begin_position;

            // Skip the remaining bytes if dheader is greater than the actual size
            if (safedds::ReturnCode::OK == ret && dheader > actual_size)
            {
                ret = deserializer_skip(dheader - actual_size);
            }
        }

        return ret;
    }

    /**
     * @brief Deserializes a Parameter list CDRv2 type
     *
     * @param callback The callback to be called for each element.
     *
     * @return ReturnCode
     */
    template<typename Callback>
    safedds::ReturnCode deserialize_type_parameterlistcdr2(
            Callback callback)
    {
        safedds::ReturnCode ret =
                (EncodingAlgorithm::PL_CDR2 == current_encoding_algorithm_) ?
                safedds::ReturnCode::OK : safedds::ReturnCode::ERROR;

        // Deserialize dheader
        uint32_t dheader = 0;

        if (safedds::ReturnCode::OK == ret)
        {
            ret = deserialize(dheader);
        }

        // Check remaining size
        if (safedds::ReturnCode::OK == ret && deserializer_remaining_size() < dheader)
        {
            ret = safedds::ReturnCode::SERIALIZATION_INVALID_BUFFER_LENGTH;
        }

        const uint32_t begin_position = position_;

        if (safedds::ReturnCode::OK == ret)
        {
            while ((position_ - begin_position) < dheader && safedds::ReturnCode::OK == ret)
            {
                uint32_t emheader = 0;
                ret =  deserialize(emheader);

                uint32_t id = emheader & 0x0FFFFFFF;
                const uint8_t lc = (emheader >> 28) & 0x07;
                const bool must_understand = (emheader & 0x80000000) != 0;

                uint32_t length = 0;

                if (lc < 4U)
                {
                    length = 1U << lc;
                }
                else if (lc == 4U)
                {
                    deserialize(length);
                }
                else
                {
                    // See the future and do not skip the nextint field
                    memory::byte_array::ByteArrayView length_view = {};
                    length_view.const_mutable_set(&buffer_[position_], sizeof(uint32_t));
                    SafeDDSTypeSupportDeserializer length_deserializer(length_view, endianness_, xcdr_version_);
                    length_deserializer.deserialize(length);

                    // Apply multiplier
                    length *= (lc == 6U) ? 4U : (lc == 7U) ? 8U : 1U;

                    // Add nextint field size
                    length += sizeof(uint32_t);
                }

                if (safedds::ReturnCode::OK == ret && length > 0)
                {
                    // Create an aux deserializer for the payload
                    memory::byte_array::ByteArrayView payload_view;
                    ret = deserializer_skip(length, payload_view);
                    SafeDDSTypeSupportDeserializer payload_deserializer(payload_view, endianness_, xcdr_version_);
                    payload_deserializer.current_encoding_algorithm_ = current_encoding_algorithm_;

                    if (safedds::ReturnCode::OK == ret)
                    {
                        ret = callback(payload_deserializer, id);
                    }

                    // Handle unknown parameters
                    if (safedds::ReturnCode::UNIMPLEMENTED == ret)
                    {
                        ret = !must_understand ? safedds::ReturnCode::OK : safedds::ReturnCode::ERROR;
                    }
                }
            }
        }

        uint32_t actual_size = position_ - begin_position;

        // Skip the remaining bytes if dheader is greater than the actual size
        if (safedds::ReturnCode::OK == ret && dheader > actual_size)
        {
            ret = deserializer_skip(dheader - actual_size);
        }

        return ret;
    }

    /**
     * @brief Aligns the deserializer to the given alignment.
     *
     * @param alignment The alignment to align the deserializer to.
     */
    safedds::ReturnCode deserializer_align_to(
            uint32_t type_size) noexcept
    {
        safedds::ReturnCode ret = safedds::ReturnCode::SERIALIZATION_INVALID_BUFFER_LENGTH;
        const uint32_t padding = xcdr_padding_needed(alignment_position_, type_size);

        if ((position_ + padding) <= buffer_.const_size())
        {
            position_ += padding;
            alignment_position_ += padding;
            ret = safedds::ReturnCode::OK;
        }

        return ret;
    }

    /**
     * @brief Deserializes a parameter header.
     *
     * @param id The parameter ID.
     * @param length The parameter length.
     *
     * @return ReturnCode
     */
    safedds::ReturnCode deserialize_parameter_header(
            uint32_t& id,
            uint32_t& length)
    {
        // Align to 4 bytes
        safedds::ReturnCode ret = deserializer_align_to(4U);

        // Deserialize param_id
        if (safedds::ReturnCode::OK == ret)
        {
            uint16_t param_id = 0;
            ret = deserialize(param_id);

            if (safedds::ReturnCode::OK == ret)
            {
                id = param_id;
            }
        }

        // Deserialize length
        if (safedds::ReturnCode::OK == ret)
        {
            uint16_t length_value = 0;
            ret = deserialize(length_value);

            if (safedds::ReturnCode::OK == ret)
            {
                length = length_value;
            }
        }

        return ret;
    }

    /**
     * @brief Deserializes an extended parameter header.
     *
     * @param id The parameter ID.
     * @param length The parameter length.
     *
     * @return ReturnCode
     */
    safedds::ReturnCode deserialize_parameter_header_extended(
            uint32_t& id,
            uint32_t& length)
    {
        // Align to 4 bytes
        safedds::ReturnCode ret = deserializer_align_to(4U);

        // Deserialize param_id
        if (safedds::ReturnCode::OK == ret)
        {
            ret = deserialize(id);
        }

        // Deserialize length
        if (safedds::ReturnCode::OK == ret)
        {
            ret = deserialize(length);
        }

        return ret;
    }

private:

    EncodingAlgorithm current_encoding_algorithm_;       //!< The encoding algorithm in use.
};

/**
 * @brief SafeDDSTypeSupportComparator class
 */
struct SafeDDSTypeSupportComparator
{
    /**
     * @brief Compares a member.
     *
     * @param first The first value to compare.
     * @param second The second value to compare.
     *
     * @return true if the values are equal, false otherwise.
     */
    template<typename T>
    static typename std::enable_if<!std::is_arithmetic<T>::value && !std::is_same<T,
            std::string>::value && !std::is_enum<T>::value, bool>::type
    equal(
            const T& first,
            const T& second)
    {
        return T::SerializationStruct::compare(first, second);
    }

    /**
     * @brief Compares two values. Specialization for arithmetic types, strings and enums.
     */
    template<typename T>
    static typename std::enable_if<std::is_arithmetic<T>::value || std::is_same<T,
            std::string>::value || std::is_enum<T>::value, bool>::type
    equal(
            const T& first,
            const T& second)
    {
        return first == second;
    }

    /**
     * @brief Compares two values. Specialization for optional types.
     */
    template<typename V>
    static bool equal(
            const memory::Optional<V>& first,
            const memory::Optional<V>& second)
    {
        bool ret = true;

        ret = ret && (first.has_value() == second.has_value());

        if (first.has_value())
        {
            ret = ret && equal(first.value(), second.value());
        }

        return ret;
    }

    /**
     * @brief Compares two values. Specialization for array types.
     */
    template<typename V, size_t N>
    static bool equal(
            const std::array<V, N>& first,
            const std::array<V, N>& second)
    {
        bool ret = true;

        for (size_t i = 0; i < N; ++i)
        {
            ret = ret && equal(first[i], second[i]);

            if (!ret)
            {
                break;
            }
        }

        return ret;
    }

    /**
     * @brief Compares two values. Specialization for vector types.
     */
    template<typename V>
    static bool equal(
            const std::vector<V>& first,
            const std::vector<V>& second)
    {
        bool ret = true;

        ret = ret && (first.size() == second.size());

        for (size_t i = 0; ret && i < first.size(); ++i)
        {
            ret = ret && equal(first[i], second[i]);

            if (!ret)
            {
                break;
            }
        }

        return ret;
    }

    /**
     * @brief Compares two values. Specialization for map types.
     */
    template<typename K, typename V>
    static bool equal(
            const std::map<K, V>& first,
            const std::map<K, V>& second)
    {
        bool ret = true;

        ret = ret && (first.size() == second.size());

        for (auto it = first.begin(); ret && it != first.end(); ++it)
        {
            auto it2 = second.find(it->first);

            if (it2 == second.end())
            {
                ret = false;
            }
            else
            {
                ret = ret && equal(it->second, it2->second);
            }
        }

        return ret;
    }

    /**
     * @brief Compares two values. Specialization for external types.
     */
    template<typename T>
    static bool equal(
            const External<T>& first,
            const External<T>& second)
    {
        bool ret = false;

        if (first.member == second.member)
        {
            ret = true;
        }

        return ret;
    }

    /**
     * @brief Compares two values. Specialization for external sequences.
     */
    template<typename T>
    static bool equal(
            const ExternalSequence<T>& first,
            const ExternalSequence<T>& second)
    {
        bool ret = false;

        if (first.member == second.member)
        {
            if (first.member != nullptr)
            {
                ret = first.len == second.len;
            }
            else
            {
                ret = true;
            }
        }

        return ret;
    }

    /**
     * @brief Compares two values. Specialization for external arrays.
     */
    template<typename T, uint32_t N>
    static bool equal(
            const ExternalArray<T, N>& first,
            const ExternalArray<T, N>& second)
    {
        bool ret = false;

        if (first.member == second.member)
        {
            ret = true;
        }

        return ret;
    }

    /**
     * @brief Compares two values. Specialization for external strings.
     */
    static bool equal(
            const ExternalString& first,
            const ExternalString& second)
    {
        bool ret = false;

        if (first.member == second.member)
        {
            if (first.member != nullptr)
            {
                ret = first.len == second.len;
            }
            else
            {
                ret = true;
            }
        }

        return ret;
    }

};

} // namespace gen
} // namespace safedds
} // namespace eprosima

#endif // SAFEDDS_SAFEDDSGEN_SAFEDDSGENAUX_HPP
