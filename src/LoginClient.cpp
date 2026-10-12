#include "LoginClient.h"
#include <ws2tcpip.h>
#include <algorithm>
#include <array>
#include <initializer_list>
#include <stdexcept>
#include <vector>

namespace
{
    constexpr char Table[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz{}";
    constexpr char DefaultKey[] = "cary";
    constexpr char RunningKey[] = "cary";

    [[noreturn]] void SocketError(const char* operation)
    {
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(WSAGetLastError()));
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

    std::string IntegerBytes(std::uint32_t value, const std::array<int, 4>& rule)
    {
        std::string bytes(4, '\0');
        for (int i = 0; i < 4; ++i)
            bytes[rule[i] - 1] = static_cast<char>((value >> (i * 8)) & 255);
        return bytes;
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

    void CheckCredential(const std::string& value)
    {
        if (value.empty() || value.size() > 15 || std::any_of(value.begin(), value.end(),
            [](unsigned char byte) { return byte < 33 || byte > 126; }))
            throw std::invalid_argument("Account and password must contain 1..15 printable ASCII characters, without spaces");
    }

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

    void ApplyStatus(const std::vector<std::string>& values, std::size_t start, std::uint32_t mask,
        const std::vector<int*>& numbers, std::string* name = nullptr, std::string* nickname = nullptr)
    {
        for (std::size_t bit = 1; bit <= 31; ++bit)
        {
            if (!(mask & (std::uint32_t{1} << bit)))
                continue;
            if (start >= values.size())
                throw std::runtime_error("Incomplete status update");
            if (bit <= numbers.size() && numbers[bit - 1])
                *numbers[bit - 1] = ParseNumber(values[start]);
            else if (name && bit == numbers.size() + 1)
                *name = UnescapeLegacy(values[start]);
            else if (nickname && bit == numbers.size() + 2)
                *nickname = UnescapeLegacy(values[start]);
            ++start;
        }
    }

    void ApplyExtra(const std::vector<std::string>& values, int& perception, int& rebirth)
    {
        // STW1 is appended after the legacy fields, including names.
        for (std::size_t end = values.size(); end >= 3; --end)
            if (values[end - 3] == "STW1")
            {
                perception = ParseNumber(values[end - 2]);
                rebirth = ParseNumber(values[end - 1]);
                break;
            }
    }

    LoginClient::ItemStatus ParseItem(const std::vector<std::string>& values, std::size_t start)
    {
        LoginClient::ItemStatus item;
        if (values[start].empty())
            return item;
        item.present = true;
        item.name = UnescapeLegacy(values[start]);
        item.description = UnescapeLegacy(values[start + 3]);
        item.level = ParseNumber(values[start + 7]);
        return item;
    }
}

LoginClient::LoginClient()
{
    WSADATA data{};
    int error = WSAStartup(MAKEWORD(2, 2), &data);
    if (error != 0)
        throw std::runtime_error("WSAStartup: " + std::to_string(error));
}

LoginClient::~LoginClient()
{
    Disconnect();
    WSACleanup();
}

void LoginClient::Disconnect() noexcept
{
    if (socket_ != INVALID_SOCKET)
        closesocket(socket_);
    socket_ = INVALID_SOCKET;
    loggedIn_ = false;
    inGame_ = false;
    sessionKey_.clear();
    characters_.clear();
    playerStatus_ = {};
    pets_.assign(5, {});
    items_.assign(20, {});
    itemsReceived_ = false;
    battleReplySerial_ = 0;
    battleReplyAccepted_ = false;
    waitingRide_ = false;
    rideError_.clear();
    nextHeartbeat_ = 0;
    pending_.clear();
}

bool LoginClient::IsConnected() const noexcept
{
    return socket_ != INVALID_SOCKET;
}

void LoginClient::Wait(bool writing, ULONGLONG deadline)
{
    auto now = GetTickCount64();
    if (now >= deadline)
        throw std::runtime_error("Connection or login timed out");
    auto remaining = deadline - now;
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(remaining / 1000);
    timeout.tv_usec = static_cast<long>((remaining % 1000) * 1000);
    fd_set ready, errors;
    FD_ZERO(&ready);
    FD_ZERO(&errors);
    FD_SET(socket_, &ready);
    FD_SET(socket_, &errors);
    int result = select(0, writing ? nullptr : &ready, writing ? &ready : nullptr, &errors, &timeout);
    if (result == SOCKET_ERROR)
        SocketError("select");
    if (result == 0)
        throw std::runtime_error("Connection or login timed out");
    if (FD_ISSET(socket_, &errors))
        throw std::runtime_error("Socket connection failed");
}

void LoginClient::Connect(const std::string& address, std::uint16_t port, int timeoutMs)
{
    if (port == 0 || timeoutMs <= 0)
        throw std::invalid_argument("Invalid port or timeout");
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    if (InetPtonA(AF_INET, address.c_str(), &endpoint.sin_addr) != 1)
        throw std::invalid_argument("Enter a numeric IPv4 server address");
    Disconnect();
    try
    {
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET)
            SocketError("socket");
        u_long nonblocking = 1;
        if (ioctlsocket(socket_, FIONBIO, &nonblocking) == SOCKET_ERROR)
            SocketError("ioctlsocket");
        auto deadline = GetTickCount64() + timeoutMs;
        if (connect(socket_, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)) == SOCKET_ERROR)
        {
            if (WSAGetLastError() != WSAEWOULDBLOCK)
                SocketError("connect");
            Wait(true, deadline);
            int error = 0;
            int length = sizeof(error);
            if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) == SOCKET_ERROR)
                SocketError("getsockopt");
            if (error != 0)
                throw std::runtime_error("connect: " + std::to_string(error));
        }
        auto greeting = Receive(deadline, '\0', 63);
        if (!greeting.empty() && greeting[0] == 'E')
            throw std::runtime_error("Server is busy or refusing this connection");
        if (greeting.size() != 1 || (greeting[0] != '$' && greeting[0] != 'L'))
            throw std::runtime_error("Unsupported server greeting");
    }
    catch (...)
    {
        Disconnect();
        throw;
    }
}

