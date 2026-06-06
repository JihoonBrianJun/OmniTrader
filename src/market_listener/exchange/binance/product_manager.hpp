#pragma once
#include <string>
#include <map>
#include <quill/Logger.h>

namespace Omni::Binance {

struct ProductFilter {
    double tick_size = 0.0;   // PRICE_FILTER tickSize
    double step_size = 0.0;   // LOT_SIZE stepSize
    int price_precision = 8;
    int qty_precision = 8;
};

// Loads GET /fapi/v1/exchangeInfo and exposes per-product tick/step filters plus
// price/quantity rounding & formatting helpers.
class ProductManager {
    public:
        ProductManager(quill::Logger* logger, const std::string& rest_domain);

        void load();
        bool has(const std::string& product) const;
        ProductFilter filter(const std::string& product) const;

        double round_price(const std::string& product, double price) const;
        double round_qty(const std::string& product, double qty) const;
        std::string format_price(const std::string& product, double price) const;
        std::string format_qty(const std::string& product, double qty) const;

    private:
        quill::Logger* logger_;
        std::string rest_domain_;
        std::map<std::string, ProductFilter> filters_;
};

} // namespace Omni::Binance
