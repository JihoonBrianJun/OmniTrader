#include "order_book.hpp"

namespace Omni::Binance {

void OrderBook::apply_side(
    std::map<double, double, std::greater<double>>& book,
    const std::vector<PriceLevel>& levels
) {
    for (const auto& [price, qty] : levels) {
        if (qty <= 0.0) book.erase(price);
        else book[price] = qty;
    }
}


void OrderBook::apply_side(
    std::map<double, double>& book,
    const std::vector<PriceLevel>& levels
) {
    for (const auto& [price, qty] : levels) {
        if (qty <= 0.0) book.erase(price);
        else book[price] = qty;
    }
}


void OrderBook::apply_snapshot(
    long last_update_id,
    const std::vector<PriceLevel>& bids,
    const std::vector<PriceLevel>& asks
) {
    bids_.clear();
    asks_.clear();
    for (const auto& [price, qty] : bids) {
        if (qty > 0.0) bids_[price] = qty;
    }
    for (const auto& [price, qty] : asks) {
        if (qty > 0.0) asks_[price] = qty;
    }
    last_update_id_ = last_update_id;
    synced_ = true;
}


bool OrderBook::apply_diff(
    long U, long u, long pu,
    const std::vector<PriceLevel>& bids,
    const std::vector<PriceLevel>& asks
) {
    if (!synced_) return false;

    // Drop events fully older than the snapshot.
    if (u < last_update_id_) return true;

    // First event after snapshot must straddle lastUpdateId+1; subsequent events
    // must chain via pu == previous u (Binance USDⓈ-M futures rule).
    bool first_ok = (U <= last_update_id_ + 1) && (last_update_id_ + 1 <= u);
    bool chain_ok = (pu == last_update_id_);
    if (!first_ok && !chain_ok) {
        synced_ = false;
        return false;
    }

    apply_side(bids_, bids);
    apply_side(asks_, asks);
    last_update_id_ = u;
    return true;
}


Omni::OrderbookData OrderBook::to_data(int levels) const {
    Omni::OrderbookData data;
    int cnt = 0;
    for (const auto& [price, qty] : bids_) {
        if (cnt++ >= levels) break;
        data.bid_book.push_back({price, qty});
    }
    cnt = 0;
    for (const auto& [price, qty] : asks_) {
        if (cnt++ >= levels) break;
        data.ask_book.push_back({price, qty});
    }
    return data;
}

} // namespace Omni::Binance
