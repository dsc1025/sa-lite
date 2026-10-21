#include "AuctionPage.h"
#include "../session/SessionView.h"
#include "UiText.h"
#include "PetUi.h"
#include <algorithm>
#include <initializer_list>
#include <limits>

namespace Client
{
    namespace
    {
        enum Control { Tabs = 1, List, Filter, Keyword, Search, Previous, Next, BidAmount,
            Bid, Buyout, Cancel, Claim, AssetType, Asset, Duration, StartPrice, BuyoutPrice, Submit };

        CString Price(std::int64_t value)
        {
            CString text;
            text.Format(L"%lld P", static_cast<long long>(value));
            return text;
        }

        bool ReadPrice(const CEdit& edit, std::int64_t& value, bool optional = false)
        {
            CString text;
            edit.GetWindowText(text);
            text.Trim();
            value = 0;
            if (text.IsEmpty()) return optional;
            for (int i = 0; i < text.GetLength(); ++i)
            {
                if (text[i] < L'0' || text[i] > L'9') return false;
                int digit = text[i] - L'0';
                if (value > (std::numeric_limits<int>::max() - digit) / 10) return false;
                value = value * 10 + digit;
            }
            return value > 0;
        }
    }

    BEGIN_MESSAGE_MAP(AuctionPage, CWnd)
        ON_WM_SIZE()
        ON_NOTIFY(TCN_SELCHANGE, Tabs, &AuctionPage::OnTab)
        ON_NOTIFY(LVN_ITEMCHANGED, List, &AuctionPage::OnSelect)
        ON_NOTIFY(NM_DBLCLK, List, &AuctionPage::OnDetail)
        ON_BN_CLICKED(Search, &AuctionPage::OnSearch)
        ON_BN_CLICKED(Previous, &AuctionPage::OnPrevious)
        ON_BN_CLICKED(Next, &AuctionPage::OnNext)
        ON_BN_CLICKED(Bid, &AuctionPage::OnBid)
        ON_BN_CLICKED(Buyout, &AuctionPage::OnBuyout)
        ON_BN_CLICKED(Cancel, &AuctionPage::OnCancel)
        ON_BN_CLICKED(Claim, &AuctionPage::OnClaim)
        ON_BN_CLICKED(Submit, &AuctionPage::OnCreateListing)
        ON_CBN_SELCHANGE(AssetType, &AuctionPage::OnAssetType)
        ON_CBN_SELCHANGE(Asset, &AuctionPage::OnAsset)
    END_MESSAGE_MAP()

