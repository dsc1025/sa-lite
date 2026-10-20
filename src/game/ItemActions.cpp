#include "../LoginClient.h"
#include "../protocol/ProtocolCodec.h"
#include "../protocol/LegacyFields.h"
#include <stdexcept>
#include <algorithm>

using namespace Protocol;

std::string LoginClient::DropItem(int slot, std::uint32_t revision)
{
    if (!inGame_ || !playerStatus_.received || playerStatus_.inBattle ||
        IsEncounterEnabled() || IsEncounterStopping())
        throw std::runtime_error("请先停止遇敌并结束战斗，再丢弃道具");
    if (slot < 0 || slot >= static_cast<int>(items_.size()) || !items_[slot].present ||
        items_[slot].revision != revision)
        throw std::runtime_error("道具数据已变化，请重新选择");
    auto deadline = GetTickCount64() + 2000;
    SendNumbers("19", { playerStatus_.mapX, playerStatus_.mapY, slot }, deadline);
    while (items_[slot].present && items_[slot].revision == revision && GetTickCount64() < deadline)
        PollGame(100);
    if (!items_[slot].present)
        return "道具已丢弃";
    return "服务器未确认道具丢弃，请检查道具及当前场景是否允许丢弃，勿连续重复操作";
}

std::string LoginClient::EquipItem(int slot, std::uint32_t revision, int equipmentSlot)
{
    if (!inGame_ || !itemsReceived_ || !playerStatus_.received || playerStatus_.inBattle ||
        IsEncounterEnabled() || IsEncounterStopping())
        throw std::runtime_error("请先停止遇敌并结束战斗，再穿脱装备");
    if (slot < 0 || slot >= static_cast<int>(items_.size()) || !items_[slot].present ||
        items_[slot].revision != revision || !Game::IsEquipmentType(items_[slot].type))
        throw std::runtime_error("装备数据已变化，请重新选择");
    int destination = -1;
    if (slot < 5)
    {
        for (int i = 5; i < static_cast<int>(items_.size()); ++i)
            if (!items_[i].present) { destination = i; break; }
        if (destination < 0)
            throw std::runtime_error("背包已满，请先空出栏位再卸下装备");
    }
    else
    {
        destination = Game::EquipmentSlot(items_[slot].type);
        if (destination == 3)
        {
            if (equipmentSlot != 3 && equipmentSlot != 4)
                throw std::runtime_error("请选择饰品栏位");
            destination = equipmentSlot;
        }
        else if (equipmentSlot != -1 && equipmentSlot != destination)
            throw std::runtime_error("装备栏位无效");
    }
    auto serial = itemMoveSerial_;
    auto deadline = GetTickCount64() + 2000;
    SendNumbers("23", { slot, destination }, deadline);
    while (itemMoveSerial_ == serial && GetTickCount64() < deadline)
        PollGame(100);
    if (itemMoveSerial_ == serial)
        return "服务器未确认穿脱装备，请检查背包与装备栏，勿连续重复操作";
    if (itemMoveFrom_ != slot || itemMoveTo_ != destination)
        return "服务器拒绝穿脱装备，请检查装备条件及服务器提示";
    return slot < 5 ? "装备已卸下" : "装备已穿上";
}

std::string LoginClient::UseItem(int slot, std::uint32_t revision, int petSlot)
{
    if (!inGame_ || !playerStatus_.received || playerStatus_.inBattle ||
        IsEncounterEnabled() || IsEncounterStopping())
        throw std::runtime_error("请先停止遇敌并结束战斗，再使用道具");
    if (slot < 5 || slot >= static_cast<int>(items_.size()) || !items_[slot].present ||
        (items_[slot].type != 20 && items_[slot].type != 16) ||
        items_[slot].revision != revision)
        throw std::runtime_error("背包道具数据已变化，请重新选择道具");
    const auto& item = items_[slot];
    if (item.useField == 1 || item.target < 0 || item.target > 2)
        throw std::runtime_error("该道具不能在当前场景使用");
    if ((item.type == 16 && item.target == 0 && petSlot != -1) || petSlot < -1 || petSlot >= static_cast<int>(pets_.size()) ||
        (petSlot >= 0 && (!pets_[petSlot].received || !pets_[petSlot].present)))
        throw std::runtime_error("宠物数据已变化，请重新选择使用目标");
    waitingFood_ = true;
    foodMessage_.clear();
    try
    {
        auto deadline = GetTickCount64() + 2000;
        SendNumbers("17", { playerStatus_.mapX, playerStatus_.mapY, slot, petSlot + 1 }, deadline);
        while (items_[slot].revision == revision && foodMessage_.empty() && GetTickCount64() < deadline)
            PollGame(100);
        waitingFood_ = false;
        if (!foodMessage_.empty()) return foodMessage_;
        if (items_[slot].revision != revision)
            return "道具使用请求已处理，背包数据已更新";
        return "服务器未确认道具使用，请检查使用条件和背包，勿连续重复使用";
    }
    catch (...)
    {
        waitingFood_ = false;
        throw;
    }
}

