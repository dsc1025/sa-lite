#include "PetUi.h"
#include "UiText.h"
#include <charconv>
#include <string_view>
#include "../game/PlayerStatus.h"
#include "../game/PetTypes.h"
#include "../game/ItemTypes.h"

namespace Client
{
    int PetStateIndex(const Game::PlayerStatus& player, int slot)
    {
        if (player.ridePet == slot) return static_cast<int>(Game::PetState::Ride);
        if (player.battlePet == slot) return static_cast<int>(Game::PetState::Battle);
        return static_cast<int>(Game::PetState::Standby);
    }
    namespace
    {
        bool ParsePetInitialAttributes(const std::string& nickname, int (&attributes)[5])
        {
            std::string_view values(nickname);
            if (values.size() >= 2 && values.front() == '[' && values.back() == ']')
                values = values.substr(1, values.size() - 2);
            for (int i = 0; i < 5; ++i)
            {
                auto end = values.find('/');
                if ((i < 4 && end == std::string_view::npos) ||
                    (i == 4 && end != std::string_view::npos))
                    return false;
                auto value = values.substr(0, end);
                if (value.empty() || value.front() < '0' || value.front() > '9')
                    return false;
                auto parsed = std::from_chars(value.data(), value.data() + value.size(), attributes[i]);
                if (parsed.ec != std::errc() || parsed.ptr != value.data() + value.size())
                    return false;
                if (i < 4)
                    values.remove_prefix(end + 1);
            }
            return true;
        }

    }
    CString PetPropertiesText(const Game::PetStatus& pet)
    {
        CString text;
        text.Format(L"名称：%s\r\n昵称：%s\r\n转生：%d\r\n等级：%d\r\n"
            L"生命：%d / %d\r\n"
            // L"魔力：%d / %d\r\n"
            L"攻击：%d\r\n防御：%d\r\n敏捷：%d\r\n"
            // L"感知：%d\r\n"
            L"忠诚：%s\r\n技能栏：%d\r\n元素：地 %d  水 %d  火 %d  风 %d\r\n"
            L"经验 / 下级经验：%d / %d",
            LegacyText(pet.name).GetString(), LegacyText(pet.nickname).GetString(), pet.rebirth, pet.level,
            pet.hp, pet.maxHp, /* pet.mp, pet.maxMp, */ pet.attack, pet.defence, pet.quick,
            /* pet.perception, */ (pet.loyalty < 0 ? CString(L"领取后确定") : Number(pet.loyalty)).GetString(),
            pet.skillSlots, pet.earth, pet.water, pet.fire, pet.wind,
            pet.experience, pet.nextExperience);
        int initialAttributes[5];
        if (pet.level >= 1 && ParsePetInitialAttributes(pet.nickname, initialAttributes))
        {
            const int attributes[] = { pet.maxHp, pet.attack, pet.defence, pet.quick, pet.perception };
            double growth[5] = {};
            if (pet.level > 1)
            {
                for (int i = 0; i < 5; ++i)
                    growth[i] = (static_cast<double>(attributes[i]) - initialAttributes[i]) / (pet.level - 1);
            }
            CString line;
            line.Format(L"\r\n\r\n成长计算:\r\n生命:%.3f 攻击:%.3f 防御:%.3f 敏捷:%.3f" /* L" 感知:%.3f" */ L" 合计:%.3f",
                growth[0], growth[1], growth[2], growth[3] /* , growth[4] */, growth[1] + growth[2] + growth[3]);
            text += line;
        }
        text += L"\r\n\r\n宠物技能：";
        if (!pet.skillsReceived)
            text += L"\r\n等待技能数据…";
        else
        {
            bool hasSkills = false;
            for (std::size_t i = 0; i < pet.skills.size(); ++i)
            {
                const auto& skill = pet.skills[i];
                if (!skill.present)
                    continue;
                CString line;
                line.Format(L"\r\n%d. %s", static_cast<int>(i + 1), LegacyText(skill.name).GetString());
                text += line;
                if (!skill.description.empty())
                {
                    text += L"  ";
                    text += LegacyText(skill.description);
                }
                hasSkills = true;
            }
            if (!hasSkills)
                text += L"\r\n无";
        }
        return text;
    }
    CString ItemTypeName(int type)
    {
        const wchar_t* names[] = {
            L"拳", L"斧头", L"棍棒", L"枪／矛", L"弓", L"盾", L"头盔", L"铠甲",
            L"手镯", L"乐器", L"项链", L"戒指", L"腰带", L"耳环", L"鼻环", L"护身符",
            L"道具", L"回旋镖", L"投掷斧头", L"投掷石", L"料理", L"金属", L"宝石", L"货物"
        };
        return type >= 0 && type < static_cast<int>(sizeof(names) / sizeof(names[0])) ?
            CString(names[type]) : Number(type);
    }

    CString ItemPropertiesText(const Game::ItemStatus& item)
    {
        CString text;
        text.Format(L"名称：%s\r\n分类：%s\r\n", LegacyText(item.name).GetString(),
            ItemTypeName(item.type).GetString());
        if (item.levelKnown)
        {
            CString level;
            level.Format(L"使用等级：%d\r\n", item.level);
            text += level;
        }
        text += L"\r\n说明：\r\n";
        text += LegacyText(item.description);
        return text;
    }
}
