#include "../LoginClient.h"
#include "../protocol/ProtocolCodec.h"
#include "../protocol/LegacyFields.h"
#include "../text/TextEncoding.h"
#include <algorithm>
#include <climits>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>
#include <map>

using namespace Protocol;

namespace
{
    std::int64_t AuctionNumber(const std::string& text, std::int64_t maximum = (std::numeric_limits<int>::max)())
    {
        std::int64_t result = 0;
        if (text.empty()) throw std::runtime_error("竞拍响应缺少数字");
        for (unsigned char c : text)
        {
            if (c < '0' || c > '9' || result > maximum / 10 ||
                (result == maximum / 10 && c - '0' > maximum % 10))
                throw std::runtime_error("竞拍响应数字无效");
            result = result * 10 + c - '0';
        }
        return result;
    }

    std::string AuctionHex(const std::string& text)
    {
        const char* digits = "0123456789abcdef";
        std::string result;
        for (unsigned char c : text) { result += digits[c >> 4]; result += digits[c & 15]; }
        return result;
    }

    std::string AuctionUnhex(const std::string& text, std::size_t maximum)
    {
        if (text.size() % 2 || text.size() / 2 > maximum) throw std::runtime_error("竞拍文本长度无效");
        auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            throw std::runtime_error("竞拍文本编码无效");
        };
        std::string result;
        for (std::size_t i = 0; i < text.size(); i += 2)
        {
            char c = static_cast<char>(digit(text[i]) * 16 + digit(text[i + 1]));
            if (!c) throw std::runtime_error("竞拍文本含有空字符");
            result += c;
        }
        return result;
    }

    std::string AuctionKeyword(const std::wstring& keyword)
    {
        if (keyword.empty()) return {};
        if (keyword.size() > 64 || keyword.find(L'\0') != std::wstring::npos)
            throw std::runtime_error("搜索名称过长或含无效字符");
        BOOL replaced = FALSE;
        int size = WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS, keyword.data(),
            static_cast<int>(keyword.size()), nullptr, 0, nullptr, &replaced);
        if (size <= 0 || size > 128 || replaced) throw std::runtime_error("搜索名称无法转换为游戏编码");
        std::string result(size, '\0');
        if (!WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS, keyword.data(), static_cast<int>(keyword.size()),
            &result[0], size, nullptr, &replaced) || replaced)
            throw std::runtime_error("搜索名称无法转换为游戏编码");
        return AuctionHex(result);
    }

    std::string AuctionId()
    {
        std::random_device random;
        std::string bytes;
        for (int i = 0; i < 4; ++i)
        {
            auto value = random();
            for (int j = 0; j < 4; ++j) bytes += static_cast<char>(value >> (j * 8));
        }
        return AuctionHex(bytes);
    }

    std::wstring AuctionError(const std::string& code)
    {
        if (code == "DISABLED") return L"竞拍已暂停新上架和出价，已有订单仍可撤拍和领取";
        if (code == "BUSY") return L"请结束战斗、交易并退出队伍后再操作竞拍";
        if (code == "LIMIT") return L"已达到当前角色的上架数量上限";
        if (code == "BALANCE") return L"P 余额不足或余额数据无效";
        if (code == "BALANCE_LIMIT") return L"领取后 P 将超过余额上限，请先使用部分 P";
        if (code == "FULL") return L"携带栏位已满，请先空出宠物或道具栏位";
        if (code == "FORBIDDEN") return L"该资产不能交易，或当前订单不允许撤拍";
        if (code == "OWN_AUCTION") return L"不能竞拍同一账号上架的资产";
        if (code == "PRICE_CHANGED") return L"价格已变化，请刷新列表后重新出价";
        if (code == "CLOSED") return L"拍卖已结束或到期，请刷新列表";
        if (code == "CLAIMED") return L"该记录已领取，请刷新待领取列表";
        if (code == "STALE_ASSET" || code == "ASSET") return L"资产已变化，请重新选择后上架";
        if (code == "UNAVAILABLE") return L"当前服务器不支持竞拍";
        if (code == "NOT_FOUND") return L"拍卖记录不存在，请刷新列表";
        if (code == "DETAIL_UNAVAILABLE") return L"暂时无法读取属性详情，请稍后重试";
        return L"竞拍操作未成功：" + TextEncoding::LegacyText(code);
    }
}

