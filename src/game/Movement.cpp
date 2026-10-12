#include "../LoginClient.h"
#include "../protocol/ProtocolCodec.h"
#include "../protocol/LegacyFields.h"
#include <stdexcept>

using namespace Protocol;

void LoginClient::RefreshPosition(ULONGLONG deadline)
{
    auto serial = positionSerial_;
    connection_.Send(EncodePacket("&;45;" + Encode64("C", sessionKey_, -1) + ";" +
        EncodeInteger(1, sessionKey_) + ";#;"), deadline);
    while (positionSerial_ == serial)
    {
        if (GetTickCount64() >= deadline)
            throw std::runtime_error("服务器位置确认超时，移动已停止");
        HandleGamePacket(DecodePacket(connection_.Receive(deadline)));
    }
}

bool LoginClient::MoveTo(int map, int x, int y, int timeoutMs)
{
    if (!inGame_ || map < 0 || x < 0 || y < 0 || timeoutMs <= 0)
        throw std::logic_error("尚未收到有效的地图位置");
    if (playerStatus_.inBattle)
        throw std::runtime_error("战斗中无法移动");
    auto deadline = GetTickCount64() + timeoutMs;
    RefreshPosition(deadline);
    if (playerStatus_.inBattle)
        throw std::runtime_error("战斗中无法移动");
    SendNumbers("116", { map, x, y }, deadline);
    for (;;)
    {
        if (GetTickCount64() >= deadline)
            throw std::runtime_error("大瞬移回复超时，移动已停止");
        auto reply = DecodePacket(connection_.Receive(deadline));
        if (reply[1] != "117")
        {
            HandleGamePacket(reply);
            continue;
        }
        if (reply.size() != 5)
            throw std::runtime_error("Invalid teleport reply");
        auto result = ReadInteger(Decode64(reply[2], sessionKey_, -1), { 2, 4, 1, 3 });
        auto checksum = ReadInteger(Decode64(reply[3], sessionKey_, -1), { 2, 4, 1, 3 });
        if (checksum != result)
            throw std::runtime_error("Invalid teleport checksum");
        switch (result)
        {
        case 0: break;
        case 1: throw std::runtime_error("战斗中无法大瞬移");
        case 2: throw std::runtime_error("交易中无法大瞬移");
        case 3: throw std::runtime_error("组队中无法大瞬移");
        case 4: throw std::runtime_error("大瞬移目标地图或坐标无效");
        case 5: throw std::runtime_error("服务器执行大瞬移失败");
        default: throw std::runtime_error("Invalid teleport result");
        }
        break;
    }
    RefreshPosition(deadline);
    if (playerStatus_.mapId != map)
        return false;
    if (playerStatus_.mapX != x || playerStatus_.mapY != y)
        return false;
    if (playerStatus_.inBattle)
        throw std::runtime_error("战斗中无法触发地图事件");
    SendNumbers("38", { map, x, y, x + 1, y + 1 }, deadline);
    for (;;)
    {
        if (GetTickCount64() >= deadline)
            throw std::runtime_error("地图事件确认超时，移动已停止");
        auto fields = DecodePacket(connection_.Receive(deadline));
        if (fields[1] != "39")
        {
            HandleGamePacket(fields);
            if (playerStatus_.mapId != map)
                return true;
            continue;
        }
        if (fields.size() != 10)
            throw std::runtime_error("Invalid map event reply");
        std::uint32_t bounds[5]{}, sum = 0;
        for (int i = 0; i < 5; ++i)
        {
            bounds[i] = ReadInteger(Decode64(fields[2 + i], sessionKey_, -1), { 2, 4, 1, 3 });
            sum += bounds[i];
        }
        auto data = Decode64(fields[7], sessionKey_, 1);
        sum += static_cast<std::uint32_t>(data.size());
        if (sum != ReadInteger(Decode64(fields[8], sessionKey_, -1), { 2, 4, 1, 3 }))
            throw std::runtime_error("Invalid map event checksum");
        if (bounds[0] != static_cast<std::uint32_t>(map) || bounds[1] != static_cast<std::uint32_t>(x) ||
            bounds[2] != static_cast<std::uint32_t>(y) || bounds[3] != static_cast<std::uint32_t>(x + 1) ||
            bounds[4] != static_cast<std::uint32_t>(y + 1))
            continue;
        auto groups = SplitLegacy(data);
        if (groups.size() != 4)
            throw std::runtime_error("Invalid map event data");
        int event = groups[3].empty() ? 0 : static_cast<int>(ParseMask(groups[3]));
        if (event == 3 || event == 6 || event == 7 || event == 8)
        {
            // The server selects the destination for a warp event at the confirmed position.
            eventSequence_ = (eventSequence_ + 1) % 65536;
            SendNumbers("3", { event, eventSequence_, x, y, -1 }, deadline);
            for (;;)
            {
                if (GetTickCount64() >= deadline)
                    throw std::runtime_error("传送事件确认超时，移动已停止");
                auto reply = DecodePacket(connection_.Receive(deadline));
                if (reply[1] != "4")
                {
                    HandleGamePacket(reply);
                    continue;
                }
                if (reply.size() != 6)
                    throw std::runtime_error("Invalid warp event reply");
                auto sequence = ReadInteger(Decode64(reply[2], sessionKey_, -1), { 2, 4, 1, 3 });
                auto result = ReadInteger(Decode64(reply[3], sessionKey_, -1), { 2, 4, 1, 3 });
                auto replyChecksum = ReadInteger(Decode64(reply[4], sessionKey_, -1), { 2, 4, 1, 3 });
                if (replyChecksum != sequence + result)
                    throw std::runtime_error("Invalid warp event checksum");
                if (sequence == static_cast<std::uint32_t>(eventSequence_))
                    break;
            }
            RefreshPosition(deadline);
        }
        return true;
    }
}

