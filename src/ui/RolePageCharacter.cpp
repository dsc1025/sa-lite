#include "RolePage.h"
#include "../session/SessionView.h"
#include "../session/GameSession.h"
#include "UiText.h"

namespace Client
{
    bool RolePage::CanIncreaseAttribute() const
    {
        return view_.screen == Screen::Game && IsWindowVisible() && tabs_.GetCurSel() == 0 &&
            view_.player.received && view_.player.remainingPointsReceived && view_.player.remainingPoints > 0 &&
            !view_.attributePending && !view_.itemPending && !view_.petStoragePending &&
            !view_.player.inBattle && !view_.encounterEnabled && !view_.encounterPending;
    }

    bool RolePage::AttributeButtonRect(int row, CRect& rect) const
    {
        if (row < 3 || row > 6 || !detail_.GetSubItemRect(row, 0, LVIR_BOUNDS, rect))
            return false;
        rect.right = rect.left + detail_.GetColumnWidth(0);
        rect.DeflateRect(2, 1);
        rect.left += 48;
        if (rect.Width() < 32)
            return false;
        rect.right = rect.left + 32;
        return !rect.IsRectEmpty();
    }

    void RolePage::OnDetailDraw(NMHDR* header, LRESULT* result)
    {
        auto draw = reinterpret_cast<NMLVCUSTOMDRAW*>(header);
        *result = CDRF_DODEFAULT;
        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
            *result = CDRF_NOTIFYITEMDRAW;
        else if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
            *result = CDRF_NOTIFYSUBITEMDRAW;
        else if (draw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM) && draw->iSubItem == 0)
            *result = CDRF_NOTIFYPOSTPAINT;
        else if (draw->nmcd.dwDrawStage == (CDDS_ITEMPOSTPAINT | CDDS_SUBITEM) && draw->iSubItem == 0 &&
            view_.player.remainingPointsReceived && view_.player.remainingPoints > 0)
        {
            CRect rect;
            if (!AttributeButtonRect(static_cast<int>(draw->nmcd.dwItemSpec), rect))
                return;
            bool enabled = CanIncreaseAttribute();
            CDC* dc = CDC::FromHandle(draw->nmcd.hdc);
            int saved = dc->SaveDC();
            dc->DrawFrameControl(rect, DFC_BUTTON, DFCS_BUTTONPUSH | (enabled ? 0 : DFCS_INACTIVE));
            dc->SetBkMode(TRANSPARENT);
            dc->SetTextColor(GetSysColor(enabled ? COLOR_BTNTEXT : COLOR_GRAYTEXT));
            dc->DrawText(L"up", rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            dc->RestoreDC(saved);
        }
    }

    void RolePage::OnDetailClick(NMHDR* header, LRESULT* result)
    {
        *result = 0;
        auto click = reinterpret_cast<NMITEMACTIVATE*>(header);
        CRect rect;
        if (!CanIncreaseAttribute() || click->iSubItem != 0 ||
            !AttributeButtonRect(click->iItem, rect) || !rect.PtInRect(click->ptAction))
            return;
        if (session_.RequestIncreaseAttribute(click->iItem - 3))
        {
            detail_.Invalidate(FALSE);
            footer_.SetWindowText(L"正在加点…");
        }
    }

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
        AddRow(L"剩余点数", p.remainingPointsReceived ? Number(p.remainingPoints) : CString(L"等待数据"));
        AddRow(L"攻击", Number(p.attack)); AddRow(L"防御", Number(p.defence));
        AddRow(L"敏捷", Number(p.quick)); AddRow(L"魅力", Number(p.charm));
        AddRow(L"运气", Number(p.luck)); AddRow(L"感知", Number(p.perception));
        CString elements;
        elements.Format(L"地 %d   水 %d   火 %d   风 %d", p.earth, p.water, p.fire, p.wind);
        AddRow(L"元素", elements); AddRow(L"经验 / 下级经验", Ratio(p.experience, p.nextExperience));
        AddRow(L"金币", Number(p.gold)); AddRow(L"决斗积分", Number(p.duelPoints));
    }
}
