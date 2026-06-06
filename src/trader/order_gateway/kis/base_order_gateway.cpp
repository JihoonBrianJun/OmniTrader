#include <sstream>
#include <iomanip>
#include <cmath>
#include <fmt/core.h>
#include <quill/LogMacros.h>

#include "config_handlers/config_utils.hpp"
#include "connection_handlers/http/http_client.hpp"
#include "base_order_gateway.hpp"


namespace Omni::KIS::OrderGateway {

BaseOrderGateway::BaseOrderGateway(quill::Logger* logger, const std::string& http_domain)
:   logger_(logger),
    account_info_(Config::get_account_info().second),
    http_domain_(http_domain)
{
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
        auto client = std::make_shared<Omni::Connection::HttpClient>(logger_);
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


void BaseOrderGateway::place_order(
    const OG::OrderPlaceInfo& order_place_info, OG::OrderResponse& place_order_response
) {
    std::string order_place_params;
    write_order_place_params(order_place_params, order_place_info);

    auto client = std::make_shared<Omni::Connection::HttpClient>(logger_);
    auto response = client->post(
        order_place_url_, order_place_params,
        order_place_info.is_bid ? bid_order_place_header_ : ask_order_place_header_
    );

    if (response.success && (response.status_code == 200)) {
        parse_order_response(response.body, place_order_response);
    } else {
        LOG_WARNING(
            logger_, "Place order failed: {} {} {}",
            response.error_msg, response.status_code, response.body
        );
    }
}


void BaseOrderGateway::amend_order(
    const OG::OrderAmendInfo& order_amend_info, OG::OrderResponse& amend_order_response
) {
    std::string order_amend_params;
    write_order_amend_params(order_amend_params, order_amend_info);

    auto client = std::make_shared<Omni::Connection::HttpClient>(logger_);
    auto response = client->post(order_change_url_, order_amend_params, order_change_header_);

    if (response.success && (response.status_code == 200)) {
        parse_order_response(response.body, amend_order_response);
    } else {
        LOG_WARNING(
            logger_, "Amend order failed: {} {} {}",
            response.error_msg, response.status_code, response.body
        );
    }
}


void BaseOrderGateway::cancel_order(
    const OG::OrderCancelInfo& order_cancel_info, OG::OrderResponse& cancel_order_response
) {
    std::string order_cancel_params;
    write_order_cancel_params(order_cancel_params, order_cancel_info);

    auto client = std::make_shared<Omni::Connection::HttpClient>(logger_);
    auto response = client->post(order_change_url_, order_cancel_params, order_change_header_);

    if (response.success && (response.status_code == 200)) {
        parse_order_response(response.body, cancel_order_response);
    } else {
        LOG_WARNING(
            logger_, "Cancel order failed: {} {} {}",
            response.error_msg, response.status_code, response.body
        );
    }
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
