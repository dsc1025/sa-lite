#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Protocol
{
    std::string Encode64(const std::string& bytes, const std::string& key, int direction);
    std::string Decode64(const std::string& encoded, const std::string& key, int direction);
    std::uint32_t ReadInteger(std::string bytes, const std::array<int, 4>& rule);
    std::string EncodeInteger(std::uint32_t value, const std::string& key);
    std::string EncodePacket(std::string raw);
    std::vector<std::string> DecodePacket(std::string packet);
}
