#include "LoginClient.h"
#include "protocol/ProtocolCodec.h"
#include "protocol/LegacyFields.h"
#include <ws2tcpip.h>
#include <algorithm>
#include <stdexcept>

using namespace Protocol;

namespace
{
    constexpr char DefaultKey[] = "cary";
    constexpr char RunningKey[] = "cary";

    void CheckCredential(const std::string& value)
    {
        if (value.empty() || value.size() > 15 || std::any_of(value.begin(), value.end(),
            [](unsigned char byte) { return byte < 33 || byte > 126; }))
            throw std::invalid_argument("Account and password must contain 1..15 printable ASCII characters, without spaces");
    }
}

LoginClient::LoginClient() = default;

LoginClient::~LoginClient()
{
    Disconnect();
}

void LoginClient::Disconnect() noexcept
{
    connection_.Disconnect();
    loggedIn_ = false;
    inGame_ = false;
    sessionKey_.clear();
    characters_.clear();
    playerStatus_ = {};
    encounter_ = {};
    battle_ = {};
    battleResult_ = {};
    positionSerial_ = 0;
    eventSequence_ = 0;
    pets_.assign(5, {});
    items_.assign(20, {});
    magic_.assign(5, {});
    itemsReceived_ = false;
    itemRevision_ = 0;
    itemStorage_.clear();
    itemStorageReceived_ = false;
    waitingFood_ = false;
    foodMessage_.clear();
    petStorage_.clear();
    petStorageReceived_ = false;
    battleReplySerial_ = 0;
    battleReplyAccepted_ = false;
    waitingRide_ = false;
    rideError_.clear();
    nextHeartbeat_ = 0;
}