void LoginClient::Send(const std::string& packet, ULONGLONG deadline)
{
    std::size_t sent = 0;
    while (sent < packet.size())
    {
        Wait(true, deadline);
        int count = send(socket_, packet.data() + sent, static_cast<int>(packet.size() - sent), 0);
        if (count == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAEWOULDBLOCK)
                continue;
            SocketError("send");
        }
        if (count == 0)
            throw std::runtime_error("Server closed connection");
        sent += count;
    }
}

std::string LoginClient::Receive(ULONGLONG deadline, char terminator, std::size_t maxSize)
{
    for (;;)
    {
        auto end = pending_.find(terminator);
        if (end != std::string::npos)
        {
            if (end > maxSize)
                throw std::runtime_error("Protocol packet too large");
            auto packet = pending_.substr(0, end);
            pending_.erase(0, end + 1);
            return packet;
        }
        if (pending_.size() > maxSize)
            throw std::runtime_error("Protocol packet too large");
        Wait(false, deadline);
        char buffer[4096];
        int count = recv(socket_, buffer, sizeof(buffer), 0);
        if (count == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAEWOULDBLOCK)
                continue;
            SocketError("recv");
        }
        if (count == 0)
            throw std::runtime_error("Server closed connection");
        pending_.append(buffer, count);
    }
}

