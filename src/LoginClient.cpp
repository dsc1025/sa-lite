#include "LoginClient.h"
#include "protocol/ProtocolCodec.h"
#include "protocol/LegacyFields.h"
#include <ws2tcpip.h>
#include <algorithm>
#include <stdexcept>
#include <utility>

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
    offlineResultReceived_ = false;
    offlineMessage_.clear();
    sessionKey_.clear();
    characters_.clear();
    chatMessages_.clear();
    playerStatus_ = {};
    encounter_ = {};
    captureStatistics_ = {};
    captureNames_ = {};
    battle_ = {};
    battleResult_ = {};
    battleDebug_.clear();
    positionSerial_ = 0;
    eventSequence_ = 0;
    pets_.assign(5, {});
    items_.assign(20, {});
    magic_.assign(5, {});
    itemsReceived_ = false;
    itemRevision_ = 0;
    itemMoveSerial_ = 0;
    itemMoveFrom_ = itemMoveTo_ = -1;
    shopItems_.clear();
    shopCategory_ = -1;
    pearlBalance_ = 0;
    auction_ = {};
    shopUncertain_ = false;
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
    Game::ValidateNewCharacter(character);
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
        connection_.Send(EncodePacket("&;126;" + Encode64(character.name, sessionKey_, -1) + ";"
            + EncodeInteger(static_cast<std::uint32_t>(character.name.size()), sessionKey_) + ";#;"), deadline);
        auto reply = WaitReply("78", deadline);
        if (reply[0] != "successful")
            throw std::runtime_error("Character login rejected: " + reply[1]);
        while (!offlineResultReceived_)
        {
            if (GetTickCount64() >= deadline)
                throw std::runtime_error("服务器未确认 lite 离线协议，请检查服务端版本");
            HandleGamePacket(DecodePacket(connection_.Receive(deadline)));
        }
        connection_.Send(EncodePacket("&;120;" + Encode64("", sessionKey_, -1) + ";"
            + EncodeInteger(0, sessionKey_) + ";#;"), deadline);
        inGame_ = true;
        nextHeartbeat_ = GetTickCount64() + 30000;
    }
    catch (...)
    {
        Disconnect();
        throw;
    }
}