    bool AuctionPage::Create(CWnd* owner, CFont* font)
    {
        CRect empty(0, 0, 0, 0);
        if (!CWnd::CreateEx(WS_EX_CONTROLPARENT, AfxRegisterWndClass(0, LoadCursor(nullptr, IDC_ARROW),
            reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1)), L"竞拍",
            WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, empty, owner, 160)) return false;
        tabs_.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS, empty, this, Tabs);
        const wchar_t* names[] = { L"综合列表", L"我的上架", L"我的出价", L"待领取", L"上架" };
        for (int i = 0; i < 5; ++i) tabs_.InsertItem(i, names[i]);
        list_.Create(WS_CHILD | WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            empty, this, List);
        list_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        const wchar_t* columns[] = { L"名称", L"类型", L"数量", L"等级", L"当前价 / 金额", L"一口价", L"卖家", L"剩余 / 状态" };
        const int widths[] = { 140, 50, 45, 45, 100, 85, 90, 130 };
        for (int i = 0; i < 8; ++i) list_.InsertColumn(i, columns[i], LVCFMT_LEFT, widths[i]);
        DWORD combo = WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST;
        filter_.Create(combo, empty, this, Filter);
        for (auto name : { L"全部类型", L"宠物", L"道具" }) filter_.AddString(name);
        filter_.SetCurSel(0);
        DWORD edit = WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
        keyword_.Create(edit, empty, this, Keyword);
        keyword_.SetLimitText(64);
        keyword_.SetCueBanner(L"搜索名称");
        bidAmount_.Create(edit | ES_NUMBER, empty, this, BidAmount);
        startPrice_.Create(edit | ES_NUMBER, empty, this, StartPrice);
        buyoutPrice_.Create(edit | ES_NUMBER, empty, this, BuyoutPrice);
        for (auto input : { &bidAmount_, &startPrice_, &buyoutPrice_ }) input->SetLimitText(10);
        startPrice_.SetWindowText(L"1");
        buyoutPrice_.SetCueBanner(L"选填");
        DWORD button = WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON;
        search_.Create(L"搜索 / 刷新", button, empty, this, Search);
        previous_.Create(L"上一页", button, empty, this, Previous);
        next_.Create(L"下一页", button, empty, this, Next);
        bid_.Create(L"出价", button, empty, this, Bid);
        buyout_.Create(L"一口价", button, empty, this, Buyout);
        cancel_.Create(L"撤拍", button, empty, this, Cancel);
        claim_.Create(L"领取", button, empty, this, Claim);
        submit_.Create(L"确认上架", button, empty, this, Submit);
        pagination_.Create(L"", WS_CHILD, empty, this);
        bidLabel_.Create(L"出价 P", WS_CHILD, empty, this);
        assetType_.Create(combo, empty, this, AssetType);
        assetType_.AddString(L"宠物");
        assetType_.AddString(L"道具");
        assetType_.SetCurSel(0);
        asset_.Create(combo, empty, this, Asset);
        duration_.Create(combo, empty, this, Duration);
        for (auto hours : { L"4 小时", L"8 小时", L"12 小时" }) duration_.AddString(hours);
        duration_.SetCurSel(0);
        const wchar_t* captions[] = { L"类型", L"出售资产", L"起拍价 P", L"一口价 P", L"上架时长" };
        for (int i = 0; i < 5; ++i) labels_[i].Create(captions[i], WS_CHILD, empty, this);
        assetDetail_.Create(L"", WS_CHILD, empty, this);
        rules_.Create(L"道具整组出售；仅无人出价时可撤拍。\r\n成交货物、货款和退款在“待领取”中领取。", WS_CHILD, empty, this);
        tabs_.SetWindowPos(&CWnd::wndBottom, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        for (CWnd* child = GetWindow(GW_CHILD); child; child = child->GetNextWindow()) child->SetFont(font);
        Refresh();
        return true;
    }

    void AuctionPage::Layout()
    {
        if (!tabs_.GetSafeHwnd()) return;
        CRect area;
        GetClientRect(&area);
        tabs_.MoveWindow(area);
        tabs_.AdjustRect(FALSE, &area);
        area.DeflateRect(6, 6);
        int x = area.left, y = area.top, width = area.Width();
        bool create = tabs_.GetCurSel() == 4;
        filter_.MoveWindow(x, y, 100, 180);
        keyword_.MoveWindow(x + 108, y, std::max(80, width - 220), 25);
        search_.MoveWindow(x + width - 104, y, 104, 25);
        list_.MoveWindow(x, y + 31, width, std::max(40, static_cast<int>(area.bottom) - y - 66));
        int bottom = area.bottom - 28;
        previous_.MoveWindow(x, bottom, 72, 25);
        next_.MoveWindow(x + 80, bottom, 72, 25);
        pagination_.MoveWindow(x + 160, bottom + 3, 96, 22);
        int actionsLeft = x + width - 314;
        bidLabel_.MoveWindow(actionsLeft, bottom + 3, 55, 22);
        bidAmount_.MoveWindow(actionsLeft + 58, bottom, 100, 25);
        bid_.MoveWindow(actionsLeft + 166, bottom, 65, 25);
        buyout_.MoveWindow(actionsLeft + 239, bottom, 75, 25);
        cancel_.MoveWindow(x + width - 75, bottom, 75, 25);
        claim_.MoveWindow(x + width - 75, bottom, 75, 25);
        labels_[0].MoveWindow(x, y + 3, 70, 22);
        assetType_.MoveWindow(x + 76, y, 100, 160);
        labels_[1].MoveWindow(x + 190, y + 3, 70, 22);
        asset_.MoveWindow(x + 260, y, std::max(120, width - 260), 180);
        assetDetail_.MoveWindow(x, y + 32, width, 42);
        labels_[2].MoveWindow(x, y + 82, 76, 22);
        startPrice_.MoveWindow(x + 76, y + 78, 105, 26);
        labels_[3].MoveWindow(x + 196, y + 82, 76, 22);
        buyoutPrice_.MoveWindow(x + 272, y + 78, 105, 26);
        labels_[4].MoveWindow(x, y + 118, 76, 22);
        duration_.MoveWindow(x + 76, y + 114, 105, 160);
        submit_.MoveWindow(x + 272, y + 114, 105, 26);
        rules_.MoveWindow(x, y + 150, width, 45);
        for (auto control : std::initializer_list<CWnd*>{ &filter_, &keyword_, &search_, &list_, &previous_, &pagination_, &next_ })
            ShowIfChanged(*control, !create);
        for (auto control : std::initializer_list<CWnd*>{ &assetType_, &asset_, &duration_, &startPrice_, &buyoutPrice_, &submit_, &rules_, &assetDetail_ })
            ShowIfChanged(*control, create);
        for (auto& label : labels_) ShowIfChanged(label, create);
        bool bidding = tabs_.GetCurSel() == 0;
        ShowIfChanged(bidLabel_, bidding);
        ShowIfChanged(bidAmount_, bidding);
        ShowIfChanged(bid_, bidding);
        ShowIfChanged(buyout_, bidding || tabs_.GetCurSel() == 2);
        ShowIfChanged(cancel_, tabs_.GetCurSel() == 1);
        ShowIfChanged(claim_, tabs_.GetCurSel() == 3);
    }

    bool AuctionPage::Ready() const
    {
        return request && view_.auction.available && !view_.auction.pending && view_.screen == Screen::Game &&
            !view_.player.inBattle && !view_.encounterEnabled && !view_.encounterPending &&
            !view_.shopPending && !view_.itemPending && !view_.petStoragePending && !view_.attributePending;
    }

    const Game::AuctionEntry* AuctionPage::Selected() const
    {
        int row = list_.GetNextItem(-1, LVNI_SELECTED);
        return row >= 0 && static_cast<std::size_t>(row) < displayed_.size() ? &displayed_[row] : nullptr;
    }

    void AuctionPage::UpdateActions()
    {
        const auto* entry = Selected();
        bool ready = Ready();
        EnableIfChanged(tabs_, !view_.auction.pending);
        for (auto control : std::initializer_list<CWnd*>{ &filter_, &keyword_, &bidAmount_, &assetType_,
            &asset_, &duration_, &startPrice_, &buyoutPrice_ }) EnableIfChanged(*control, ready);
        EnableIfChanged(search_, ready);
        EnableIfChanged(previous_, ready && offset_ > 0);
        EnableIfChanged(next_, ready && hasDisplayed_ && view_.auction.hasNext);
        EnableIfChanged(bid_, ready && view_.auction.enabled && entry && entry->canBid);
        EnableIfChanged(buyout_, ready && view_.auction.enabled && entry && entry->canBuyout);
        EnableIfChanged(cancel_, ready && entry && entry->canCancel);
        EnableIfChanged(claim_, ready && entry && entry->canClaim);
        EnableIfChanged(submit_, ready && view_.auction.enabled && asset_.GetCurSel() >= 0 &&
            view_.auction.activeListings < view_.auction.listingLimit);
    }

    void AuctionPage::Refresh()
    {
        if (!GetSafeHwnd()) return;
        const auto& auction = view_.auction;
        if (tabs_.GetCurSel() == 4 && assetRevision_ != auction.revision)
        {
            FillAssets();
            assetRevision_ = auction.revision;
        }
        if (detailRevision_ != auction.detailRevision)
        {
            detailRevision_ = auction.detailRevision;
            if (auction.available && !auction.pending && detailRevision_)
            {
                if (auction.detailType == "pet")
                    detail_.Show(GetTopLevelParent(), PetPropertiesText(auction.detailPet), L"宠物属性", auction.detailPet.image);
                else if (auction.detailType == "item")
                    detail_.Show(GetTopLevelParent(), ItemPropertiesText(auction.detailItem), L"道具属性");
                else detail_.Show(GetTopLevelParent(), auction.detail.c_str(), L"竞拍详情");
            }
        }
        bool matches = tabs_.GetCurSel() < 4 && static_cast<int>(auction.page) == tabs_.GetCurSel();
        if (!matches || !auction.available || !auction.received)
        {
            if (!displayed_.empty()) list_.DeleteAllItems();
            displayed_.clear();
            hasDisplayed_ = false;
        }
        else if (!hasDisplayed_ || displayedRevision_ != auction.revision)
        {
            std::string selected = Selected() ? Selected()->id : std::string();
            list_.DeleteAllItems();
            displayed_ = auction.entries;
            displayedRevision_ = auction.revision;
            hasDisplayed_ = true;
            offset_ = auction.offset;
            for (std::size_t i = 0; i < displayed_.size(); ++i)
            {
                const auto& entry = displayed_[i];
                int row = list_.InsertItem(static_cast<int>(i), LegacyText(entry.name));
                list_.SetItemText(row, 1, entry.type == "pet" ? L"宠物" : entry.type == "item" ? L"道具" : L"P");
                list_.SetItemText(row, 2, Number(entry.quantity));
                list_.SetItemText(row, 3, Number(entry.level));
                list_.SetItemText(row, 4, Price(entry.price));
                list_.SetItemText(row, 5, entry.buyout > 0 ? Price(entry.buyout) : CString(L"—"));
                list_.SetItemText(row, 6, LegacyText(entry.seller));
                CString state = LegacyText(entry.status);
                if (entry.status == "active") state = L"在售 / 待结算";
                else if (entry.status == "sold") state = L"已成交";
                else if (entry.status == "expired") state = L"流拍";
                else if (entry.status == "cancelled") state = L"已撤拍";
                else if (entry.status == "asset") state = L"资产待领取";
                else if (entry.status == "refund") state = L"退款待领取";
                else if (entry.status == "proceeds") state = L"货款待领取";
                if (entry.status == "active" && entry.remainingSeconds > 0)
                    state.Format(L"%lld小时%lld分", static_cast<long long>(entry.remainingSeconds / 3600),
                        static_cast<long long>((entry.remainingSeconds % 3600 + 59) / 60));
                list_.SetItemText(row, 7, state);
                if (entry.id == selected) list_.SetItemState(row, LVIS_SELECTED, LVIS_SELECTED);
            }
        }
        CString page;
        page.Format(L"第 %d 页", offset_ / 20 + 1);
        SetTextIfChanged(pagination_, page);
        UpdateActions();
    }

    void AuctionPage::Activate()
    {
        FillAssets();
        Refresh();
        if (Ready() && tabs_.GetCurSel() < 4) Load(0);
    }

    Game::AuctionRequest AuctionPage::Query() const
    {
        Game::AuctionRequest value;
        value.page = static_cast<Game::AuctionPage>(std::min(3, tabs_.GetCurSel()));
        value.type = filter_.GetCurSel() == 1 ? "pet" : filter_.GetCurSel() == 2 ? "item" : "";
        CString keyword;
        keyword_.GetWindowText(keyword);
        value.keyword = keyword.GetString();
        value.offset = offset_;
        return value;
    }

    bool AuctionPage::Send(Game::AuctionRequest value)
    {
        if (!Ready()) return false;
        return request(value);
    }

    void AuctionPage::Load(int offset)
    {
        auto value = Query();
        value.offset = offset;
        Send(value);
    }

    void AuctionPage::FillAssets()
    {
        if (!asset_.GetSafeHwnd()) return;
        std::string selected;
        int selectedSlot = -1;
        int current = asset_.GetCurSel();
        if (current >= 0 && static_cast<std::size_t>(current) < assets_.size())
        {
            selectedSlot = assets_[current].first;
            selected = assets_[current].second;
        }
        assets_.clear();
        assetRevisions_.clear();
        asset_.ResetContent();
        if (assetType_.GetCurSel() == 0)
        {
            for (std::size_t i = 0; i < view_.pets.size(); ++i)
            {
                const auto& pet = view_.pets[i];
                if (!pet.present || static_cast<int>(i) == view_.player.battlePet ||
                    static_cast<int>(i) == view_.player.ridePet) continue;
                CString text;
                text.Format(L"[%d] %s  Lv.%d", static_cast<int>(i) + 1,
                    LegacyText(pet.nickname.empty() ? pet.name : pet.nickname).GetString(), pet.level);
                asset_.AddString(text);
                assets_.emplace_back(static_cast<int>(i), pet.uniqueCode);
                assetRevisions_.push_back(0);
            }
        }
        else
        {
            for (std::size_t i = 5; i < view_.items.size(); ++i)
            {
                const auto& item = view_.items[i];
                if (!item.present) continue;
                CString text;
                text.Format(L"[%d] %s", static_cast<int>(i) - 4, LegacyText(item.name).GetString());
                asset_.AddString(text);
                assets_.emplace_back(static_cast<int>(i), item.uniqueCode);
                assetRevisions_.push_back(item.revision);
            }
        }
        int selection = -1;
        for (std::size_t i = 0; i < assets_.size(); ++i)
            if (assets_[i].first == selectedSlot && assets_[i].second == selected) selection = static_cast<int>(i);
        asset_.SetCurSel(selection);
        OnAsset();
    }

    void AuctionPage::OnAsset()
    {
        CString text = L"请选择出售资产；是否允许交易以上架时服务器校验为准。";
        int selected = asset_.GetCurSel();
        if (selected >= 0 && static_cast<std::size_t>(selected) < assets_.size())
        {
            int slot = assets_[selected].first;
            if (assetType_.GetCurSel() == 0 && static_cast<std::size_t>(slot) < view_.pets.size())
            {
                const auto& pet = view_.pets[slot];
                text.Format(L"等级 %d   HP %d   攻击 %d   防御 %d   敏捷 %d\r\n上架成功后由拍卖行托管。",
                    pet.level, pet.maxHp, pet.attack, pet.defence, pet.quick);
            }
            else if (assetType_.GetCurSel() == 1 && static_cast<std::size_t>(slot) < view_.items.size())
                text = LegacyText(view_.items[slot].description);
        }
        SetTextIfChanged(assetDetail_, text);
        UpdateActions();
    }

    void AuctionPage::Act(Game::AuctionAction action)
    {
        const auto* entry = Selected();
        if (!Ready() || !entry) return;
        auto value = Query();
        value.action = action;
        value.id = entry->id;
        CString question;
        if (action == Game::AuctionAction::Bid)
        {
            if (!entry->canBid) return;
            if (!ReadPrice(bidAmount_, value.amount) || value.amount < entry->minimumBid)
            {
                MessageBox(L"请输入达到最低出价的有效整数 P 金额。", L"无法出价", MB_OK | MB_ICONWARNING);
                return;
            }
            question.Format(L"以 %s 竞拍“%s”？", Price(value.amount).GetString(), LegacyText(entry->name).GetString());
        }
        else if (action == Game::AuctionAction::Buyout)
        {
            if (!entry->canBuyout) return;
            value.amount = entry->buyout;
            question.Format(L"以一口价 %s 购买“%s”？", Price(value.amount).GetString(), LegacyText(entry->name).GetString());
        }
        else if (action == Game::AuctionAction::Cancel)
        {
            if (!entry->canCancel) return;
            question = L"撤回这笔拍卖？资产将进入待领取。";
        }
        else
        {
            if (!entry->canClaim) return;
            question = L"领取选中货物或 P？空间或余额容量不足时将保留待领取。";
        }
        if (MessageBox(question, L"竞拍确认", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) Send(value);
    }

    void AuctionPage::OnCreateListing()
    {
        int selected = asset_.GetCurSel();
        if (!Ready() || selected < 0 || static_cast<std::size_t>(selected) >= assets_.size()) return;
        Game::AuctionRequest value;
        value.action = Game::AuctionAction::Create;
        value.type = assetType_.GetCurSel() == 0 ? "pet" : "item";
        value.slot = assets_[selected].first;
        value.uniqueCode = assets_[selected].second;
        value.assetRevision = assetRevisions_[selected];
        value.hours = (duration_.GetCurSel() + 1) * 4;
        if (!ReadPrice(startPrice_, value.amount) || !ReadPrice(buyoutPrice_, value.buyout, true) ||
            (value.buyout > 0 && value.buyout <= value.amount) || value.hours < 4 || value.hours > 12)
        {
            MessageBox(L"起拍价至少 1 P；一口价留空或填写大于起拍价的整数。", L"无法上架", MB_OK | MB_ICONWARNING);
            return;
        }
        CString name, question;
        asset_.GetLBText(selected, name);
        question.Format(L"上架“%s” %d 小时？\r\n起拍价 %s，一口价 %s。\r\n上架费 %s，成交费 %s。",
            name.GetString(), value.hours, Price(value.amount).GetString(),
            value.buyout ? Price(value.buyout).GetString() : L"未设置",
            Price(view_.auction.listingFee).GetString(), Price(view_.auction.saleFee).GetString());
        if (MessageBox(question, L"确认上架", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) Send(value);
    }

    void AuctionPage::OnSize(UINT type, int width, int height) { CWnd::OnSize(type, width, height); Layout(); }
    void AuctionPage::OnTab(NMHDR*, LRESULT* result)
    {
        *result = 0;
        offset_ = 0;
        hasDisplayed_ = false;
        list_.DeleteAllItems();
        displayed_.clear();
        FillAssets();
        Refresh();
        Layout();
        if (tabs_.GetCurSel() < 4) Load(0);
    }
    void AuctionPage::OnSelect(NMHDR*, LRESULT* result)
    {
        *result = 0;
        if (const auto* entry = Selected())
        {
            CString amount;
            amount.Format(L"%lld", static_cast<long long>(entry->minimumBid));
            bidAmount_.SetWindowText(amount);
        }
        UpdateActions();
    }
    void AuctionPage::OnDetail(NMHDR*, LRESULT* result)
    {
        *result = 0;
        if (const auto* entry = Selected())
        {
            auto value = Query();
            value.action = Game::AuctionAction::Detail;
            value.id = entry->listingId;
            Send(value);
        }
    }
    void AuctionPage::OnSearch() { Load(0); }
    void AuctionPage::OnPrevious() { Load(std::max(0, offset_ - 20)); }
    void AuctionPage::OnNext() { if (view_.auction.hasNext) Load(offset_ + 20); }
    void AuctionPage::OnBid() { Act(Game::AuctionAction::Bid); }
    void AuctionPage::OnBuyout() { Act(Game::AuctionAction::Buyout); }
    void AuctionPage::OnCancel() { Act(Game::AuctionAction::Cancel); }
    void AuctionPage::OnClaim() { Act(Game::AuctionAction::Claim); }
    void AuctionPage::OnAssetType() { assets_.clear(); FillAssets(); }
}
