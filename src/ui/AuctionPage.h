#pragma once

#include <afxwin.h>
#include <afxcmn.h>
#include <functional>
#include <utility>
#include "../game/AuctionTypes.h"
#include "PropertiesWindow.h"

namespace Client
{
    struct SessionView;

    class AuctionPage : public CWnd
    {
    public:
        explicit AuctionPage(const SessionView& view) : view_(view) {}
        bool Create(CWnd* owner, CFont* font);
        void Refresh();
        void Activate();
        std::function<bool(const Game::AuctionRequest&)> request;

    protected:
        afx_msg void OnSize(UINT type, int width, int height);
        afx_msg void OnTab(NMHDR*, LRESULT* result);
        afx_msg void OnSelect(NMHDR*, LRESULT* result);
        afx_msg void OnDetail(NMHDR*, LRESULT* result);
        afx_msg void OnSearch();
        afx_msg void OnPrevious();
        afx_msg void OnNext();
        afx_msg void OnBid();
        afx_msg void OnBuyout();
        afx_msg void OnCancel();
        afx_msg void OnClaim();
        afx_msg void OnCreateListing();
        afx_msg void OnAssetType();
        afx_msg void OnAsset();
        DECLARE_MESSAGE_MAP()

    private:
        void Layout();
        void UpdateActions();
        void FillAssets();
        void Load(int offset);
        bool Send(Game::AuctionRequest value);
        const Game::AuctionEntry* Selected() const;
        bool Ready() const;
        Game::AuctionRequest Query() const;
        void Act(Game::AuctionAction action);

        const SessionView& view_;
        CTabCtrl tabs_;
        CListCtrl list_;
        CComboBox filter_, assetType_, asset_, duration_;
        CEdit keyword_, bidAmount_, startPrice_, buyoutPrice_;
        CButton search_, previous_, next_, bid_, buyout_, cancel_, claim_, submit_;
        CStatic pagination_, bidLabel_, labels_[5], rules_, assetDetail_;
        ItemPropertiesWindow detail_;
        std::vector<std::pair<int, std::string>> assets_;
        std::vector<std::uint32_t> assetRevisions_;
        std::vector<Game::AuctionEntry> displayed_;
        unsigned int displayedRevision_ = 0;
        unsigned int assetRevision_ = 0, detailRevision_ = 0;
        bool hasDisplayed_ = false;
        int offset_ = 0;
    };
}
