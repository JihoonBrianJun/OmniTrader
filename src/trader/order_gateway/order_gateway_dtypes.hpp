#pragma once
#include <string>

namespace Omni::OrderGateway {

struct OrderPlaceInfo {
    bool is_limit;
    bool is_bid;
    double price;
    double qty;
    std::string code;
};

struct OrderAmendInfo {
    std::string order_no;
    bool amend_all = false;
    bool is_limit = true;
    double price = 0.0;
    double qty = 0.0;
    std::string code = "";
};

struct OrderCancelInfo {
    std::string order_no;
    bool cancel_all = true;
    bool is_limit = true;
    double qty = 0.0;
    std::string code = "";
};

struct OrderResponse {
    bool success = false;
    std::string code = "";
    std::string msg = "";
    std::string order_no = "";
};

} // namespace Omni::OrderGateway
