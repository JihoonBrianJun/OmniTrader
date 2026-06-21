#pragma once
#include <string>
#include <map>
#include <cstdint>
#include <glaze/glaze.hpp>

// Predefined glaze structs for the Binance order wire format (WS-API + REST),
// used on the order hot path instead of building/parsing JSON dynamically.
//
// Binance encodes decimals as JSON strings and ids as JSON numbers. The request
// params are sent as a flat string->string object; the field order on the wire
// does not matter for signing (Binance re-derives the signature from the
// alphabetically-sorted query string), so glaze's declaration-order serialization
// is fine.

namespace Omni::Binance {

// --- WS-API request -------------------------------------------------------
// {"id":"<cid>","method":"order.place","params":{...all strings...}}
struct WsApiRequest {
    std::string id = "";
    std::string method = "";
    std::map<std::string, std::string> params = {};
};

// --- WS-API response ------------------------------------------------------
// Branch on status before fully parsing: success carries `result`, error `error`.
//   success: {"id":"..","status":200,"result":{"orderId":123,"clientOrderId":".."}}
//   error:   {"id":"..","status":4xx,"error":{"code":-1,"msg":".."}}
struct WsApiResponseClassifier {
    std::string id = "";
    int status = 0;
};

struct WsApiResult {
    long orderId = 0;
};
struct WsApiSuccessResponse {
    WsApiResult result = {};
};

struct WsApiError {
    int code = 0;
    std::string msg = "";
};
struct WsApiErrorResponse {
    WsApiError error = {};
};

// --- REST response --------------------------------------------------------
// {"orderId":123,"clientOrderId":"..",...} (only orderId is needed)
struct RestOrderResponse {
    long orderId = 0;
};

} // namespace Omni::Binance
