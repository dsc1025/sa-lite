#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Game
{
    enum class AuctionPage { Browse, Selling, Bidding, Claims };
    enum class AuctionAction { List, Detail, Create, Bid, Buyout, Cancel, Claim };

    struct AuctionEntry
    {
        std::string id, listingId, type, name, seller, status, description;
        std::int64_t price = 0, buyout = 0, minimumBid = 0, remainingSeconds = 0;
        int quantity = 1, level = 0;
        bool canBid = false, canBuyout = false, canCancel = false, canClaim = false;
    };

    struct AuctionRequest
    {
        AuctionAction action = AuctionAction::List;
        AuctionPage page = AuctionPage::Browse;
        std::string id, type, uniqueCode;
        std::wstring keyword;
        std::int64_t amount = 0, buyout = 0;
        int offset = 0, slot = -1, hours = 4;
    };

    struct AuctionView
    {
        bool available = false, pending = false, received = false, hasNext = false;
        AuctionPage page = AuctionPage::Browse;
        int offset = 0, activeListings = 0, listingLimit = 5;
        std::int64_t balance = 0, listingFee = 0, saleFee = 0;
        unsigned int revision = 0;
        std::wstring message = L"竞拍服务尚未连接";
        std::vector<AuctionEntry> entries;
    };
}
