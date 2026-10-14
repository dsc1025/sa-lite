#include "BattleGroup.h"
#include "UiText.h"
#include <algorithm>

namespace Client
{
    void BattleGroup::Update(const Game::BattleState& battle, int offset, bool received)
    {
        for (int i = 0; i < 10; ++i)
            members_[i] = battle.members[offset + i];
        offset_ = offset;
        received_ = received;
        Invalidate(FALSE);
    }

    void BattleGroup::DrawItem(LPDRAWITEMSTRUCT item)
    {
        CDC* dc = CDC::FromHandle(item->hDC);
        int saved = dc->SaveDC();
        if (CFont* font = GetFont())
            dc->SelectObject(font);
        CRect area(item->rcItem);
        dc->FillSolidRect(area, GetSysColor(COLOR_BTNFACE));
        CString caption;
        GetWindowText(caption);
        CSize size = dc->GetTextExtent(caption);
        CRect border = area;
        border.top += size.cy / 2;
        dc->DrawEdge(&border, EDGE_ETCHED, BF_RECT);
        CRect title(area.left + 8, area.top, area.left + 16 + size.cx, area.top + size.cy);
        dc->FillSolidRect(title, GetSysColor(COLOR_BTNFACE));
        dc->SetTextColor(GetSysColor(COLOR_BTNTEXT));
        dc->SetBkMode(TRANSPARENT);
        dc->DrawText(caption, title, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX);
        CRect content(area.left + 12, area.top + size.cy + 8, area.right - 10, area.bottom - 10);
        if (!received_)
            dc->DrawText(L"等待战斗队员数据", content, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        else
        {
            int lineHeight = size.cy + 4;
            int spacing = std::max(1, std::min(lineHeight, content.Height() / 10));
            int top = content.top + (content.Height() - spacing * 10) / 2;
            // Each of the five fixed positions keeps its pet above its character.
            const int order[] = { 9, 4, 7, 2, 5, 0, 6, 1, 8, 3 };
            for (int rowIndex = 0; rowIndex < 10; ++rowIndex)
            {
                int slot = order[rowIndex];
                const auto& member = members_[slot];
                if (!member.present)
                    continue;
                CString text;
                text.Format(L"* [%X] %s  Lv:%d (%d/%d)", offset_ + slot,
                    LegacyText(member.name).GetString(), member.level, member.hp, member.maxHp);
                if (!member.rideName.empty())
                {
                    CString ride;
                    ride.Format(L" [骑] %s  Lv:%d (%d/%d)", LegacyText(member.rideName).GetString(),
                        member.rideLevel, member.rideHp, member.rideMaxHp);
                    text += ride;
                }
                int rowTop = top + rowIndex * spacing;
                CRect row(content.left, rowTop, content.right,
                    std::min<LONG>(rowTop + spacing, content.bottom));
                if (row.bottom > row.top)
                    dc->DrawText(text, row, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }
        }
        dc->RestoreDC(saved);
    }
}