LoginClient::LoginResult LoginClient::Login(const std::string& account,
    const std::string& password, int timeoutMs)
{
    CheckCredential(account);
    CheckCredential(password);
    if (!IsConnected() || loggedIn_ || timeoutMs <= 0)
        throw std::logic_error("Login requires a new connection and a positive timeout");
    try
    {
        auto deadline = GetTickCount64() + timeoutMs;
        auto checksum = static_cast<std::uint32_t>(account.size() + password.size());
        Send(EncodePacket("&;71;" + Encode64(account, DefaultKey, -1) + ";"
            + Encode64(password, DefaultKey, -1) + ";" + EncodeInteger(checksum, DefaultKey) + ";#;"), deadline);
        auto fields = DecodePacket(Receive(deadline));
        if (fields.size() != 5 || fields[1] != "72")
            throw std::runtime_error("Unexpected login reply");
        std::string key = account + RunningKey;
        auto result = Decode64(fields[2], key, 1);
        auto receivedChecksum = ReadInteger(Decode64(fields[3], key, -1), { 2, 4, 1, 3 });
        if (receivedChecksum != result.size() || (result != "ok" && result != "no"))
            throw std::runtime_error("Invalid login reply or incompatible protocol key");
        loggedIn_ = result == "ok";
        if (loggedIn_)
            sessionKey_ = key;
        if (!loggedIn_)
            Disconnect();
        return loggedIn_ ? LoginResult::Accepted : LoginResult::Rejected;
    }
    catch (...)
    {
        Disconnect();
        throw;
    }
}

std::vector<std::string> LoginClient::ReceiveStrings(const std::vector<std::string>& fields) const
{
    if (fields.size() < 4)
        throw std::runtime_error("Incomplete server reply");
    std::vector<std::string> strings;
    std::uint32_t checksum = 0;
    for (std::size_t i = 2; i + 2 < fields.size(); ++i)
    {
        auto value = Decode64(fields[i], sessionKey_, 1);
        checksum += static_cast<std::uint32_t>(value.size());
        strings.push_back(value);
    }
    auto received = ReadInteger(Decode64(fields[fields.size() - 2], sessionKey_, -1), { 2, 4, 1, 3 });
    if (received != checksum)
        throw std::runtime_error("Invalid server reply checksum");
    return strings;
}