bool LoginClient::HandleOfflineResult(const std::vector<std::string>& fields)
{
    auto strings = ReceiveStrings(fields);
    if (strings.size() != 1)
        throw std::runtime_error("Invalid offline settlement reply");
    auto values = SplitLegacy(strings[0]);
    if (values.size() < 3 || (values.size() - 3) % 3 != 0 ||
        (values.size() - 3) / 3 > pets_.size())
        throw std::runtime_error("Invalid offline settlement fields");
    auto number = [&](std::size_t index) -> std::uint64_t
    {
        const auto& value = values[index];
        if (value.empty() || value.size() > 19 ||
            value.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("Invalid offline settlement number");
        auto result = std::stoull(value);
        if (result > INT64_MAX)
            throw std::runtime_error("Offline settlement number too large");
        return result;
    };
    auto seconds = number(0), experience = number(1), levels = number(2);
    if (seconds > 24 * 60 * 60 || seconds % 60 != 0 || levels > INT32_MAX ||
        (seconds == 0 && (experience != 0 || levels != 0)))
        throw std::runtime_error("Invalid offline settlement result");
    std::vector<bool> petReceived(pets_.size(), false);
    std::wstring petMessages;
    for (std::size_t i = 3; i < values.size(); i += 3)
    {
        auto slot = number(i), petExperience = number(i + 1), petLevels = number(i + 2);
        if (slot >= pets_.size() || petReceived[slot] || petLevels > INT32_MAX ||
            (seconds == 0 && (petExperience != 0 || petLevels != 0)))
            throw std::runtime_error("Invalid offline settlement pet result");
        petReceived[slot] = true;
        petMessages += L"，宠物栏 " + std::to_wstring(slot + 1) + L" 经验 +" +
            std::to_wstring(petExperience) + L"（升级 " + std::to_wstring(petLevels) + L" 级）";
    }
    if (offlineResultReceived_)
        return false;
    offlineMessage_ = L"本次没有可领取的离线经验。";
    if (seconds > 0)
    {
        std::wstring message = L"离线挂机结算：" + std::to_wstring(seconds / 3600) + L"小时" +
            std::to_wstring(seconds / 60 % 60) + L"分钟，人物经验 +" + std::to_wstring(experience) +
            L"（升级 " + std::to_wstring(levels) + L" 级）";
        message += petMessages;
        offlineMessage_ = message;
        std::replace(offlineMessage_.begin(), offlineMessage_.end(), L'，', L'\n');
        int length = WideCharToMultiByte(936, 0, message.data(), static_cast<int>(message.size()),
            nullptr, 0, nullptr, nullptr);
        if (length <= 0)
            throw std::runtime_error("Cannot encode offline settlement message");
        std::string encoded(length, '\0');
        if (!WideCharToMultiByte(936, 0, message.data(), static_cast<int>(message.size()),
            encoded.data(), length, nullptr, nullptr))
            throw std::runtime_error("Cannot encode offline settlement message");
        chatMessages_.push_back(std::move(encoded));
        if (chatMessages_.size() > 500)
            chatMessages_.erase(chatMessages_.begin());
    }
    offlineResultReceived_ = true;
    return true;
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

void LoginClient::SendChat(const std::string& text)
{
    if (!inGame_ || text.empty() || text.size() > 200 ||
        text.find_first_of("\r\n") != std::string::npos || text.find('\0') != std::string::npos)
        throw std::invalid_argument("聊天内容必须为 1 至 200 字节的单行文字");
    std::string message = "P|";
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (static_cast<unsigned char>(text[i]) >= 128 && i + 1 < text.size())
        {
            message += text[i];
            message += text[++i];
        }
        else
        {
            switch (text[i])
            {
            case '\\': message += "\\y"; break;
            case '|': message += "\\z"; break;
            case ',': message += "\\c"; break;
            default: message += text[i]; break;
            }
        }
    }
    int x = playerStatus_.mapX, y = playerStatus_.mapY;
    std::uint32_t checksum = static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(y) +
        static_cast<std::uint32_t>(message.size());
    connection_.Send(EncodePacket("&;35;" + EncodeInteger(x, sessionKey_) + ";" +
        EncodeInteger(y, sessionKey_) + ";" + Encode64(message, sessionKey_, -1) + ";" +
        EncodeInteger(0, sessionKey_) + ";" + EncodeInteger(0, sessionKey_) + ";" +
        EncodeInteger(checksum, sessionKey_) + ";#;"), GetTickCount64() + 10000);
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

namespace
{
    std::vector<int> ShopNumbers(const std::vector<std::string>& fields, int count,
        int strings, const std::string& key, std::vector<std::string>& texts)
    {
        if (fields.size() != static_cast<std::size_t>(count + strings + 4))
            throw std::runtime_error("Invalid shop reply");
        std::uint32_t checksum = 0;
        std::vector<int> numbers;
        for (int i = 0; i < count; ++i)
        {
            auto value = ReadInteger(Decode64(fields[2 + i], key, -1), { 2, 4, 1, 3 });
            checksum += value;
            numbers.push_back(static_cast<int>(value));
        }
        for (int i = 0; i < strings; ++i)
        {
            auto value = Decode64(fields[2 + count + i], key, 1);
            auto end = value.find('\0');
            if (end != std::string::npos) value.resize(end);
            checksum += static_cast<std::uint32_t>(value.size());
            texts.push_back(value);
        }
        if (checksum != ReadInteger(Decode64(fields[fields.size() - 2], key, -1), { 2, 4, 1, 3 }))
            throw std::runtime_error("Invalid shop checksum");
        return numbers;
    }

    std::string ShopError(int result)
    {
        switch (result)
        {
        case 2: return "购买参数无效";
        case 3: return "请结束战斗或交易后再操作黑市";
        case 4: return "黑市数据库操作失败，请刷新并确认余额和库存";
        case 5: return "商品已下架或不支持所选货币";
        case 6: return "商品价格已变化，请刷新后重新购买";
        case 7: return "COST不足";
        case 8: return "背包空位不足";
        case 9: return "商品生成失败，购买已取消";
        case 10: return "P 余额不足";
        case 11: return "商品库存不足";
        default: return "黑市返回未知状态";
        }
    }
}

void LoginClient::RefreshShop(int category)
{
    if (!inGame_ || category < 0 || category > 2 || playerStatus_.inBattle || IsEncounterEnabled())
        throw std::runtime_error("请先停止遇敌并结束战斗，再刷新黑市");
    if (shopUncertain_)
        throw std::runtime_error("上次黑市请求未确认，请重新登录后检查余额与背包");
    shopItems_.clear();
    shopCategory_ = -1;
    int offset = 0, expectedTotal = -1;
    bool awaiting = false;
    std::vector<Game::ShopItem> items;
    try
    {
        for (;;)
        {
            auto deadline = GetTickCount64() + 10000;
            awaiting = true;
            SendNumbers("122", { category, offset }, deadline);
            int pageStart = offset;
            for (;;)
            {
                auto fields = DecodePacket(connection_.Receive(deadline));
                if (fields[1] != "123") { HandleGamePacket(fields); continue; }
                std::vector<std::string> texts;
                auto n = ShopNumbers(fields, 12, 2, sessionKey_, texts);
                pearlBalance_ = n[10];
                if (n[1] != category || n[2] != offset || n[3] < offset)
                    throw std::runtime_error("黑市列表已变化，请重新刷新");
                if (n[0] != 0 && n[0] != 1) {
                    awaiting = false;
                    throw std::runtime_error(ShopError(n[0]));
                }
                if (expectedTotal < 0) expectedTotal = n[3];
                if (expectedTotal != n[3] || expectedTotal > 100000)
                    throw std::runtime_error("黑市列表已变化或记录过多，请重新刷新");
                if (n[0] == 1) { awaiting = false; break; }
                if (n[4] < 0 || n[5] < -1 || n[6] < -1 || n[7] < -1 || offset >= expectedTotal)
                    throw std::runtime_error("Invalid shop item");
                Game::ShopItem item;
                item.id = n[4]; item.cost = n[5]; item.pearl = n[6]; item.stock = n[7];
                item.image = n[8]; item.level = n[9]; item.type = n[11];
                item.name = texts[0]; item.description = texts[1];
                items.push_back(item);
                ++offset;
            }
            if (offset == expectedTotal) break;
            if (offset == pageStart) throw std::runtime_error("Invalid shop pagination");
        }
        shopItems_ = std::move(items);
        shopCategory_ = category;
    }
    catch (...)
    {
        shopUncertain_ = awaiting;
        throw;
    }
}

std::string LoginClient::BuyShopItem(int itemId, int quantity, int price, int currency)
{
    if (!inGame_ || playerStatus_.inBattle || IsEncounterEnabled() || IsEncounterStopping() ||
        shopUncertain_ || quantity < 1 || quantity > 15 || price < 0 || currency < 0 || currency > 1)
        throw std::runtime_error("当前状态无法购买，请停止遇敌并刷新黑市");
    auto deadline = GetTickCount64() + 10000;
    shopUncertain_ = true;
    SendNumbers("124", { itemId, quantity, price, currency }, deadline);
    for (;;)
    {
        auto fields = DecodePacket(connection_.Receive(deadline));
        if (fields[1] != "125") { HandleGamePacket(fields); continue; }
        std::vector<std::string> texts;
        auto n = ShopNumbers(fields, 7, 0, sessionKey_, texts);
        if (n[1] != itemId || n[2] != quantity || n[4] != currency)
            throw std::runtime_error("Invalid shop purchase reply");
        shopUncertain_ = false;
        playerStatus_.gold = n[5];
        pearlBalance_ = n[6];
        return n[0] == 0 ? "购买成功" : ShopError(n[0]);
    }
}
