#pragma once
#include <string>
#include <vector>
#include <utility>
#include <memory>

#include <quill/Logger.h>

#include "common/market_msg_types.hpp"
#include "config_handlers/signer.hpp"

namespace Omni::Binance {

struct DepthSnapshot {
    bool ok = false;
    long last_update_id = 0;
    std::vector<std::pair<double, double>> bids;
    std::vector<std::pair<double, double>> asks;
};

// REST helpers for bootstrapping/resyncing state that the websockets don't push
// on (re)connect: order-book depth snapshots, position & open-order snapshots,
// and the user-data-stream listenKey lifecycle.
class SnapshotFetcher {
    public:
        SnapshotFetcher(
            quill::Logger* logger,
            const std::string& rest_domain,
            std::shared_ptr<Omni::Config::ISigner> signer
        );

        DepthSnapshot fetch_depth(const std::string& symbol, int limit = 1000);

        // Authoritative position snapshot (GET /fapi/v2/positionRisk).
        std::vector<Omni::PositionMsg> fetch_positions();

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

        std::string signed_get(const std::string& path, const std::string& extra_params = "");
};

} // namespace Omni::Binance
