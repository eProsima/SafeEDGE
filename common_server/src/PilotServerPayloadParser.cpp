#include <safe_edge/server/common/PilotServerPayloadParser.hpp>

#include <cerrno>
#include <cstdlib>
#include <iostream>

// Hand-rolled JSON extractor — no external library, no exceptions.
// Compatible with -fno-exceptions / -fno-rtti.

namespace safe_edge {
namespace server {
namespace common {

namespace {

// Returns the index one past the closing '}' matching the '{' at obj_start,
// or std::string::npos on mismatch.
static size_t find_object_end(const std::string& s, size_t obj_start)
{
    int depth = 0;
    bool in_string = false;
    bool escape = false;

    for (size_t i = obj_start; i < s.size(); ++i)
    {
        const char c = s[i];
        if (escape)
        {
            escape = false;
            continue;
        }
        if (c == '\\' && in_string)
        {
            escape = true;
            continue;
        }
        if (c == '"')
        {
            in_string = !in_string;
            continue;
        }
        if (in_string)
        {
            continue;
        }
        if (c == '{')
        {
            ++depth;
        }
        else if (c == '}')
        {
            --depth;
            if (depth == 0)
            {
                return i + 1U;
            }
        }
    }
    return std::string::npos;
}

static bool extract_string(const std::string& obj, const char* key, std::string& out)
{
    const std::string token = std::string("\"") + key + "\"";
    const size_t kpos = obj.find(token);
    if (kpos == std::string::npos)
    {
        return false;
    }
    size_t pos = obj.find(':', kpos + token.size());
    if (pos == std::string::npos)
    {
        return false;
    }
    pos = obj.find('"', pos + 1U);
    if (pos == std::string::npos)
    {
        return false;
    }
    const size_t start = pos + 1U;
    bool escape = false;
    size_t end = start;
    while (end < obj.size())
    {
        if (escape)
        {
            escape = false;
        }
        else if (obj[end] == '\\')
        {
            escape = true;
        }
        else if (obj[end] == '"')
        {
            break;
        }
        ++end;
    }
    if (end >= obj.size())
    {
        return false;
    }
    out = obj.substr(start, end - start);
    return true;
}

static bool extract_int(const std::string& obj, const char* key, int& out)
{
    const std::string token = std::string("\"") + key + "\"";
    const size_t kpos = obj.find(token);
    if (kpos == std::string::npos)
    {
        return false;
    }
    size_t pos = obj.find(':', kpos + token.size());
    if (pos == std::string::npos)
    {
        return false;
    }
    ++pos;
    while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t' || obj[pos] == '\n'))
    {
        ++pos;
    }
    if (pos >= obj.size())
    {
        return false;
    }
    errno = 0;
    char* endp = nullptr;
    const long val = strtol(obj.c_str() + pos, &endp, 10);
    if (endp == obj.c_str() + pos || errno != 0)
    {
        return false;
    }
    out = static_cast<int>(val);
    return true;
}

static bool extract_float(const std::string& obj, const char* key, float& out)
{
    const std::string token = std::string("\"") + key + "\"";
    const size_t kpos = obj.find(token);
    if (kpos == std::string::npos)
    {
        return false;
    }
    size_t pos = obj.find(':', kpos + token.size());
    if (pos == std::string::npos)
    {
        return false;
    }
    ++pos;
    while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t' || obj[pos] == '\n'))
    {
        ++pos;
    }
    if (pos >= obj.size())
    {
        return false;
    }
    errno = 0;
    char* endp = nullptr;
    const double val = strtod(obj.c_str() + pos, &endp);
    if (endp == obj.c_str() + pos || errno != 0)
    {
        return false;
    }
    out = static_cast<float>(val);
    return true;
}

} // namespace

std::vector<ParsedChargerLocation>
PilotServerPayloadParser::parse_charger_locations(
        const std::string& body)
{
    std::vector<ParsedChargerLocation> result;

    size_t pos = 0U;
    while (pos < body.size())
    {
        const size_t obj_start = body.find('{', pos);
        if (obj_start == std::string::npos)
        {
            break;
        }
        const size_t obj_end = find_object_end(body, obj_start);
        if (obj_end == std::string::npos)
        {
            std::cerr << "[payload_parser] parse_charger_locations: unmatched '{'" << std::endl;
            break;
        }

        const std::string obj = body.substr(obj_start, obj_end - obj_start);

        ParsedChargerLocation loc;

        extract_int(obj, "id", loc.id);
        if (!extract_string(obj, "charger_name", loc.name))
        {
            extract_string(obj, "name", loc.name); // fallback
        }

        // Try flat fields first, then nested "position" object
        const bool has_flat =
            extract_float(obj, "latitude", loc.latitude) &&
            extract_float(obj, "longitude", loc.longitude);

        if (!has_flat)
        {
            const size_t pos_key = obj.find("\"position\"");
            if (pos_key != std::string::npos)
            {
                const size_t inner_start = obj.find('{', pos_key);
                if (inner_start != std::string::npos)
                {
                    const size_t inner_end = find_object_end(obj, inner_start);
                    if (inner_end != std::string::npos)
                    {
                        const std::string inner = obj.substr(inner_start,
                                                             inner_end - inner_start);
                        extract_float(inner, "latitude",  loc.latitude);
                        extract_float(inner, "longitude", loc.longitude);
                    }
                }
            }
        }

        result.push_back(loc);
        pos = obj_end;
    }

    return result;
}

} // namespace common
} // namespace server
} // namespace safe_edge
