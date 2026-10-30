#include "../LoginClient.h"
#include "../protocol/ProtocolCodec.h"
#include "BattleTiming.h"
#include <stdexcept>
#include <algorithm>

using namespace Protocol;

bool LoginClient::HandleEncounterReply(const std::vector<std::string>& fields)
{
    if (fields.size() != 5)
        throw std::runtime_error("Invalid encounter reply");
    auto result = ReadInteger(Decode64(fields[2], sessionKey_, -1), { 2, 4, 1, 3 });
    auto checksum = ReadInteger(Decode64(fields[3], sessionKey_, -1), { 2, 4, 1, 3 });
    if (result != checksum || result > 10)
        throw std::runtime_error("Invalid encounter result or checksum");
    if (encounter_.requestPending)
        encounter_.requestResult = static_cast<int>(result);
    return true;
}

void LoginClient::SetEncounterEnabled(bool enabled, bool tower)
{
    if (encounter_.enabled == enabled && !encounter_.stopPending)
        return;
    if (!enabled)
    {
        encounter_.enabled = false;
        encounter_.stopPending = encounter_.requestPending || playerStatus_.inBattle;
        return;
    }
    if (!inGame_)
        throw std::logic_error("尚未进入游戏，无法设置遇敌");
    if (encounter_.requestPending)
        throw std::runtime_error("请等待上一笔原地遇敌请求完成");
    RefreshPosition(GetTickCount64() + 2000);
    if (playerStatus_.mapId < 0 || playerStatus_.mapX < 0 || playerStatus_.mapY < 0)
        throw std::runtime_error("尚未收到有效地图位置，无法设置遇敌");
    if (tower && captureEnabled_)
        throw std::runtime_error("进阶模式不能同时抓宠");
    if (captureEnabled_)
    {
        if (capture_.sourceInstanceId < 0)
            throw std::runtime_error("请先选择要捕捉的一级宠物");
        if (std::any_of(pets_.begin(), pets_.end(), [](const PetStatus& pet) { return !pet.received; }))
            throw std::runtime_error("请等待宠物栏数据加载完成");
        if (IsCaptureFull())
            throw std::runtime_error("携带宠物已满，抓宠已停止");
        captureStatistics_ = {};
    }
    encounter_.tower = tower;
    encounter_.map = playerStatus_.mapId;
    encounter_.nextRequest = 0;
    encounter_.enabled = true;
    encounter_.stopPending = false;
}

void LoginClient::PollEncounter()
{
    const auto now = GetTickCount64();
    if (encounter_.requestPending)
    {
        if (encounter_.requestResult > 0)
        {
            const int result = encounter_.requestResult;
            encounter_.requestPending = false;
            encounter_.requestResult = -1;
            const char* errors[] = {
                "", "服务器仍在战斗中，原地遇敌已停止", "交易中无法原地遇敌",
                "队员不能主动原地遇敌，请由队长发起", "当前位置没有可遇敌的怪物",
                "服务器创建战斗失败，原地遇敌已停止", "角色 HP 为 0，无法原地遇敌",
                "99阶特别 BOSS 暂未开放，进阶已停止", "进阶怪物基板不足10个，无法创建阶层怪物池",
                "角色闯关进度无效，请检查数据库中的进度编码", "闯关进度保存失败，进阶已停止，请重新登录"
            };
            if (result == 3 && encounter_.tower)
                throw std::runtime_error("进阶暂仅支持单人携宠，请退出队伍后再挑战");
            throw std::runtime_error(errors[result]);
        }
        if (encounter_.requestResult == 0 && encounter_.battleStarted)
        {
            encounter_.requestPending = false;
            encounter_.requestResult = -1;
        }
        else if (now >= encounter_.requestDeadline)
        {
            Disconnect();
            throw std::runtime_error("服务器未确认原地开战，连接已关闭，请确认服务端已更新原地遇敌协议");
        }
        else
            return;
    }
    if (PollCaptureNames())
        return;
    if (encounter_.stopPending)
    {
        if (!playerStatus_.inBattle)
            encounter_.stopPending = false;
        return;
    }
    if (!encounter_.enabled || playerStatus_.inBattle || now < encounter_.nextRequest)
        return;
    if (playerStatus_.mapId != encounter_.map)
    {
        SetEncounterEnabled(false);
        throw std::runtime_error("地图已改变，原地遇敌已停止");
    }
    if (captureEnabled_ && IsCaptureFull())
    {
        SetEncounterEnabled(false);
        throw std::runtime_error("携带宠物已满，抓宠已停止");
    }
    SendNumbers("118", { encounter_.tower ? 1 : 0 }, now + 2000);
    encounter_.requestPending = true;
    encounter_.requestResult = -1;
    encounter_.battleStarted = false;
    encounter_.requestDeadline = GetTickCount64() + 10000;
}
