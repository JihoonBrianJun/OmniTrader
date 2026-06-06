#include <cmath>
#include "pricer.hpp"

namespace Omni::Trader {

Pricer::Pricer(double min_tick_size, double lot_size)
:   min_tick_size_(min_tick_size),
    lot_size_(lot_size)
{
}


double Pricer::to_double_price(int64_t price_in_min_ticks) {
    return static_cast<double>(price_in_min_ticks) * min_tick_size_;
}


double Pricer::to_double_qty(int32_t qty_in_lots) {
    return static_cast<double>(qty_in_lots) * lot_size_;
}


double Pricer::compute_mid_price(
    int64_t bbid_price_in_min_ticks, int64_t bask_price_in_min_ticks
) {
    return to_double_price(bbid_price_in_min_ticks + bask_price_in_min_ticks) / 2;
}


void Pricer::fetch_mid_price(const L1& l1, PriceInfo& price_info) {
    price_info.bbid_price_in_min_ticks = l1.bbid_price_in_min_ticks;
    price_info.bask_price_in_min_ticks = l1.bask_price_in_min_ticks;

    if ((l1.bbid_price_in_min_ticks == -1) || (l1.bask_price_in_min_ticks == -1)) {
        price_info.mid_price = NAN;
    } else {
        price_info.mid_price = compute_mid_price(
            l1.bbid_price_in_min_ticks, l1.bask_price_in_min_ticks
        );
    }

    price_info.fair_price = price_info.mid_price;
}


void Pricer::fetch_vwap(const L1& l1, PriceInfo& price_info) {
    price_info.bbid_price_in_min_ticks = l1.bbid_price_in_min_ticks;
    price_info.bask_price_in_min_ticks = l1.bask_price_in_min_ticks;

    if ((l1.bbid_price_in_min_ticks == -1) || (l1.bask_price_in_min_ticks == -1)) {
        price_info.mid_price = NAN;
        price_info.fair_price = NAN;
        return;
    }

    price_info.mid_price = compute_mid_price(
        l1.bbid_price_in_min_ticks, l1.bask_price_in_min_ticks
    );

    auto bbid_qty = to_double_qty(l1.bbid_qty_in_lots);
    auto bask_qty = to_double_qty(l1.bask_qty_in_lots);
    price_info.fair_price = (
        to_double_price(l1.bbid_price_in_min_ticks) * bask_qty +
        to_double_price(l1.bask_price_in_min_ticks) * bbid_qty
    ) / (bbid_qty + bask_qty);
}

} // namespace Omni::Trader
