#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <utility>

#include "common/market_msg_types.hpp"

namespace Omni::Binance {

// Maintains one symbol's full order book from a REST snapshot + diff depth
// stream, following Binance's U/u/pu sequencing rules. Out-of-sequence updates
// flag the book as desynced so the listener can refetch a snapshot.
class OrderBook {
    public:
        using PriceLevel = std::pair<double, double>;  // {price, qty}

        void apply_snapshot(long last_update_id, const std::vector<PriceLevel>& bids,
                            const std::vector<PriceLevel>& asks);

        // Returns true if applied in-sequence; false means the book desynced and
        // a fresh snapshot is required.
        bool apply_diff(long U, long u, long pu,
                        const std::vector<PriceLevel>& bids,
                        const std::vector<PriceLevel>& asks);

        bool synced() const { return synced_; }
        void mark_desynced() { synced_ = false; }

        Omni::OrderbookData to_data(int levels) const;

    private:
        bool synced_ = false;
        long last_update_id_ = 0;
        std::map<double, double, std::greater<double>> bids_;
        std::map<double, double> asks_;

        static void apply_side(std::map<double, double, std::greater<double>>& book,
                               const std::vector<PriceLevel>& levels);
        static void apply_side(std::map<double, double>& book,
                               const std::vector<PriceLevel>& levels);
};

} // namespace Omni::Binance
