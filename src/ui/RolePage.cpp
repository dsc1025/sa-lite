#include "RolePage.h"
#include "RoleControlIds.h"
#include "../session/GameSession.h"
#include "../session/SessionView.h"
#include "UiText.h"
#include "PetUi.h"

namespace Client
{
    BEGIN_MESSAGE_MAP(RolePage, CWnd)
        ON_WM_SIZE()
        ON_WM_PAINT()
        ON_NOTIFY(TCN_SELCHANGE, Tabs, &RolePage::OnTab)
        ON_NOTIFY(LVN_ITEMCHANGED, List, &RolePage::OnSelect)
        ON_NOTIFY(NM_CLICK, List, &RolePage::OnPetClick)
        ON_NOTIFY(NM_RCLICK, List, &RolePage::OnListRightClick)
        ON_NOTIFY(NM_RCLICK, PetStorageList, &RolePage::OnPetStorageRightClick)
        ON_NOTIFY(NM_DBLCLK, PetStorageList, &RolePage::OnListDoubleClick)
        ON_NOTIFY(NM_DBLCLK, List, &RolePage::OnListDoubleClick)
        ON_NOTIFY(NM_DBLCLK, ItemStorageList, &RolePage::OnListDoubleClick)
        ON_NOTIFY(NM_RCLICK, ItemStorageList, &RolePage::OnItemStorageRightClick)
        ON_CBN_SELENDOK(PetStateCombo, &RolePage::OnPetState)
        ON_CBN_SELENDCANCEL(PetStateCombo, &RolePage::OnPetStateClose)
        ON_CBN_KILLFOCUS(PetStateCombo, &RolePage::OnPetStateClose)
    END_MESSAGE_MAP()