std::string LoginClient::AuctionExchange(const std::string& command, const std::string& requestId,
    const Game::AuctionRequest& request)
{
    auto deadline = GetTickCount64() + 15000;
    const std::string wire = "AU|" + command;
    try
    {
        connection_.Send(EncodePacket("&;45;" + Encode64(wire, sessionKey_, -1) + ";" +
            EncodeInteger(static_cast<std::uint32_t>(wire.size()), sessionKey_) + ";#;"), deadline);
        Game::AuctionView updated = auction_;
        updated.entries.clear();
        bool began = false;
        for (;;)
        {
            if (GetTickCount64() >= deadline) throw std::runtime_error("竞拍响应超时");
            auto fields = DecodePacket(connection_.Receive(deadline));
            if (fields[1] != "46") { HandleGamePacket(fields); continue; }
            auto strings = ReceiveStrings(fields);
            if (strings.size() != 1) throw std::runtime_error("竞拍响应格式无效");
            if (strings[0].compare(0, 3, "AU|") != 0) { HandleGamePacket(fields); continue; }
            auto values = SplitLegacy(strings[0]);
            if (values.size() < 3) throw std::runtime_error("竞拍响应不完整");
            if (values[1] == "REPLAY") throw std::runtime_error("此竞拍请求已处理，请重新登录确认结果");
            if (values[1] == "RESULT")
            {
                if (values.size() < 4 || (values[2] != requestId && values[2] != "0"))
                    throw std::runtime_error("竞拍响应请求编号不匹配");
                const auto& code = values[3];
                if (code == "UNKNOWN" || code == "STALE_CHARACTER" || code == "SESSION" || code == "RELOGIN")
                    throw std::runtime_error("竞拍结果需要重新确认，请重新登录查看订单和待领取");
                if (code != "OK")
                {
                    if (values.size() != 4) throw std::runtime_error("竞拍错误响应无效");
                    return code;
                }
                if (values.size() != 10 || values[2] != requestId || requestId == "0")
                    throw std::runtime_error("竞拍交易响应无效");
                AuctionNumber(values[4], (std::numeric_limits<std::int64_t>::max)());
                pearlBalance_ = static_cast<int>(AuctionNumber(values[5]));
                auction_.balance = pearlBalance_;
                return {};
            }
            if (request.action == Game::AuctionAction::Detail && values[1] == "DETAIL" && values.size() == 4)
            {
                if (values[2] != request.id) throw std::runtime_error("竞拍详情编号不匹配");
                std::map<std::string, std::string> properties;
                for (const auto& field : SplitLegacy(AuctionUnhex(values[3], 8192)))
                {
                    auto at = field.find('=');
                    if (at == std::string::npos) continue;
                    if (!properties.emplace(field.substr(0, at), field.substr(at + 1)).second)
                        throw std::runtime_error("竞拍属性重复");
                }
                auction_.detailType.clear();
                if (properties["view"] != "1")
                    auction_.detail = L"当前服务器尚未提供面板属性详情，请更新服务器后查看。";
                else if (properties["type"] == "pet")
                {
                    Game::PetStatus pet;
                    auto number = [&](const char* key) { return ParseNumber(properties.at(key)); };
                    pet.name = AuctionUnhex(properties.at("name"), 256);
                    pet.nickname = AuctionUnhex(properties.at("nickname"), 256);
                    pet.image = number("image"); pet.hp = number("hp"); pet.maxHp = number("maxhp");
                    pet.level = number("level"); pet.attack = number("attack"); pet.defence = number("defence");
                    pet.quick = number("quick"); pet.earth = number("earth"); pet.water = number("water");
                    pet.fire = number("fire"); pet.wind = number("wind"); pet.skillSlots = number("slots");
                    pet.experience = ParseNumber64(properties.at("experience"));
                    pet.nextExperience = ParseNumber64(properties.at("next"));
                    pet.perception = number("perception"); pet.rebirth = number("rebirth");
                    pet.loyalty = -1;
                    pet.skillsReceived = true;
                    pet.skills.resize(7);
                    for (int i = 0; i < 7; ++i)
                    {
                        auto skill = properties.find("skill" + std::to_string(i));
                        if (skill == properties.end()) continue;
                        pet.skills[i].present = true;
                        pet.skills[i].name = AuctionUnhex(skill->second, 256);
                        pet.skills[i].description = AuctionUnhex(properties.at("description" + std::to_string(i)), 2048);
                    }
                    auction_.detailPet = std::move(pet);
                    auction_.detailType = "pet";
                }
                else if (properties["type"] == "item")
                {
                    Game::ItemStatus item;
                    item.name = AuctionUnhex(properties.at("name"), 256);
                    item.description = AuctionUnhex(properties.at("description"), 4096);
                    item.level = ParseNumber(properties.at("level")); item.levelKnown = true;
                    item.type = ParseNumber(properties.at("category"));
                    auction_.detailItem = std::move(item);
                    auction_.detailType = "item";
                }
                else throw std::runtime_error("竞拍属性类型无效");
                ++auction_.detailRevision;
                return {};
            }
            if (request.action != Game::AuctionAction::List) throw std::runtime_error("竞拍响应类型不匹配");
            if (values[1] == "BEGIN" && values.size() == 7 && !began)
            {
                if (AuctionNumber(values[2], 3) != static_cast<int>(request.page) ||
                    AuctionNumber(values[3], 1000000) != request.offset) throw std::runtime_error("竞拍分页不匹配");
                updated.page = request.page;
                updated.offset = request.offset;
                updated.enabled = AuctionNumber(values[4], 1) != 0;
                updated.listingLimit = static_cast<int>(AuctionNumber(values[5], 100));
                updated.activeListings = static_cast<int>(AuctionNumber(values[6]));
                began = true;
            }
            else if (values[1] == "ROW" && values.size() == 18 && began && updated.entries.size() < 20)
            {
                Game::AuctionEntry entry;
                entry.id = values[2]; entry.listingId = values[3]; entry.type = values[4];
                if (!AuctionNumber(entry.id, (std::numeric_limits<std::int64_t>::max)()) ||
                    !AuctionNumber(entry.listingId, (std::numeric_limits<std::int64_t>::max)()) ||
                    (entry.type != "pet" && entry.type != "item")) throw std::runtime_error("竞拍条目无效");
                entry.name = AuctionUnhex(values[5], 256);
                entry.quantity = static_cast<int>(AuctionNumber(values[6]));
                entry.level = static_cast<int>(AuctionNumber(values[7]));
                entry.price = AuctionNumber(values[8]); entry.buyout = AuctionNumber(values[9]);
                entry.seller = AuctionUnhex(values[10], 128); entry.status = values[11];
                entry.remainingSeconds = AuctionNumber(values[12], 43200);
                entry.minimumBid = AuctionNumber(values[13], 4294967294LL);
                entry.canBid = AuctionNumber(values[14], 1) && updated.enabled;
                entry.canBuyout = AuctionNumber(values[15], 1) && updated.enabled;
                entry.canCancel = AuctionNumber(values[16], 1) != 0;
                entry.canClaim = AuctionNumber(values[17], 1) != 0;
                updated.entries.push_back(std::move(entry));
            }
            else if (values[1] == "END" && values.size() == 5 && began)
            {
                if (AuctionNumber(values[2], 3) != static_cast<int>(request.page) ||
                    AuctionNumber(values[3], 1000000) != request.offset) throw std::runtime_error("竞拍分页结束不匹配");
                updated.hasNext = AuctionNumber(values[4], 1) != 0;
                updated.received = true;
                ++updated.revision;
                auction_ = std::move(updated);
                return {};
            }
            else throw std::runtime_error("竞拍列表响应无效");
        }
    }
    catch (...)
    {
        bool timedOut = GetTickCount64() >= deadline;
        Disconnect();
        if (timedOut) throw std::runtime_error("竞拍响应超时，连接已关闭，请重新登录确认交易结果");
        throw;
    }
}

