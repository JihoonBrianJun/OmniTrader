#pragma once

#include <string>
#include <variant>

#include "common/market_msg_types.hpp"

// Task types produced by the Connection clients that consume an internal feed. They
// live here (rather than in the trader) because more than one executable reads an
// internal feed: the trader consumes the listener's market/user feed *and* the
// pricer's fair-price feed, while the pricer consumes the listener's market feed.
//
// The two links have separate task types on purpose. A consumer that holds both (the
// trader) puts them on one queue and tells them apart by type, so the dispatch is a
// std::visit over distinct alternatives rather than a source tag tested inside shared
// handlers -- and a consumer that holds only one (the pricer) never carries
// alternatives it cannot receive.

namespace Omni {

// ---- Listener link: normalized market and user data -------------------------

struct ListenerStatusUpdate {
    bool connecting = false;
    bool connected = false;
};

struct ListenerSubscribeUpdate {
    bool subscribe = false;
    bool success = false;
    std::string product = "";
};

struct MarketDataResponse {
    enum Feed {
        Orderbook,
        Trade,
        Execution,
        Position,
        ProductInfo,
        Error
    } feed;
    std::string product;   // for an asset position this carries the asset (e.g. "USDC")
    std::variant<
        OrderbookData, TradeData, ExecutionData, PositionData, ProductInfoData
    > data;
};

// ---- Pricer link: fair price ------------------------------------------------

struct PricerStatusUpdate {
    bool connecting = false;
    bool connected = false;
};

struct PricerSubscribeUpdate {
    bool subscribe = false;
    bool success = false;
    std::string product = "";
};

struct FairPriceResponse {
    std::string product;
    FairPriceData data;
};

} // namespace Omni