bool LoginClient::IsConnected() const noexcept
{
    return connection_.IsConnected();
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
        auto deadline = GetTickCount64() + timeoutMs;
        connection_.Connect(endpoint, deadline);
        auto greeting = connection_.Receive(deadline, '\0', 63);
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
        connection_.Send(EncodePacket("&;71;" + Encode64(account, DefaultKey, -1) + ";"
            + Encode64(password, DefaultKey, -1) + ";" + EncodeInteger(checksum, DefaultKey) + ";#;"), deadline);
        auto fields = DecodePacket(connection_.Receive(deadline));
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

std::vector<std::string> LoginClient::WaitReply(const std::string& function, ULONGLONG deadline)
{
    for (;;)
    {
        if (GetTickCount64() >= deadline)
            throw std::runtime_error("Server reply timed out");
        auto fields = DecodePacket(connection_.Receive(deadline));
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
        connection_.Send(EncodePacket("&;79;" + EncodeInteger(0, sessionKey_) + ";#;"), deadline);
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
                character.slot = ParseNumber(options[0]);
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

void LoginClient::ValidateNewCharacter(const NewCharacter& character)
{
    if (character.name.empty() || character.name.size() >= 32)
        throw std::invalid_argument("名字必须为 1～31 个旧编码字节");
    bool trail = false;
    for (unsigned char byte : character.name)
    {
        if (byte <= 0x20 || byte == 0xff || (byte >= 0x7f && byte <= 0xa0))
            throw std::invalid_argument("名字包含服务端不允许的字符");
        if (trail)
        {
            if (!((byte >= 0x40 && byte <= 0x7e) || (byte >= 0xa1 && byte <= 0xfe)))
                throw std::invalid_argument("名字编码无效");
            trail = false;
        }
        else if (byte >= 0xa1) trail = true;
    }
    if (trail) throw std::invalid_argument("名字编码不完整");
    if (character.model < 0 || character.model >= 12 || character.color < 0 || character.color >= 4 ||
        character.hometown < 0 || character.hometown >= 4)
        throw std::invalid_argument("外观或出生地无效");
    int total = 0;
    for (int point : character.points)
    {
        if (point < 0 || point > 20) throw std::invalid_argument("四围点数无效");
        total += point;
    }
    if (total != 20) throw std::invalid_argument("四围合计必须为 20");
    total = 0;
    int count = 0;
    for (int element : character.elements)
    {
        if (element < 0 || element > 10) throw std::invalid_argument("元素点数无效");
        total += element;
        if (element) ++count;
    }
    if (total != 10 || count > 2 || (character.elements[0] && character.elements[2]) ||
        (character.elements[1] && character.elements[3]))
        throw std::invalid_argument("元素组合无效");
}

bool LoginClient::CreateCharacter(const NewCharacter& character, std::string& error, int timeoutMs)
{
    ValidateNewCharacter(character);
    if (!loggedIn_ || inGame_ || timeoutMs <= 0)
        throw std::logic_error("创建角色需要先登录账号");
    int slot = -1;
    for (int candidate = 0; candidate < 2; ++candidate)
        if (std::none_of(characters_.begin(), characters_.end(),
            [&](const Character& available) { return available.slot == candidate; }))
        {
            slot = candidate;
            break;
        }
    if (characters_.size() >= 2 || slot < 0 || std::any_of(characters_.begin(), characters_.end(),
        [](const Character& available) { return available.slot < 0 || available.slot > 1; }))
        throw std::logic_error("没有可确认的空角色槽位");
    if (std::any_of(characters_.begin(), characters_.end(),
        [&](const Character& available) { return available.name == character.name; }))
        throw std::invalid_argument("账号下已有同名角色");
    try
    {
        auto deadline = GetTickCount64() + timeoutMs;
        std::string packet = "&;73;" + EncodeInteger(slot, sessionKey_) + ";" +
            Encode64(character.name, sessionKey_, -1) + ";";
        std::uint32_t checksum = static_cast<std::uint32_t>(slot + character.name.size());
        int values[] = { 100000 + character.model * 20 + character.color * 5,
            30000 + character.model * 100 + character.color * 25,
            character.points[0], character.points[1], character.points[2], character.points[3],
            character.elements[0], character.elements[1], character.elements[2], character.elements[3],
            character.hometown };
        for (int value : values)
        {
            packet += EncodeInteger(value, sessionKey_) + ";";
            checksum += value;
        }
        connection_.Send(EncodePacket(packet + EncodeInteger(checksum, sessionKey_) + ";#;"), deadline);
        auto reply = WaitReply("74", deadline);
        error = reply[1].empty() ? "服务端拒绝创建角色，请检查名字及服务器配置" : reply[1];
        return reply[0] == "successful";
    }
    catch (const std::exception& exception)
    {
        std::string message = "创建结果未确认，请重新登录检查角色列表：" + std::string(exception.what());
        Disconnect();
        throw std::runtime_error(message);
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
        connection_.Send(EncodePacket("&;77;" + Encode64(character.name, sessionKey_, -1) + ";"
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
            connection_.Send(EncodePacket("&;87;" + Encode64(heartbeat, sessionKey_, -1) + ";"
                + EncodeInteger(static_cast<std::uint32_t>(heartbeat.size()), sessionKey_) + ";#;"), now + 10000);
            nextHeartbeat_ = GetTickCount64() + 30000;
        }
        bool changed = false;
        if (connection_.Poll(timeoutMs))
            changed = HandleGamePacket(DecodePacket(connection_.Receive(GetTickCount64() + 10000)));
        return PollBattle() || changed;
    }
    catch (...)
    {
        Disconnect();
        throw;
    }
}

void LoginClient::SendNumbers(const char* function, std::initializer_list<int> numbers, ULONGLONG deadline)
{
    std::string raw = std::string("&;") + function + ";";
    std::uint32_t checksum = 0;
    for (int value : numbers)
    {
        auto number = static_cast<std::uint32_t>(value);
        raw += EncodeInteger(number, sessionKey_) + ";";
        checksum += number;
    }
    connection_.Send(EncodePacket(raw + EncodeInteger(checksum, sessionKey_) + ";#;"), deadline);
}

void LoginClient::LeaveGame(int timeoutMs)
{
    if (!inGame_ || timeoutMs <= 0)
        throw std::logic_error("Logout requires a character login");
    Disconnect();
}
