#pragma once
#include <string>
#include <map>
#include <quill/Logger.h>

namespace Omni::Binance {

struct SymbolFilter {
    double tick_size = 0.0;   // PRICE_FILTER tickSize
    double step_size = 0.0;   // LOT_SIZE stepSize
    int price_precision = 8;
    int qty_precision = 8;
};

// Loads GET /fapi/v1/exchangeInfo and exposes per-symbol tick/step filters plus
// price/quantity rounding & formatting helpers.
class SymbolManager {
    public:
        SymbolManager(quill::Logger* logger, const std::string& rest_domain);

        void load();
        bool has(const std::string& symbol) const;
        SymbolFilter filter(const std::string& symbol) const;

        double round_price(const std::string& symbol, double price) const;
        double round_qty(const std::string& symbol, double qty) const;
        std::string format_price(const std::string& symbol, double price) const;
        std::string format_qty(const std::string& symbol, double qty) const;

    private:
        quill::Logger* logger_;
        std::string rest_domain_;
        std::map<std::string, SymbolFilter> filters_;
};

} // namespace Omni::Binance