bool LoginClient::HandleGamePacket(const std::vector<std::string>& fields)
{
    if (fields[1] == "36" && waitingRide_)
    {
        if (fields.size() != 7)
            throw std::runtime_error("Invalid server message");
        auto sender = ReadInteger(Decode64(fields[2], sessionKey_, -1), { 2, 4, 1, 3 });
        auto message = Decode64(fields[3], sessionKey_, 1);
        auto color = ReadInteger(Decode64(fields[4], sessionKey_, -1), { 2, 4, 1, 3 });
        auto checksum = ReadInteger(Decode64(fields[5], sessionKey_, -1), { 2, 4, 1, 3 });
        if (checksum != sender + static_cast<std::uint32_t>(message.size()) + color)
            throw std::runtime_error("Invalid server message checksum");
        if (sender == static_cast<std::uint32_t>(-1) && message.compare(0, 2, "P|") == 0)
        {
            auto text = UnescapeLegacy(message.substr(2));
            int length = MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring wide(length, L'\0');
            if (length > 0)
                MultiByteToWideChar(936, 0, text.data(), static_cast<int>(text.size()), &wide[0], length);
            if (wide.find(L"骑") != std::wstring::npos || wide.find(L"忠诚") != std::wstring::npos ||
                wide.find(L"模型不匹配") != std::wstring::npos)
                rideError_ = text;
        }
        return false;
    }
    if (fields[1] == "55" || fields[1] == "115")
    {
        if (fields.size() != 6)
            throw std::runtime_error("Invalid pet state reply");
        auto slot = ReadInteger(Decode64(fields[2], sessionKey_, -1), { 2, 4, 1, 3 });
        auto result = ReadInteger(Decode64(fields[3], sessionKey_, -1), { 2, 4, 1, 3 });
        auto checksum = ReadInteger(Decode64(fields[4], sessionKey_, -1), { 2, 4, 1, 3 });
        if (checksum != slot + result)
            throw std::runtime_error("Invalid pet state checksum");
        if (fields[1] == "55")
        {
            ++battleReplySerial_;
            battleReplyAccepted_ = result != 0;
        }
        if (!result)
            return false;
        if (fields[1] == "115")
        {
            if (slot & ~((std::uint32_t{1} << pets_.size()) - 1))
                throw std::runtime_error("Invalid standby pet mask");
            playerStatus_.standbyPets = slot;
            playerStatus_.standbyReceived = true;
            return true;
        }
        if (slot != static_cast<std::uint32_t>(-1) && slot >= pets_.size())
            throw std::runtime_error("Invalid battle pet slot");
        playerStatus_.battlePet = slot == static_cast<std::uint32_t>(-1) ? -1 : static_cast<int>(slot);
        playerStatus_.battleReceived = true;
        return true;
    }
    if (fields[1] != "46" && fields[1] != "88" && fields[1] != "22")
        return false;
    auto strings = ReceiveStrings(fields);
    if (strings.size() != 1)
        throw std::runtime_error("Invalid status or heartbeat reply");
    if (fields[1] == "88" || strings[0].empty())
        return false;
    const auto& data = strings[0];
    if (fields[1] == "22" || data[0] == 'I')
    {
        bool partial = fields[1] == "22";
        auto values = SplitLegacy(partial ? data : data.substr(1));
        auto updated = partial ? items_ : std::vector<ItemStatus>(20);
        std::size_t width = partial ? 10 : 9;
        if (values.size() % width != 0 || (!partial && values.size() != updated.size() * width))
            throw std::runtime_error("Incomplete item status");
        for (std::size_t i = 0; i < values.size(); i += width)
        {
            int slot = partial ? ParseNumber(values[i]) : static_cast<int>(i / width);
            if (slot < 0 || static_cast<std::size_t>(slot) >= updated.size())
                throw std::runtime_error("Invalid item slot");
            updated[slot] = ParseItem(values, i + (partial ? 1 : 0));
        }
        items_ = updated;
        if (!partial)
            itemsReceived_ = true;
        return true;
    }
    auto values = SplitLegacy(data);
    if (data[0] == 'P')
    {
        auto status = playerStatus_;
        std::vector<int*> numbers = { &status.hp, &status.maxHp, &status.mp, &status.maxMp,
            &status.vital, &status.strength, &status.toughness, &status.dexterity,
            &status.experience, &status.nextExperience, &status.level, &status.attack,
            &status.defence, &status.quick, &status.charm, &status.luck,
            &status.earth, &status.water, &status.fire, &status.wind, &status.gold,
            &status.title, &status.duelPoints, &status.rebirth, nullptr, nullptr, &status.ridePet };
        bool full = values[0] == "P1";
        ApplyStatus(values, 1, full ? 0x01fffffe : ParseMask(values[0].substr(1)), numbers);
        if (full && values.size() > 25)
            status.ridePet = ParseNumber(values[25]);
        ApplyExtra(values, status.perception, status.rebirth);
        if (full)
            status.received = true;
        playerStatus_ = status;
        return true;
    }
    if (data[0] == 'K')
    {
        if (values.size() < 2)
            throw std::runtime_error("Incomplete pet status");
        int slot = ParseNumber(values[0].substr(1));
        if (slot < 0 || static_cast<std::size_t>(slot) >= pets_.size())
            throw std::runtime_error("Invalid pet slot");
        if (values[1] == "0")
        {
            pets_[slot] = {};
            pets_[slot].received = true;
            return true;
        }
        auto pet = pets_[slot];
        std::vector<int*> numbers = { &pet.image, &pet.hp, &pet.maxHp, &pet.mp, &pet.maxMp,
            &pet.experience, &pet.nextExperience, &pet.level, &pet.attack, &pet.defence,
            &pet.quick, &pet.loyalty, &pet.earth, &pet.water, &pet.fire, &pet.wind,
            &pet.skillSlots, &pet.canRename };
        bool full = values[1] == "1";
        auto mask = full ? 0x001ffffe : ParseMask(values[1]);
        // The server's partial-pet table omits MAXMP, so this bit carries no value.
        if (!full)
            mask &= ~(std::uint32_t{1} << 5);
        ApplyStatus(values, 2, mask, numbers, &pet.name, &pet.nickname);
        ApplyExtra(values, pet.perception, pet.rebirth);
        pet.present = true;
        if (full)
            pet.received = true;
        pets_[slot] = pet;
        return true;
    }
    return false;
}

