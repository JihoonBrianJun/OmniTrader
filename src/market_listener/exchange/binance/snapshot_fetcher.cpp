#include <fmt/core.h>
#include <quill/LogMacros.h>
#include <nlohmann/json.hpp>

#include "utils/datetime.hpp"
#include "connection_handlers/http/http_client.hpp"
#include "market_listener/exchange/binance/binance_common.hpp"
#include "snapshot_fetcher.hpp"


namespace Omni::Binance {

SnapshotFetcher::SnapshotFetcher(
    quill::Logger* logger,
    const std::string& rest_domain,
    std::shared_ptr<Omni::Config::ISigner> signer
)
:   logger_(logger),
    rest_domain_(rest_domain),
    signer_(std::move(signer))
{
}


std::string SnapshotFetcher::signed_get(const std::string& path, const std::string& extra_params) {
    if (!signer_) {
        LOG_WARNING(logger_, "No signer configured for signed request {}", path);
        return "";
    }
    std::string params = fmt::format(
        "timestamp={}&recvWindow={}", get_curr_tstamp_ms(), DEFAULT_RECV_WINDOW
    );
    if (!extra_params.empty()) params = extra_params + "&" + params;

    auto url = fmt::format("{}{}?{}", rest_domain_, path, sign_query(*signer_, params));
    auto client = std::make_shared<Omni::Connection::HttpClient>(logger_);
    auto response = client->get(url, auth_header(*signer_));
    if (!response.success || response.status_code != 200) {
        LOG_WARNING(
            logger_, "Signed GET {} failed: {} {} {}",
            path, response.error_msg, response.status_code, response.body
        );
        return "";
    }
    return response.body;
}


DepthSnapshot SnapshotFetcher::fetch_depth(const std::string& product, int limit) {
    DepthSnapshot snapshot;
    auto url = fmt::format("{}/fapi/v1/depth?symbol={}&limit={}", rest_domain_, product, limit);
    auto client = std::make_shared<Omni::Connection::HttpClient>(logger_);
    auto response = client->get(url);
    if (!response.success || response.status_code != 200) {
        LOG_WARNING(
            logger_, "Depth snapshot for {} failed: {} {} {}",
            product, response.error_msg, response.status_code, response.body
        );
        return snapshot;
    }

    try {
        auto j = nlohmann::json::parse(response.body);
        snapshot.last_update_id = j.at("lastUpdateId").get<long>();
        for (const auto& level : j.at("bids")) {
            snapshot.bids.emplace_back(
                std::stod(level[0].get<std::string>()), std::stod(level[1].get<std::string>())
            );
        }
        for (const auto& level : j.at("asks")) {
            snapshot.asks.emplace_back(
                std::stod(level[0].get<std::string>()), std::stod(level[1].get<std::string>())
            );
        }
        snapshot.ok = true;
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Failed to parse depth snapshot for {}: {}", product, e.what());
    }
    return snapshot;
}


std::vector<Omni::PositionMsg> SnapshotFetcher::fetch_positions() {
    std::vector<Omni::PositionMsg> positions;
    auto body = signed_get("/fapi/v2/positionRisk");
    if (body.empty()) return positions;

    try {
        auto j = nlohmann::json::parse(body);
        for (const auto& p : j) {
            double amt = std::stod(p.at("positionAmt").get<std::string>());
            if (amt == 0.0) continue;
            Omni::PositionMsg msg;
            msg.product = p.at("symbol").get<std::string>();
            msg.position_data.position_amt = amt;
            msg.position_data.entry_price = std::stod(p.at("entryPrice").get<std::string>());
            msg.position_data.unrealized_pnl = std::stod(p.at("unRealizedProfit").get<std::string>());
            positions.push_back(std::move(msg));
        }
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Failed to parse positionRisk: {}", e.what());
    }
    return positions;
}


std::vector<Omni::BalanceMsg> SnapshotFetcher::fetch_balances() {
    std::vector<Omni::BalanceMsg> balances;
    auto body = signed_get("/fapi/v2/balance");
    if (body.empty()) return balances;

    try {
        auto j = nlohmann::json::parse(body);
        for (const auto& b : j) {
            Omni::BalanceMsg msg;
            msg.asset = b.at("asset").get<std::string>();
            msg.balance_data.wallet_balance = std::stod(b.at("balance").get<std::string>());
            msg.balance_data.available_balance = std::stod(b.at("availableBalance").get<std::string>());
            balances.push_back(std::move(msg));
        }
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Failed to parse balance: {}", e.what());
    }
    return balances;
}


std::vector<Omni::ExecutionMsg> SnapshotFetcher::fetch_open_orders() {
    std::vector<Omni::ExecutionMsg> orders;
    auto body = signed_get("/fapi/v1/openOrders");
    if (body.empty()) return orders;

    try {
        auto j = nlohmann::json::parse(body);
        for (const auto& o : j) {
            Omni::ExecutionMsg msg;
            msg.product = o.at("symbol").get<std::string>();
            auto& d = msg.execution_data;
            d.order_no = std::to_string(o.at("orderId").get<long>());
            d.is_accept_data = true;
            d.is_place = true;
            d.is_accepted = true;
            d.is_bid = (o.at("side").get<std::string>() == "BUY");
            d.order_price = std::stod(o.at("price").get<std::string>());
            double orig = std::stod(o.at("origQty").get<std::string>());
            double exec = std::stod(o.at("executedQty").get<std::string>());
            d.order_qty = orig - exec;   // remaining open quantity
            orders.push_back(std::move(msg));
        }
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Failed to parse openOrders: {}", e.what());
    }
    return orders;
}


std::string SnapshotFetcher::create_listen_key() {
    if (!signer_) return "";
    auto url = fmt::format("{}/fapi/v1/listenKey", rest_domain_);
    auto client = std::make_shared<Omni::Connection::HttpClient>(logger_);
    auto response = client->post(url, "", auth_header(*signer_));
    if (!response.success || response.status_code != 200) {
        LOG_WARNING(
            logger_, "create listenKey failed: {} {} {}",
            response.error_msg, response.status_code, response.body
        );
        return "";
    }
    try {
        auto j = nlohmann::json::parse(response.body);
        return j.at("listenKey").get<std::string>();
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Failed to parse listenKey response: {}", e.what());
        return "";
    }
}


void SnapshotFetcher::keepalive_listen_key() {
    if (!signer_) return;
    auto url = fmt::format("{}/fapi/v1/listenKey", rest_domain_);
    auto client = std::make_shared<Omni::Connection::HttpClient>(logger_);
    auto response = client->put(url, "", auth_header(*signer_));
    if (!response.success || response.status_code != 200) {
        LOG_WARNING(
            logger_, "keepalive listenKey failed: {} {} {}",
            response.error_msg, response.status_code, response.body
        );
    }
}


void SnapshotFetcher::close_listen_key() {
    if (!signer_) return;
    auto url = fmt::format("{}/fapi/v1/listenKey", rest_domain_);
    auto client = std::make_shared<Omni::Connection::HttpClient>(logger_);
    client->del(url, auth_header(*signer_));
}

} // namespace Omni::Binance