void LoginClient::RequestAuction(const Game::AuctionRequest& request)
{
    try
    {
        if (!inGame_ || playerStatus_.inBattle || IsEncounterEnabled() || IsEncounterStopping())
            throw std::runtime_error("请先停止遇敌并结束战斗，再操作竞拍");
        auto query = request;
        auto listCommand = [&]() {
            if (query.offset < 0 || query.offset > 1000000 || query.offset % 20 ||
                static_cast<int>(query.page) < 0 || static_cast<int>(query.page) > 3 ||
                (!query.type.empty() && query.type != "pet" && query.type != "item"))
                throw std::runtime_error("竞拍筛选条件无效");
            return "LIST|" + std::to_string(static_cast<int>(query.page)) + "|" +
                (query.type.empty() ? "all" : query.type) + "|" + std::to_string(query.offset) + "|" + AuctionKeyword(query.keyword);
        };
        std::string command, id = "0";
        if (request.action == Game::AuctionAction::List) command = listCommand();
        else if (request.action == Game::AuctionAction::Detail)
        {
            if (!AuctionNumber(request.id, (std::numeric_limits<std::int64_t>::max)())) throw std::runtime_error("拍卖编号无效");
            command = "DETAIL|" + request.id;
        }
        else
        {
            id = AuctionId();
            if (request.action == Game::AuctionAction::Create)
            {
                bool pet = request.type == "pet";
                if ((!pet && request.type != "item") || request.amount < 1 || request.amount > INT_MAX ||
                    request.buyout < 0 || request.buyout > INT_MAX || (request.buyout && request.buyout <= request.amount) ||
                    (request.hours != 4 && request.hours != 8 && request.hours != 12)) throw std::runtime_error("上架参数无效");
                std::string code = request.uniqueCode;
                if (pet)
                {
                    if (request.slot < 0 || request.slot >= static_cast<int>(pets_.size()) ||
                        !pets_[request.slot].present || code.empty() || pets_[request.slot].uniqueCode != code)
                        throw std::runtime_error("宠物已变化，请重新选择");
                }
                else
                {
                    if (request.slot < 5 || request.slot >= static_cast<int>(items_.size()) || !items_[request.slot].present ||
                        items_[request.slot].revision != request.assetRevision) throw std::runtime_error("道具已变化，请重新选择");
                    auto error = ItemStorageRequest("CODE", request.slot, {}, request.assetRevision);
                    if (!error.empty()) { auction_.message = AuctionError(error); return; }
                    code = items_[request.slot].uniqueCode;
                }
                if (code.empty() || code.size() > 128) throw std::runtime_error("资产唯一编码无效");
                command = "CREATE|" + id + "|" + request.type + "|" + std::to_string(request.slot) + "|" +
                    std::to_string(request.amount) + "|" + std::to_string(request.buyout) + "|" +
                    std::to_string(request.hours) + "|" + AuctionHex(code);
            }
            else
            {
                if (!AuctionNumber(request.id, (std::numeric_limits<std::int64_t>::max)())) throw std::runtime_error("竞拍编号无效");
                const char* action = request.action == Game::AuctionAction::Bid ? "BID" :
                    request.action == Game::AuctionAction::Buyout ? "BUYOUT" : request.action == Game::AuctionAction::Cancel ? "CANCEL" :
                    request.action == Game::AuctionAction::Claim ? "CLAIM" : nullptr;
                if (!action) throw std::runtime_error("竞拍操作无效");
                command = std::string(action) + "|" + id + "|" + request.id;
                if (request.action == Game::AuctionAction::Bid || request.action == Game::AuctionAction::Buyout)
                {
                    if (request.amount < 1 || request.amount > INT_MAX) throw std::runtime_error("竞拍金额无效");
                    command += "|" + std::to_string(request.amount);
                }
            }
        }
        auto error = AuctionExchange(command, id, request);
        if (!error.empty())
        {
            if (error == "DISABLED") auction_.enabled = false;
            auction_.message = AuctionError(error);
            return;
        }
        bool mutation = request.action != Game::AuctionAction::List && request.action != Game::AuctionAction::Detail;
        if (mutation)
        {
            query.action = Game::AuctionAction::List;
            if (request.action == Game::AuctionAction::Create)
            {
                query.page = Game::AuctionPage::Selling;
                query.type.clear(); query.keyword.clear(); query.offset = 0;
            }
            error = AuctionExchange(listCommand(), "0", query);
            auction_.message = error.empty() ? L"操作成功，列表已刷新" : L"操作成功；列表刷新失败：" + AuctionError(error);
        }
        else auction_.message = request.action == Game::AuctionAction::Detail ? L"已读取竞拍详情" :
            auction_.enabled ? L"竞拍列表已刷新" : L"竞拍暂停新交易，已有订单仍可撤拍和领取";
    }
    catch (const std::exception& error)
    {
        if (IsConnected()) auction_.message = TextEncoding::ErrorText(error.what());
        throw;
    }
}
