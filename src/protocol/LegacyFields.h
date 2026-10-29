#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Protocol
{
    std::vector<std::string> SplitLegacy(const std::string& value);
    std::string UnescapeLegacy(const std::string& value);
    int ParseNumber(const std::string& value);
    std::int64_t ParseNumber64(const std::string& value);
    std::int64_t ParseExperience62(const std::string& value);
    std::uint32_t ParseMask(const std::string& value);
}