namespace
{
    bool ValidItemCode(const std::string& code)
    {
        return !code.empty() && code.size() <= 128 && std::none_of(code.begin(), code.end(),
            [](unsigned char value) { return value <= 32 || value >= 127 || value == '|'; });
    }

    std::string ItemStorageError(const std::string& reason, bool put)
    {
        if (reason == "BUSY") return "战斗或交易中无法操作道具仓库";
        if (reason == "FULL") return put ? "道具仓库已满" : "背包已满，请先空出背包栏位";
        if (reason == "STALE") return "道具数据已变化，请刷新后再操作";
        if (reason == "SLOT") return "道具栏位无效，身上装备不能直接存入仓库";
        if (reason == "FORBIDDEN") return "该道具不能存入仓库";
        if (reason == "DATA") return "无法读取道具数据或唯一编码";
        return "服务器拒绝道具仓库请求：" + reason;
    }

    Game::ItemStatus ParseStoredItem(const std::vector<std::string>& values)
    {
        Game::ItemStatus item;
        std::string name;
        for (std::size_t i = 4; i < values.size(); ++i)
        {
            auto delimiter = values[i].find('=');
            if (delimiter == std::string::npos) continue;
            auto key = values[i].substr(0, delimiter);
            auto value = values[i].substr(delimiter + 1);
            if (key == "na") name = UnescapeLegacy(value);
            else if (key == "sn") item.name = UnescapeLegacy(value);
            else if (key == "en") item.description = UnescapeLegacy(value);
            else if (key == "ucode") item.uniqueCode = UnescapeLegacy(value);
            else if (key == "ep") item.type = ParseNumber(value);
            else if (key == "ft") item.useField = ParseNumber(value);
            else if (key == "tg") item.target = ParseNumber(value);
            else if (key == "lv")
            {
                item.level = ParseNumber(value);
                item.levelKnown = true;
            }
        }
        if (item.name.empty()) item.name = name;
        if (item.name.empty() || !ValidItemCode(item.uniqueCode))
            throw std::runtime_error("仓库道具数据缺少名称或唯一编码");
        item.present = true;
        return item;
    }
}