    bool RolePage::Create(CWnd* owner, CFont* font)
    {
        CRect empty(0, 0, 0, 0);
        if (!CWnd::CreateEx(WS_EX_CONTROLPARENT, AfxRegisterWndClass(0, LoadCursor(nullptr, IDC_ARROW),
            reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1)), L"",
            WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, empty, owner, 130))
            return false;
        SetFont(font);
        tabs_.Create(WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS, empty, this, Tabs);
        tabs_.InsertItem(0, L"属性");
        tabs_.InsertItem(1, L"宠物");
        tabs_.InsertItem(2, L"道具");
        DWORD listStyle = WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS;
        list_.Create(listStyle, empty, this, List);
        detail_.Create(listStyle, empty, this, Detail);
        petStorage_.Create(listStyle | WS_VSCROLL, empty, this, PetStorageList);
        itemStorage_.Create(listStyle, empty, this, ItemStorageList);
        itemStorage_.ModifyStyleEx(0, WS_EX_CLIENTEDGE);
        itemStorage_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        itemStorage_.InsertColumn(0, L"名称", LVCFMT_LEFT, 180);
        itemStorage_.InsertColumn(1, L"分类", LVCFMT_LEFT, 80);
        itemStorage_.SetBkColor(RGB(255, 255, 255));
        itemStorage_.SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
        petState_.Create(WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, empty, this, PetStateCombo);
        for (const wchar_t* state : PetStateNames)
            petState_.AddString(state);
        tabs_.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        list_.ModifyStyleEx(0, WS_EX_CLIENTEDGE);
        detail_.ModifyStyleEx(0, WS_EX_CLIENTEDGE);
        petStorage_.ModifyStyleEx(0, WS_EX_CLIENTEDGE);
        list_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        detail_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        petStorage_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        petStorage_.InsertColumn(0, L"名称", LVCFMT_LEFT, 150);
        petStorage_.InsertColumn(1, L"等级", LVCFMT_RIGHT, 45);
        list_.InsertColumn(0, L"名称", LVCFMT_LEFT, 180);
        list_.InsertColumn(1, L"等级", LVCFMT_RIGHT, 70);
        detail_.InsertColumn(0, L"属性", LVCFMT_LEFT, 150);
        detail_.InsertColumn(1, L"数值", LVCFMT_LEFT, 230);
        list_.SetBkColor(RGB(255, 255, 255));
        detail_.SetBkColor(RGB(255, 255, 255));
        petStorage_.SetBkColor(RGB(255, 255, 255));
        list_.SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
        detail_.SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
        petStorage_.SetTextColor(GetSysColor(COLOR_WINDOWTEXT));
        for (CWnd* child = GetWindow(GW_CHILD); child; child = child->GetNextWindow())
            child->SetFont(font);
        return true;
    }

    void RolePage::Layout()
    {
        CloseEditor();
        CRect area;
        GetClientRect(&area);
        if (area.IsRectEmpty())
            return;
        tabs_.MoveWindow(area);
        CRect page = area;
        tabs_.AdjustRect(FALSE, &page);
        page.DeflateRect(6, 6);
        bool player = tabs_.GetCurSel() == 0;
        bool pets = tabs_.GetCurSel() == 1;
        bool items = tabs_.GetCurSel() == 2;
        int itemWidth = (page.Width() - 16) / 2;
        list_.MoveWindow(page.left, page.top, pets || items ? itemWidth : 220, page.Height());
        if (pets)
            petStorage_.MoveWindow(page.left + itemWidth + 16, page.top,
                page.Width() - itemWidth - 16, page.Height());
        if (items)
            itemStorage_.MoveWindow(page.left + itemWidth + 16, page.top,
                page.Width() - itemWidth - 16, page.Height());
        detail_.MoveWindow(player ? page.left : page.left + 236, page.top,
            player ? page.Width() : page.Width() - 236, page.Height());
        if (view_.screen == Screen::Game)
        {
            CRect detailArea, listArea;
            detail_.GetClientRect(&detailArea);
            list_.GetClientRect(&listArea);
            int detailWidth = detailArea.Width() - 150 - GetSystemMetrics(SM_CXVSCROLL);
            bool petColumns = tabs_.GetCurSel() == 1 && list_.GetHeaderCtrl()->GetItemCount() == 3;
            int listWidth = listArea.Width() - (petColumns ? 105 : items ? 80 : 70) - GetSystemMetrics(SM_CXVSCROLL);
            list_.SetColumnWidth(1, petColumns ? 45 : items ? 80 : 70);
            if (petColumns)
                list_.SetColumnWidth(2, 60);
            detail_.SetColumnWidth(1, detailWidth > 0 ? detailWidth : 0);
            list_.SetColumnWidth(0, listWidth > 0 ? listWidth : 0);
            detail_.ShowScrollBar(SB_HORZ, FALSE);
            list_.ShowScrollBar(SB_HORZ, FALSE);
            CRect storageArea;
            petStorage_.GetClientRect(&storageArea);
            int storageWidth = storageArea.Width() - 45 - GetSystemMetrics(SM_CXVSCROLL);
            petStorage_.SetColumnWidth(0, storageWidth > 0 ? storageWidth : 0);
            petStorage_.SetColumnWidth(1, 45);
            petStorage_.ShowScrollBar(SB_HORZ, FALSE);
            CRect itemStorageArea;
            itemStorage_.GetClientRect(&itemStorageArea);
            int itemStorageWidth = itemStorageArea.Width() - 80 - GetSystemMetrics(SM_CXVSCROLL);
            itemStorage_.SetColumnWidth(0, itemStorageWidth > 0 ? itemStorageWidth : 0);
            itemStorage_.SetColumnWidth(1, 80);
            itemStorage_.ShowScrollBar(SB_HORZ, FALSE);
        }
        Invalidate(FALSE);
    }

    void RolePage::Refresh()
    {
        refreshing_ = true;
        bool game = view_.screen == Screen::Game;
        bool role = game && IsWindowVisible();
        bool pets = role && tabs_.GetCurSel() == 1;
        bool items = role && tabs_.GetCurSel() == 2;
        tabs_.ShowWindow(role ? SW_SHOW : SW_HIDE);
        list_.ShowWindow(pets || items ? SW_SHOW : SW_HIDE);
        detail_.ShowWindow(role && tabs_.GetCurSel() == 0 ? SW_SHOW : SW_HIDE);
        petStorage_.ShowWindow(pets ? SW_SHOW : SW_HIDE);
        itemStorage_.ShowWindow(items ? SW_SHOW : SW_HIDE);
        bool petColumns = role && tabs_.GetCurSel() == 1;
        int columns = list_.GetHeaderCtrl()->GetItemCount();
        if (petColumns && columns == 2)
            list_.InsertColumn(2, L"状态", LVCFMT_LEFT, 60);
        else if (!petColumns && columns == 3)
            list_.DeleteColumn(2);
        LVCOLUMN column{};
        column.mask = LVCF_TEXT | LVCF_FMT;
        column.pszText = const_cast<wchar_t*>(items ? L"分类" : L"等级");
        column.fmt = items ? LVCFMT_LEFT : LVCFMT_RIGHT;
        list_.SetColumn(1, &column);
        int previous = list_.GetNextItem(-1, LVNI_SELECTED);
        DWORD_PTR previousSlot = previous >= 0 ? list_.GetItemData(previous) : 0;
        list_.DeleteAllItems();
        if (role && tabs_.GetCurSel() == 1)
        {
            RefreshPets();
        }
        else if (role && tabs_.GetCurSel() == 2)
        {
            RefreshItems();
        }
        int selection = 0;
        for (int i = 0; i < list_.GetItemCount(); ++i)
            if (list_.GetItemData(i) == previousSlot) selection = i;
        if (list_.GetItemCount())
            list_.SetItemState(selection, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        RefreshItemStorage(items);
        RefreshPetStorage(pets);
        if (game) RefreshDetail();
        refreshing_ = false;
        Layout();
    }

    void RolePage::OnTab(NMHDR*, LRESULT* result)
    {
        list_.DeleteAllItems();
        Refresh();
        Activate();
        *result = 0;
    }

    void RolePage::Activate()
    {
        CloseEditor();
        if (view_.screen != Screen::Game || !IsWindowVisible())
            return;
        if (tabs_.GetCurSel() == 1)
            session_.RequestPetStorage(Game::PetStorageAction::List);
        else if (tabs_.GetCurSel() == 2)
            session_.RequestItemStorage(Game::ItemStorageAction::List);
    }

    void RolePage::CloseEditor()
    {
        if (petState_.GetSafeHwnd())
            petState_.ShowDropDown(FALSE);
        OnPetStateClose();
    }

    void RolePage::OnSize(UINT type, int width, int height)
    {
        CWnd::OnSize(type, width, height);
        if (detail_.GetSafeHwnd())
            Layout();
    }

    void RolePage::OnPaint()
    {
        CPaintDC dc(this);
        CRect area;
        GetClientRect(&area);
        dc.FillSolidRect(area, GetSysColor(COLOR_BTNFACE));
    }

    BOOL RolePage::PreTranslateMessage(MSG* message)
    {
        if (petState_.GetSafeHwnd() && petState_.IsWindowVisible() && message->message == WM_KEYDOWN)
        {
            if (message->wParam == VK_ESCAPE)
            {
                CloseEditor();
                return TRUE;
            }
            if (message->wParam == VK_RETURN)
            {
                OnPetState();
                return TRUE;
            }
        }
        return CWnd::PreTranslateMessage(message);
    }

    void RolePage::OnSelect(NMHDR*, LRESULT* result)
    {
        if (!refreshing_ && view_.screen == Screen::Game)
            RefreshDetail();
        *result = 0;
    }
}
