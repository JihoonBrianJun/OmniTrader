#include <sstream>
#include <iomanip>
#include <cmath>
#include <fmt/core.h>
#include <quill/LogMacros.h>

#include "config_handlers/config_utils.hpp"
#include "connection_handlers/rest/rest_client.hpp"
#include "base_order_gateway.hpp"


namespace Omni::KIS::OrderGateway {

// Whole-transfer cap for an order request. Much tighter than the RestClient
// default: the trader is waiting on the reply and the shutdown drain waits on any
// request still running, so a stuck order has to fail fast rather than hang.
static constexpr long ORDER_REST_TIMEOUT_SEC = 5;


BaseOrderGateway::BaseOrderGateway(quill::Logger* logger, const std::string& http_domain)
:   logger_(logger),
    runner_(logger, "kis rest order gateway"),
    account_info_(Config::get_account_info().second),
    http_domain_(http_domain)
{
}


BaseOrderGateway::~BaseOrderGateway() {
    // Last resort only -- by now parse_order_response resolves to the pure virtual.
    // The concrete gateway is expected to have drained already; see the header.
    if (runner_.pending() > 0) {
        LOG_ERROR(
            logger_,
            "{} KIS order request(s) still queued in ~BaseOrderGateway: the concrete "
            "gateway did not call drain_pending()",
            runner_.pending()
        );
    }
    runner_.drain();
}


void BaseOrderGateway::initalize() {
    write_balance_url();
    write_order_place_url();
    write_order_change_url();

    write_common_rest_header();
    write_balance_header();
    write_order_place_header(true);
    write_order_place_header(false);
    write_order_change_header();

    write_balance_params_list();
}


void BaseOrderGateway::write_common_rest_header() {
    auto auth_keys_info = Omni::Config::get_auth_keys(Config::EXCHANGE);
    if (!auth_keys_info.first) {
        LOG_WARNING(logger_, "Failed to get app_key and app_secret");
        return;
    }

    auto rest_access_token_info = Config::get_rest_access_token();
    if (!rest_access_token_info.first) {
        LOG_WARNING(logger_, "Failed to get rest access token");
        return;
    }

    common_rest_header_["content-type"] = "application/json";
    common_rest_header_["authorization"] = fmt::format("Bearer {}", rest_access_token_info.second);
    common_rest_header_["appkey"] = auth_keys_info.second.key;
    common_rest_header_["appsecret"] = auth_keys_info.second.secret;
}


void BaseOrderGateway::write_rest_header(
    std::map<std::string, std::string>& rest_header, const std::string& tr_id
) {
    rest_header = common_rest_header_;
    rest_header["tr_id"] = tr_id;
}


void BaseOrderGateway::get_balances(std::vector<std::string>& balance_responses) {
    for (const auto& balance_params : balance_params_list_) {
        auto client = std::make_shared<Omni::Connection::RestClient>(logger_);
        auto response = client->post(balance_url_, balance_params, balance_header_);

        if (response.success && (response.status_code == 200)) {
            balance_responses.emplace_back(response.body);
        } else {
            LOG_WARNING(
                logger_, "Getting balance failed: {} {} {}",
                response.error_msg, response.status_code, response.body
            );
            balance_responses.emplace_back("");
        }
    }
}


void BaseOrderGateway::deliver_dispatch_failure(uint64_t cid) {
    OG::OrderResponse response;
    response.cid = cid;
    response.success = false;
    response.msg = "KIS REST order could not be queued";
    deliver(response);
}


// KIS is REST-only, so every order is a blocking HTTP round trip. Each one is queued
// on the gateway's single worker and the parsed response (tagged with the order's cid)
// is delivered through the sink from there, which is what the IOrderGateway contract
// describes. One worker means the venue sees the requests in the order they were made.
//
// The params and headers are built here, on the caller's thread, and captured by
// value: they read members that the caller still owns, and the task outlives the call.
bool BaseOrderGateway::send_order_request(
    const std::string& what, std::string url, std::string params,
    std::map<std::string, std::string> headers, uint64_t cid
) {
    bool queued = runner_.post(
        [this, what, url = std::move(url), params = std::move(params),
         headers = std::move(headers), cid]() {
            auto client = std::make_shared<Omni::Connection::RestClient>(logger_);
            client->set_timeout_sec(ORDER_REST_TIMEOUT_SEC);
            auto response = client->post(url, params, headers);

            OG::OrderResponse order_response;
            order_response.cid = cid;
            if (response.success && (response.status_code == 200)) {
                parse_order_response(response.body, order_response);
            } else {
                LOG_WARNING(
                    logger_, "{} order failed: {} {} {}",
                    what, response.error_msg, response.status_code, response.body
                );
                order_response.msg = response.body;
            }
            deliver(order_response);
        }
    );

    if (!queued) {
        deliver_dispatch_failure(cid);
        return false;
    }
    return true;
}


bool BaseOrderGateway::place_order(const OG::OrderPlaceInfo& order_place_info) {
    std::string order_place_params;
    write_order_place_params(order_place_params, order_place_info);
    return send_order_request(
        "Place", order_place_url_, std::move(order_place_params),
        order_place_info.is_bid ? bid_order_place_header_ : ask_order_place_header_,
        order_place_info.cid
    );
}


bool BaseOrderGateway::amend_order(const OG::OrderAmendInfo& order_amend_info) {
    std::string order_amend_params;
    write_order_amend_params(order_amend_params, order_amend_info);
    return send_order_request(
        "Amend", order_change_url_, std::move(order_amend_params),
        order_change_header_, order_amend_info.cid
    );
}


bool BaseOrderGateway::cancel_order(const OG::OrderCancelInfo& order_cancel_info) {
    std::string order_cancel_params;
    write_order_cancel_params(order_cancel_params, order_cancel_info);
    return send_order_request(
        "Cancel", order_change_url_, std::move(order_cancel_params),
        order_change_header_, order_cancel_info.cid
    );
}


std::string BaseOrderGateway::double_to_string(double value) {
    if (std::isnan(value) || std::isinf(value)) {
        return std::to_string(value);
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(15) << value;
    std::string result = oss.str();

    size_t decimal_pos = result.find('.');
    if (decimal_pos == std::string::npos) {
        return result;
    }

    size_t last_non_zero = result.find_last_not_of('0');
    if (last_non_zero == decimal_pos) {
        result.erase(decimal_pos);
    } else if (last_non_zero != std::string::npos) {
        result.erase(last_non_zero + 1);
    }

    return result;
}

} // namespace Omni::KIS::OrderGateway
