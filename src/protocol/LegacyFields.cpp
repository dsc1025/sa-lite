#include "LegacyFields.h"
#include <stdexcept>

namespace Protocol
{
    std::vector<std::string> SplitLegacy(const std::string& value)
    {
        std::vector<std::string> fields;
        std::size_t start = 0;
        for (std::size_t i = 0; i < value.size(); ++i)
        {
            // A legacy double-byte character may contain a delimiter as its second byte.
            if (static_cast<unsigned char>(value[i]) >= 128 && i + 1 < value.size())
                ++i;
            else if (value[i] == '|')
            {
                fields.push_back(value.substr(start, i - start));
                start = i + 1;
            }
        }
        fields.push_back(value.substr(start));
        return fields;
    }

    std::string UnescapeLegacy(const std::string& value)
    {
        std::string result;
        for (std::size_t i = 0; i < value.size(); ++i)
        {
            if (static_cast<unsigned char>(value[i]) >= 128 && i + 1 < value.size())
            {
                result += value[i];
                result += value[++i];
            }
            else if (value[i] == '\\' && i + 1 < value.size())
            {
                char escaped = value[++i];
                switch (escaped)
                {
                case 'n': result += '\n'; break;
                case 'c': result += ','; break;
                case 'z': result += '|'; break;
                case 'y': result += '\\'; break;
                default: result += escaped; break;
                }
            }
            else
                result += value[i];
        }
        return result;
    }

    int ParseNumber(const std::string& value)
    {
        std::size_t consumed = 0;
        int result = std::stoi(value, &consumed);
        if (consumed != value.size())
            throw std::runtime_error("Invalid character number");
        return result;
    }

    std::uint32_t ParseMask(const std::string& value)
    {
        const std::string alphabet = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::uint64_t mask = 0;
        if (value.empty())
            throw std::runtime_error("Missing status mask");
        for (char digit : value)
        {
            auto number = alphabet.find(digit);
            if (number == std::string::npos)
                throw std::runtime_error("Invalid status mask");
            mask = mask * 62 + number;
            if (mask > UINT32_MAX)
                throw std::runtime_error("Status mask too large");
        }
        return static_cast<std::uint32_t>(mask);
    }
}