std::string LoginClient::ItemStorageRequest(const std::string& action, int slot,
    const std::string& uniqueCode, std::uint32_t revision)
{
    auto deadline = GetTickCount64() + 10000;
    std::string request = "WH|" + action + "|I";
    if (action != "LIST") request += "|" + std::to_string(slot);
    if (action == "PUT" || action == "GET") request += "|" + uniqueCode;
    try
    {
        connection_.Send(EncodePacket("&;45;" + Encode64(request, sessionKey_, -1) + ";" +
            EncodeInteger(static_cast<std::uint32_t>(request.size()), sessionKey_) + ";#;"), deadline);
        std::vector<ItemStatus> updated;
        bool began = false;
        for (;;)
        {
            if (GetTickCount64() >= deadline)
                throw std::runtime_error("道具仓库请求超时，请重新登录确认存取结果");
            auto fields = DecodePacket(connection_.Receive(deadline));
            if (fields[1] != "46")
            {
                HandleGamePacket(fields);
                continue;
            }
            auto strings = ReceiveStrings(fields);
            if (strings.size() != 1)
                throw std::runtime_error("Invalid item warehouse reply");
            if (strings[0].compare(0, 3, "WH|") != 0)
            {
                HandleGamePacket(fields);
                continue;
            }
            auto values = SplitLegacy(strings[0]);
            if (values.size() == 4 && values[1] == "RESULT" && values[2] == "0")
                return values[3];
            if (action == "LIST")
            {
                if (values.size() == 4 && values[1] == "BEGIN" && values[2] == "I" && !began)
                {
                    int capacity = ParseNumber(values[3]);
                    if (capacity < 10 || capacity > 30)
                        throw std::runtime_error("Invalid item warehouse capacity");
                    updated.resize(capacity);
                    began = true;
                    continue;
                }
                if (began && values.size() >= 5 && values[1] == "ENTRY" && values[2] == "I")
                {
                    int entry = ParseNumber(values[3]);
                    if (entry < 0 || entry >= static_cast<int>(updated.size()) || updated[entry].present)
                        throw std::runtime_error("Invalid item warehouse slot");
                    updated[entry] = ParseStoredItem(values);
                    continue;
                }
                if (began && values.size() == 3 && values[1] == "END" && values[2] == "I")
                {
                    itemStorage_ = updated;
                    itemStorageReceived_ = true;
                    return {};
                }
            }
            else if (action == "CODE" && values.size() == 5 && values[1] == "CODE" &&
                values[2] == "I" && ParseNumber(values[3]) == slot && ValidItemCode(values[4]))
            {
                if (!items_[slot].present || items_[slot].revision != revision)
                    return "STALE";
                items_[slot].uniqueCode = values[4];
                return {};
            }
            else if (values.size() == 8 && values[1] == "RESULT" && values[2] == "1" &&
                values[3] == action && values[4] == "I" && ParseNumber(values[5]) == slot && values[7] == uniqueCode)
            {
                int target = ParseNumber(values[6]);
                int capacity = action == "PUT" ? static_cast<int>(itemStorage_.size()) : static_cast<int>(items_.size());
                if (target < (action == "PUT" ? 0 : 5) || target >= capacity)
                    throw std::runtime_error("Invalid item warehouse destination");
                return {};
            }
            throw std::runtime_error("Unexpected item warehouse reply");
        }
    }
    catch (...)
    {
        bool timedOut = GetTickCount64() >= deadline;
        Disconnect();
        if (timedOut)
            throw std::runtime_error("道具仓库请求超时，连接已关闭，请重新登录确认存取结果");
        throw;
    }
}

void LoginClient::RefreshItemStorage()
{
    if (!inGame_ || playerStatus_.inBattle || IsEncounterEnabled() || IsEncounterStopping())
        throw std::runtime_error("请先停止遇敌并结束战斗，再刷新道具仓库");
    itemStorageReceived_ = false;
    auto error = ItemStorageRequest("LIST", -1, {});
    if (!error.empty()) throw std::runtime_error(ItemStorageError(error, false));
}

void LoginClient::TransferItemStorage(bool put, int slot, std::uint32_t revision, const std::string& uniqueCode)
{
    if (!inGame_ || !itemsReceived_ || !itemStorageReceived_)
        throw std::runtime_error("请先打开道具页加载仓库");
    if (playerStatus_.inBattle || IsEncounterEnabled() || IsEncounterStopping())
        throw std::runtime_error("请先停止遇敌并结束战斗，再存取道具");
    if (put)
    {
        if (slot < 5 || slot >= static_cast<int>(items_.size()) || !items_[slot].present ||
            items_[slot].revision != revision)
            throw std::runtime_error("道具数据已变化，只有背包道具可以存入");
        if (std::none_of(itemStorage_.begin(), itemStorage_.end(), [](const ItemStatus& item) { return !item.present; }))
            throw std::runtime_error("道具仓库已满");
        auto error = ItemStorageRequest("CODE", slot, {}, revision);
        if (!error.empty()) throw std::runtime_error(ItemStorageError(error, true));
    }
    else
    {
        if (slot < 0 || slot >= static_cast<int>(itemStorage_.size()) || !itemStorage_[slot].present ||
            itemStorage_[slot].uniqueCode != uniqueCode || !ValidItemCode(uniqueCode))
            throw std::runtime_error("仓库道具数据已变化，请刷新后再操作");
        if (std::none_of(items_.begin() + 5, items_.end(), [](const ItemStatus& item) { return !item.present; }))
            throw std::runtime_error("背包已满，请先空出背包栏位");
    }
    auto code = put ? items_[slot].uniqueCode : uniqueCode;
    auto error = ItemStorageRequest(put ? "PUT" : "GET", slot, code);
    if (!error.empty())
    {
        if (error == "STALE" || error == "SLOT") itemStorageReceived_ = false;
        throw std::runtime_error(ItemStorageError(error, put));
    }
    try { RefreshItemStorage(); }
    catch (const std::exception& error)
    {
        throw std::runtime_error(std::string(put ? "道具已存入，但仓库刷新失败：" : "道具已取出，但仓库刷新失败：") + error.what());
    }
}
