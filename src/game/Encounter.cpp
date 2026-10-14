#include "../LoginClient.h"
#include "../protocol/ProtocolCodec.h"
#include "BattleTiming.h"
#include <stdexcept>

using namespace Protocol;

void LoginClient::SendEncounterWalk(const std::string& directions, int x, int y)
{
    std::uint32_t checksum = static_cast<std::uint32_t>(x) + static_cast<std::uint32_t>(y) +
        static_cast<std::uint32_t>(directions.size());
    connection_.Send(EncodePacket("&;1;" + EncodeInteger(x, sessionKey_) + ";" +
        EncodeInteger(y, sessionKey_) + ";" + Encode64(directions, sessionKey_, -1) + ";" +
        EncodeInteger(checksum, sessionKey_) + ";#;"), GetTickCount64() + Game::BATTLE_DELAY_MS);
}

void LoginClient::SetEncounterEnabled(bool enabled)
{
    if (encounter_.enabled == enabled && !encounter_.stopPending)
        return;
    if (!enabled)
    {
        encounter_.enabled = false;
        encounter_.stopPending = true;
    }
    if (!inGame_)
        throw std::logic_error("尚未进入游戏，无法设置遇敌");
    RefreshPosition(GetTickCount64() + 2000);
    if (playerStatus_.mapId < 0 || playerStatus_.mapX < 0 || playerStatus_.mapY < 0)
        throw std::runtime_error("尚未收到有效地图位置，无法设置遇敌");
    if (enabled)
    {
        encounter_.map = playerStatus_.mapId;
        encounter_.x = playerStatus_.mapX;
        encounter_.y = playerStatus_.mapY;
        encounter_.nextRequest = 0;
        encounter_.enabled = true;
        encounter_.stopPending = false;
    }
    else if (!playerStatus_.inBattle)
    {
        SendEncounterWalk("", playerStatus_.mapX, playerStatus_.mapY);
        encounter_.stopPending = false;
    }
}

void LoginClient::PollEncounter()
{
    if (encounter_.stopPending)
    {
        if (!playerStatus_.inBattle)
            SetEncounterEnabled(false);
        return;
    }
    if (!encounter_.enabled || playerStatus_.inBattle || GetTickCount64() < encounter_.nextRequest)
        return;
    if (playerStatus_.mapId != encounter_.map)
    {
        SetEncounterEnabled(false);
        throw std::runtime_error("地图已改变，快速遇敌已停止");
    }
    // STW repeats the same W2 origin with ten pairs of opposite directions.
    SendEncounterWalk("gcgcgcgcgcgcgcgcgcgc", encounter_.x, encounter_.y);
    encounter_.nextRequest = GetTickCount64() + Game::BATTLE_DELAY_MS;
}
