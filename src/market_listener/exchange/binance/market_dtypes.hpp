#pragma once
#include <string>
#include <vector>
#include <array>
#include <glaze/glaze.hpp>

// Predefined glaze structs for the Binance USDⓈ-M stream wire format, used on the
// market-data / user-data hot path instead of parsing JSON dynamically.
//
// Member names match the exchange's (single-character) JSON keys, so plain glaze
// reflection maps them without a glz::meta override. Binance encodes decimals as
// JSON strings (kept as std::string here and converted with std::stod by the
// caller) and ids/update-ids as JSON numbers.
//
// Reads must tolerate the many keys we don't model, so callers use
// error_on_unknown_keys = false; the event-type classifiers additionally use
// partial_read = true to branch before fully parsing.

namespace Omni::Listener::Binance {

// --- Market stream (combined: {"stream":"..","data":{..}}) ----------------
// Classify on data.e before fully parsing (bookTicker / depthUpdate / aggTrade).
struct MarketEventType {
    std::string e = "";
};
struct MarketEventClassifier {
    MarketEventType data = {};
};

// bookTicker: {"data":{"e":"bookTicker","s":"..","b":"..","B":"..","a":"..","A":".."}}
struct BookTicker {
    std::string s = "";   // symbol
    std::string b = "";   // best bid price
    std::string B = "";   // best bid qty
    std::string a = "";   // best ask price
    std::string A = "";   // best ask qty
};
struct BookTickerEnvelope {
    BookTicker data = {};
};

// depthUpdate: {"data":{"e":"depthUpdate","s":"..","U":..,"u":..,"pu":..,"b":[["p","q"]..],"a":[..]}}
struct DepthUpdate {
    std::string s = "";
    long U = 0;
    long u = 0;
    long pu = -1;
    std::vector<std::array<std::string, 2>> b = {};   // bids [price, qty]
    std::vector<std::array<std::string, 2>> a = {};   // asks [price, qty]
};
struct DepthUpdateEnvelope {
    DepthUpdate data = {};
};

// aggTrade: {"data":{"e":"aggTrade","s":"..","p":"..","q":".."}}
struct AggTrade {
    std::string s = "";
    std::string p = "";   // trade price
    std::string q = "";   // trade qty
};
struct AggTradeEnvelope {
    AggTrade data = {};
};

// --- User stream (top-level event, no envelope) ---------------------------
// Classify on top-level e (ORDER_TRADE_UPDATE / ACCOUNT_UPDATE).
struct UserEventClassifier {
    std::string e = "";
};

// ORDER_TRADE_UPDATE: {"e":"ORDER_TRADE_UPDATE",...,"o":{...}}
struct OrderTradeUpdateOrder {
    std::string s = "";   // symbol
    std::string c = "";   // client order id (cid)
    std::string S = "";   // side: BUY / SELL
    std::string x = "";   // execution type: NEW / CANCELED / EXPIRED / TRADE / ..
    long i = 0;           // order id
    std::string p = "";   // original price
    std::string q = "";   // original qty
    std::string z = "";   // cumulative filled qty
    std::string L = "";   // last filled price
    std::string l = "";   // last filled qty
};
struct OrderTradeUpdate {
    OrderTradeUpdateOrder o = {};
};

// ACCOUNT_UPDATE: {"e":"ACCOUNT_UPDATE",...,"a":{"B":[..],"P":[..]}}
struct AccountBalance {
    std::string a = "";   // asset symbol
    std::string wb = "";  // wallet balance
};
struct AccountPosition {
    std::string s = "";   // symbol
    std::string pa = "";  // signed position amount
};
struct AccountUpdateData {
    std::vector<AccountBalance> B = {};
    std::vector<AccountPosition> P = {};
};
struct AccountUpdate {
    AccountUpdateData a = {};
};

} // namespace Omni::Listener::Binance
