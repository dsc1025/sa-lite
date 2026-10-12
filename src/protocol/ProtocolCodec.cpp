#include "ProtocolCodec.h"
#include <algorithm>
#include <stdexcept>

namespace Protocol
{
    namespace
    {
        constexpr char Table[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz{}";

        std::string IntegerBytes(std::uint32_t value, const std::array<int, 4>& rule)
        {
            std::string bytes(4, '\0');
            for (int i = 0; i < 4; ++i)
                bytes[rule[i] - 1] = static_cast<char>((value >> (i * 8)) & 255);
            return bytes;
        }
    }

    std::string Encode64(const std::string& bytes, const std::string& key, int direction)
    {
        std::string result;
        unsigned int bits = 0;
        std::size_t keyIndex = 0;
        auto append = [&](unsigned int value)
        {
            int shift = key.empty() ? 0 : static_cast<unsigned char>(key[keyIndex++ % key.size()]);
            result += Table[(static_cast<int>(value) + direction * shift + 256) % 64];
        };
        for (std::size_t i = 0; i < bytes.size(); ++i)
        {
            bits |= static_cast<unsigned char>(bytes[i]) << ((i % 3) * 2);
            append(bits & 63);
            bits >>= 6;
            if (i % 3 == 2)
            {
                append(bits & 63);
                bits = 0;
            }
        }
        // The legacy codec omits a final zero chunk.
        if (bits != 0)
            append(bits);
        return result;
    }

    std::string Decode64(const std::string& encoded, const std::string& key, int direction)
    {
        std::string result;
        unsigned int bits = 0;
        for (std::size_t i = 0; i < encoded.size(); ++i)
        {
            const char* position = std::find(Table, Table + 64, encoded[i]);
            if (position == Table + 64)
                throw std::runtime_error("Invalid protocol alphabet");
            int shift = key.empty() ? 0 : static_cast<unsigned char>(key[i % key.size()]);
            auto value = static_cast<unsigned int>((position - Table + direction * shift + 256) % 64);
            if (i % 4 == 0)
                bits = value;
            else
            {
                bits |= value << ((4 - i % 4) * 2);
                result += static_cast<char>(bits & 255);
                bits >>= 8;
            }
        }
        if (bits != 0)
            result += static_cast<char>(bits & 255);
        return result;
    }

    std::uint32_t ReadInteger(std::string bytes, const std::array<int, 4>& rule)
    {
        if (bytes.size() < 3 || bytes.size() > 4)
            throw std::runtime_error("Invalid protocol integer");
        bytes.resize(4, '\0');
        std::uint32_t result = 0;
        for (int i = 0; i < 4; ++i)
            result |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i]) ^ 255)
                << ((rule[i] - 1) * 8);
        return result;
    }

    std::string EncodeInteger(std::uint32_t value, const std::string& key)
    {
        auto bytes = IntegerBytes(value, { 3, 1, 4, 2 });
        for (char& byte : bytes)
            byte = static_cast<char>(static_cast<unsigned char>(byte) ^ 255);
        return Encode64(bytes, key, 1);
    }

    std::string EncodePacket(std::string raw)
    {
        // A zero rotation is a valid seed in the server's 0..98 range.
        std::string packet = Encode64(std::string(4, static_cast<char>(255)), "", 0) + raw;
        for (char& byte : packet)
            byte = static_cast<char>(static_cast<unsigned char>(byte) ^ 255);
        packet += '\n';
        return packet;
    }

    std::vector<std::string> DecodePacket(std::string packet)
    {
        if (packet.size() < 7)
            throw std::runtime_error("Incomplete protocol packet");
        for (char& byte : packet)
            byte = static_cast<char>(static_cast<unsigned char>(byte) ^ 255);
        auto seed = ReadInteger(Decode64(packet.substr(0, 6), "", 0), { 3, 1, 4, 2 });
        if (seed > 98)
            throw std::runtime_error("Invalid protocol seed");
        std::string raw = packet.substr(6);
        auto rotation = seed % raw.size();
        std::rotate(raw.begin(), raw.end() - rotation, raw.end());
        std::vector<std::string> fields;
        std::size_t start = 0;
        for (std::size_t end = raw.find(';'); end != std::string::npos; end = raw.find(';', start))
        {
            fields.push_back(raw.substr(start, end - start));
            start = end + 1;
        }
        if (start != raw.size() || fields.size() < 3 || fields.front() != "&" || fields.back() != "#")
            throw std::runtime_error("Invalid protocol frame");
        return fields;
    }
}