std::vector<std::string> LoginClient::WaitReply(const std::string& function, ULONGLONG deadline)
{
    for (;;)
    {
        if (GetTickCount64() >= deadline)
            throw std::runtime_error("Server reply timed out");
        auto fields = DecodePacket(Receive(deadline));
        if (fields[1] == function)
        {
            auto strings = ReceiveStrings(fields);
            if (strings.size() != 2)
                throw std::runtime_error("Invalid character reply");
            return strings;
        }
        HandleGamePacket(fields);
    }
}

std::vector<LoginClient::Character> LoginClient::GetCharacters(int timeoutMs)
{
    if (!loggedIn_ || inGame_ || timeoutMs <= 0)
        throw std::logic_error("Character list requires an account login");
    try
    {
        auto deadline = GetTickCount64() + timeoutMs;
        Send(EncodePacket("&;79;" + EncodeInteger(0, sessionKey_) + ";#;"), deadline);
        auto reply = WaitReply("80", deadline);
        if (reply[0] != "successful")
            throw std::runtime_error("Character list rejected: " + reply[1]);
        characters_.clear();
        if (reply[1].empty())
            return characters_;
        auto entries = SplitLegacy(reply[1]);
        if (entries.size() % 2 != 0)
            throw std::runtime_error("Invalid character list");
        for (std::size_t i = 0; i < entries.size(); i += 2)
        {
            Character character;
            character.name = UnescapeLegacy(entries[i]);
            if (character.name.empty() || character.name.size() >= 32 || character.name.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid character name");
            if (!entries[i + 1].empty())
            {
                auto options = SplitLegacy(UnescapeLegacy(entries[i + 1]));
                if (options.size() < 3)
                    throw std::runtime_error("Incomplete character options");
                character.level = ParseNumber(options[2]);
            }
            characters_.push_back(character);
        }
        return characters_;
    }
    catch (...)
    {
        Disconnect();
        throw;
    }
}

void LoginClient::EnterGame(const Character& character, int timeoutMs)
{
    if (!loggedIn_ || inGame_ || timeoutMs <= 0 || std::none_of(characters_.begin(), characters_.end(),
        [&](const Character& available) { return available.name == character.name; }))
        throw std::logic_error("Select a character from the current account list");
    try
    {
        auto deadline = GetTickCount64() + timeoutMs;
        Send(EncodePacket("&;77;" + Encode64(character.name, sessionKey_, -1) + ";"
            + EncodeInteger(static_cast<std::uint32_t>(character.name.size()), sessionKey_) + ";#;"), deadline);
        auto reply = WaitReply("78", deadline);
        if (reply[0] != "successful")
            throw std::runtime_error("Character login rejected: " + reply[1]);
        inGame_ = true;
        nextHeartbeat_ = GetTickCount64() + 30000;
    }
    catch (...)
    {
        Disconnect();
        throw;
    }
}

bool LoginClient::PollGame(int timeoutMs)
{
    if (!inGame_ || timeoutMs <= 0)
        throw std::logic_error("Game polling requires a character login");
    try
    {
        auto now = GetTickCount64();
        if (now >= nextHeartbeat_)
        {
            const std::string heartbeat = "ping";
            Send(EncodePacket("&;87;" + Encode64(heartbeat, sessionKey_, -1) + ";"
                + EncodeInteger(static_cast<std::uint32_t>(heartbeat.size()), sessionKey_) + ";#;"), now + 10000);
            nextHeartbeat_ = GetTickCount64() + 30000;
        }
        if (pending_.find('\n') == std::string::npos)
        {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(socket_, &readable);
            timeval timeout{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
            int ready = select(0, &readable, nullptr, nullptr, &timeout);
            if (ready == SOCKET_ERROR)
                SocketError("select");
            if (ready == 0)
                return false;
        }
        return HandleGamePacket(DecodePacket(Receive(GetTickCount64() + 10000)));
    }
    catch (...)
    {
        Disconnect();
        throw;
    }
}

void LoginClient::SetPetState(int slot, PetState state)
{
    if (!inGame_ || slot < 0 || slot >= static_cast<int>(pets_.size()) || !pets_[slot].present ||
        !pets_[slot].received || state < PetState::Standby || state > PetState::Ride)
        throw std::invalid_argument("请选择有效的宠物");
    if (state == PetState::Ride && playerStatus_.ridePet >= 0 && playerStatus_.ridePet != slot)
        throw std::invalid_argument("当前已有骑宠，请先将它改为等待，再选择新的骑宠");
    auto deadline = GetTickCount64() + 10000;
    auto sendNumbers = [&](const char* function, std::initializer_list<int> numbers)
    {
        std::string raw = std::string("&;") + function + ";";
        std::uint32_t checksum = 0;
        for (int number : numbers)
        {
            auto value = static_cast<std::uint32_t>(number);
            raw += EncodeInteger(value, sessionKey_) + ";";
            checksum += value;
        }
        Send(EncodePacket(raw + EncodeInteger(checksum, sessionKey_) + ";#;"), deadline);
    };
    auto ride = [&](int pet)
    {
        if (playerStatus_.ridePet == pet)
            return;
        waitingRide_ = true;
        rideError_.clear();
        try
        {
            std::string message = "R|P|" + std::to_string(pet);
            Send(EncodePacket("&;94;" + Encode64(message, sessionKey_, -1) + ";" +
                EncodeInteger(static_cast<std::uint32_t>(message.size()), sessionKey_) + ";#;"), deadline);
            while (playerStatus_.ridePet != pet)
            {
                if (!rideError_.empty())
                    throw std::runtime_error(rideError_);
                if (GetTickCount64() >= deadline)
                    throw std::runtime_error("服务器未确认骑乘，请检查宠物等级、忠诚及骑乘资格");
                PollGame(100);
            }
        }
        catch (...)
        {
            waitingRide_ = false;
            throw;
        }
        waitingRide_ = false;
    };
    auto battle = [&](int pet)
    {
        if (playerStatus_.battlePet == pet)
            return;
        auto serial = battleReplySerial_;
        sendNumbers("54", { pet });
        while (battleReplySerial_ == serial)
        {
            if (GetTickCount64() >= deadline)
                throw std::runtime_error("服务器未确认出战，请检查宠物状态及是否正在战斗");
            PollGame(100);
        }
        if (!battleReplyAccepted_ || playerStatus_.battlePet != pet)
            throw std::runtime_error("服务器拒绝出战设置，请检查宠物状态及是否正在战斗");
    };
    if (playerStatus_.ridePet == slot && state != PetState::Ride)
        ride(-1);
    if (state != PetState::Battle && playerStatus_.battlePet == slot)
        battle(-1);
    if (state == PetState::Standby)
        sendNumbers("96", { slot, 1 });
    if (state == PetState::Battle)
        battle(slot);
    if (state == PetState::Ride)
        ride(slot);
}

void LoginClient::LeaveGame(int timeoutMs)
{
    if (!inGame_ || timeoutMs <= 0)
        throw std::logic_error("Logout requires a character login");
    try
    {
        auto deadline = GetTickCount64() + timeoutMs;
        Send(EncodePacket("&;81;" + EncodeInteger(0, sessionKey_) + ";#;"), deadline);
        auto reply = WaitReply("82", deadline);
        if (reply[0] != "successful")
            throw std::runtime_error("Character logout failed: " + reply[1]);
        Disconnect();
    }
    catch (...)
    {
        Disconnect();
        throw;
    }
}

const LoginClient::PlayerStatus& LoginClient::GetPlayerStatus() const noexcept
{
    return playerStatus_;
}

const std::vector<LoginClient::PetStatus>& LoginClient::GetPets() const noexcept
{
    return pets_;
}

const std::vector<LoginClient::ItemStatus>& LoginClient::GetItems() const noexcept
{
    return items_;
}

bool LoginClient::HasItems() const noexcept
{
    return itemsReceived_;
}
