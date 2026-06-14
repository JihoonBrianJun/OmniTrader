#pragma once
#include <string>
#include <vector>
#include <map>
#include <utility>
#include <memory>

#include <quill/Logger.h>

#include "common/market_msg_types.hpp"
#include "config_handlers/signer.hpp"

namespace Omni::Binance {

struct ProductFilter {
    double tick_size = 0.0;   // PRICE_FILTER tickSize
    double step_size = 0.0;   // LOT_SIZE stepSize
    int price_precision = 8;
    int qty_precision = 8;
};

struct DepthSnapshot {
    bool ok = false;
    long last_update_id = 0;
    std::vector<std::pair<double, double>> bids;
    std::vector<std::pair<double, double>> asks;
};

// Single Binance REST surface. Combines exchangeInfo product filters (tick/step
// rounding & formatting) with the REST snapshot/listenKey helpers that bootstrap
// or resync state the websockets don't push on (re)connect.
class BinanceRestClient {
    public:
        BinanceRestClient(
            quill::Logger* logger,
            const std::string& rest_domain,
            std::shared_ptr<Omni::Config::ISigner> signer = nullptr
        );

        // --- exchangeInfo product filters ---
        void load();   // GET /fapi/v1/exchangeInfo -> per-product tick/step filters
        bool has(const std::string& product) const;
        ProductFilter filter(const std::string& product) const;

        double round_price(const std::string& product, double price) const;
        double round_qty(const std::string& product, double qty) const;
        std::string format_price(const std::string& product, double price) const;
        std::string format_qty(const std::string& product, double qty) const;

        // --- REST snapshots / listenKey lifecycle ---
        DepthSnapshot fetch_depth(const std::string& product, int limit = 1000);

        // Authoritative position snapshot (GET /fapi/v2/positionRisk).
        std::vector<Omni::PositionMsg> fetch_positions();

        // Authoritative per-asset balance snapshot (GET /fapi/v2/balance).
        std::vector<Omni::BalanceMsg> fetch_balances();

        // Open orders rebuilt as accept-style execution events (GET /fapi/v1/openOrders).
        std::vector<Omni::ExecutionMsg> fetch_open_orders();

        // listenKey lifecycle for the user-data stream.
        std::string create_listen_key();
        void keepalive_listen_key();
        void close_listen_key();

    private:
        quill::Logger* logger_;
        std::string rest_domain_;
        std::shared_ptr<Omni::Config::ISigner> signer_;
        std::map<std::string, ProductFilter> filters_;

        std::string signed_get(const std::string& path, const std::string& extra_params = "");
};

} // namespace Omni::Binance
