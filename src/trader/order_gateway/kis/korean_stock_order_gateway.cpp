#include <fmt/core.h>
#include <glaze/glaze.hpp>
#include <quill/LogMacros.h>

#include "korean_stock_order_dtypes.hpp"
#include "korean_stock_order_gateway.hpp"


namespace Omni::KIS::OrderGateway {

KoreanStockOrderGateway::KoreanStockOrderGateway(quill::Logger* logger, const std::string& http_domain)
:   BaseOrderGateway(logger, http_domain)
{
    initalize();
}


std::string KoreanStockOrderGateway::convert_code_for_order(const std::string& code) {
    return code;
}


void KoreanStockOrderGateway::write_balance_url() {
    balance_url_ = fmt::format("{}/uapi/domestic-stock/v1/trading/inquire-balance", http_domain_);
}


void KoreanStockOrderGateway::write_order_place_url() {
    order_place_url_ = fmt::format("{}/uapi/domestic-stock/v1/trading/order-cash", http_domain_);
}


void KoreanStockOrderGateway::write_order_change_url() {
    order_change_url_ = fmt::format("{}/uapi/domestic-stock/v1/trading/order-rvsecncl", http_domain_);
}


void KoreanStockOrderGateway::write_balance_header() {
    write_rest_header(balance_header_, "TTTS3012R");
}


void KoreanStockOrderGateway::write_order_place_header(bool /*is_bid*/) {
    write_rest_header(bid_order_place_header_, "TTTC8434R");
    write_rest_header(ask_order_place_header_, "TTTC8434R");
}


void KoreanStockOrderGateway::write_order_change_header() {
    write_rest_header(order_change_header_, "TTTC0803U");
}


void KoreanStockOrderGateway::write_balance_params_list() {
    balance_params_list_.clear();
    std::string json_buffer;
    auto params = BalanceParams{
        .CANO = account_info_.front,
        .ACNT_PRDT_CD = account_info_.rear,
        .AFHR_FLPR_YN = "N",
        .OFL_YN = "",
        .INQR_DVSN = "02",
        .UNPR_DVSN = "01",
        .FUND_STTL_ICLD_YN = "N",
        .FNCG_AMT_AUTO_RDPT_YN = "N",
        .PRCS_DVSN = "00",
        .CTX_AREA_FK100 = "",
        .CTX_AREA_NK100 = ""
    };
    auto ec = glz::write_json(params, json_buffer);
    if (ec) {
        LOG_WARNING(logger_, "Failed write_json while building balance params");
    } else {
        balance_params_list_.emplace_back(json_buffer);
    }
}


void KoreanStockOrderGateway::write_order_place_params(
    std::string& order_place_params, const OG::OrderPlaceInfo& order_place_info
) {
    auto params = OrderPlaceParams{
        .CANO = account_info_.front,
        .ACNT_PRDT_CD = account_info_.rear,
        .PDNO = convert_code_for_order(order_place_info.code),
        .ORD_DVSN = order_place_info.is_limit ? "00" : "01",
        .ORD_QTY = double_to_string(order_place_info.qty),
        .ORD_UNPR = double_to_string(order_place_info.price)
    };
    auto ec = glz::write_json(params, order_place_params);
    if (ec) {
        LOG_WARNING(logger_, "Failed write_json while building order place params");
    }
}


void KoreanStockOrderGateway::write_order_amend_params(
    std::string& order_amend_params, const OG::OrderAmendInfo& order_amend_info
) {
    auto params = OrderAmendParams{
        .CANO = account_info_.front,
        .ACNT_PRDT_CD = account_info_.rear,
        .KRX_FWDG_ORD_ORGNO = "",
        .ORGN_ODNO = order_amend_info.order_no,
        .ORD_DVSN = order_amend_info.is_limit ? "00" : "01",
        .RVSE_CNCL_DVSN_CD = "01",
        .ORD_QTY = double_to_string(order_amend_info.qty),
        .ORD_UNPR = double_to_string(order_amend_info.price),
        .QTY_ALL_ORD_YN = order_amend_info.amend_all ? "Y" : "N"
    };
    auto ec = glz::write_json(params, order_amend_params);
    if (ec) {
        LOG_WARNING(logger_, "Failed write_json while building order amend params");
    }
}


void KoreanStockOrderGateway::write_order_cancel_params(
    std::string& order_cancel_params, const OG::OrderCancelInfo& order_cancel_info
) {
    auto params = OrderCancelParams{
        .CANO = account_info_.front,
        .ACNT_PRDT_CD = account_info_.rear,
        .KRX_FWDG_ORD_ORGNO = "",
        .ORGN_ODNO = order_cancel_info.order_no,
        .ORD_DVSN = order_cancel_info.is_limit ? "00" : "01",
        .RVSE_CNCL_DVSN_CD = "02",
        .ORD_QTY = double_to_string(order_cancel_info.qty),
        .ORD_UNPR = "0",
        .QTY_ALL_ORD_YN = order_cancel_info.cancel_all ? "Y" : "N"
    };
    auto ec = glz::write_json(params, order_cancel_params);
    if (ec) {
        LOG_WARNING(logger_, "Failed write_json while building order cancel params");
    }
}


void KoreanStockOrderGateway::parse_order_response(
    const std::string& order_response, OG::OrderResponse& parsed_response
) {
    OrderResponseBody order_response_body;
    auto ec = glz::read_json(order_response_body, order_response);
    if (ec) {
        LOG_WARNING(logger_, "Failed to parse order response ({})", order_response);
        return;
    }

    parsed_response.success = (order_response_body.rt_cd == "0");
    parsed_response.code = order_response_body.msg_cd;
    parsed_response.msg = order_response_body.msg1;
    if (!order_response_body.output.empty()) {
        parsed_response.order_no = order_response_body.output[0].ODNO;
    }
}

} // namespace Omni::KIS::OrderGateway
