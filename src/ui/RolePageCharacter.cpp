#include "RolePage.h"
#include "UiText.h"

namespace Client
{
    void RolePage::AddRow(const wchar_t* name, const CString& value)
    {
        int row = detail_.InsertItem(detail_.GetItemCount(), name);
        detail_.SetItemText(row, 1, value);
    }

    void RolePage::RefreshDetail()
    {
        detail_.DeleteAllItems();
        if (!IsWindowVisible() || tabs_.GetCurSel() != 0) return;
        const auto& p = view_.player;
        if (!p.received)
        {
            AddRow(L"状态", L"正在等待人物数据…");
            return;
        }
        AddRow(L"等级 / 转生", Ratio(p.level, p.rebirth));
        AddRow(L"生命", Ratio(p.hp, p.maxHp));
        AddRow(L"魔力", Ratio(p.mp, p.maxMp));
        AddRow(L"体力", Number(p.vital)); AddRow(L"腕力", Number(p.strength));
        AddRow(L"耐力", Number(p.toughness)); AddRow(L"速度", Number(p.dexterity));
        AddRow(L"攻击", Number(p.attack)); AddRow(L"防御", Number(p.defence));
        AddRow(L"敏捷", Number(p.quick)); AddRow(L"魅力", Number(p.charm));
        AddRow(L"运气", Number(p.luck)); AddRow(L"感知", Number(p.perception));
        CString elements;
        elements.Format(L"地 %d   水 %d   火 %d   风 %d", p.earth, p.water, p.fire, p.wind);
        AddRow(L"元素", elements); AddRow(L"经验 / 下级经验", Ratio(p.experience, p.nextExperience));
        AddRow(L"金币", Number(p.gold)); AddRow(L"决斗积分", Number(p.duelPoints));
    }
}
