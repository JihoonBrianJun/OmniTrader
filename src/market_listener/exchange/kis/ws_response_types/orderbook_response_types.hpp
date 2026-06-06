#pragma once
#include <string>
#include <vector>
#include "common/market_msg_types.hpp"

namespace Omni::KIS::KoreanDerivatives {

using Omni::OrderbookData;
using Omni::OrderbookLevel;

struct IndexFuturesOrderbookResponse {
    std::string FUTS_SHRN_ISCD;
    std::string BSOP_HOUR;
    std::string FUTS_ASKP1, FUTS_ASKP2, FUTS_ASKP3, FUTS_ASKP4, FUTS_ASKP5;
    std::string FUTS_BIDP1, FUTS_BIDP2, FUTS_BIDP3, FUTS_BIDP4, FUTS_BIDP5;
    std::string ASKP_CSNU1, ASKP_CSNU2, ASKP_CSNU3, ASKP_CSNU4, ASKP_CSNU5;
    std::string BIDP_CSNU1, BIDP_CSNU2, BIDP_CSNU3, BIDP_CSNU4, BIDP_CSNU5;
    std::string ASKP_RSQN1, ASKP_RSQN2, ASKP_RSQN3, ASKP_RSQN4, ASKP_RSQN5;
    std::string BIDP_RSQN1, BIDP_RSQN2, BIDP_RSQN3, BIDP_RSQN4, BIDP_RSQN5;
    std::string TOTAL_ASKP_CSNU, TOTAL_BIDP_CSNU, TOTAL_ASKP_RSQN, TOTAL_BIDP_RSQN;
    std::string TOTAL_ASKP_RSQN_ICDC, TOTAL_BIDP_RSQN_ICDC;

    static constexpr size_t field_cnt = 38;

    OrderbookData normalize() {
        return OrderbookData{
            .bid_book = {
                {std::stod(FUTS_BIDP1), std::stod(BIDP_RSQN1)},
                {std::stod(FUTS_BIDP2), std::stod(BIDP_RSQN2)},
                {std::stod(FUTS_BIDP3), std::stod(BIDP_RSQN3)},
                {std::stod(FUTS_BIDP4), std::stod(BIDP_RSQN4)},
                {std::stod(FUTS_BIDP5), std::stod(BIDP_RSQN5)}
            },
            .ask_book = {
                {std::stod(FUTS_ASKP1), std::stod(ASKP_RSQN1)},
                {std::stod(FUTS_ASKP2), std::stod(ASKP_RSQN2)},
                {std::stod(FUTS_ASKP3), std::stod(ASKP_RSQN3)},
                {std::stod(FUTS_ASKP4), std::stod(ASKP_RSQN4)},
                {std::stod(FUTS_ASKP5), std::stod(ASKP_RSQN5)}
            }
        };
    }
};


struct IndexOptionOrderbookResponse {
    std::string OPTN_SHRN_ISCD;
    std::string BSOP_HOUR;
    std::string OPTN_ASKP1, OPTN_ASKP2, OPTN_ASKP3, OPTN_ASKP4, OPTN_ASKP5;
    std::string OPTN_BIDP1, OPTN_BIDP2, OPTN_BIDP3, OPTN_BIDP4, OPTN_BIDP5;
    std::string ASKP_CSNU1, ASKP_CSNU2, ASKP_CSNU3, ASKP_CSNU4, ASKP_CSNU5;
    std::string BIDP_CSNU1, BIDP_CSNU2, BIDP_CSNU3, BIDP_CSNU4, BIDP_CSNU5;
    std::string ASKP_RSQN1, ASKP_RSQN2, ASKP_RSQN3, ASKP_RSQN4, ASKP_RSQN5;
    std::string BIDP_RSQN1, BIDP_RSQN2, BIDP_RSQN3, BIDP_RSQN4, BIDP_RSQN5;
    std::string TOTAL_ASKP_CSNU, TOTAL_BIDP_CSNU, TOTAL_ASKP_RSQN, TOTAL_BIDP_RSQN;
    std::string TOTAL_ASKP_RSQN_ICDC, TOTAL_BIDP_RSQN_ICDC;

    static constexpr size_t field_cnt = 38;

    OrderbookData normalize() {
        return OrderbookData{
            .bid_book = {
                {std::stod(OPTN_BIDP1), std::stod(BIDP_RSQN1)},
                {std::stod(OPTN_BIDP2), std::stod(BIDP_RSQN2)},
                {std::stod(OPTN_BIDP3), std::stod(BIDP_RSQN3)},
                {std::stod(OPTN_BIDP4), std::stod(BIDP_RSQN4)},
                {std::stod(OPTN_BIDP5), std::stod(BIDP_RSQN5)}
            },
            .ask_book = {
                {std::stod(OPTN_ASKP1), std::stod(ASKP_RSQN1)},
                {std::stod(OPTN_ASKP2), std::stod(ASKP_RSQN2)},
                {std::stod(OPTN_ASKP3), std::stod(ASKP_RSQN3)},
                {std::stod(OPTN_ASKP4), std::stod(ASKP_RSQN4)},
                {std::stod(OPTN_ASKP5), std::stod(ASKP_RSQN5)}
            }
        };
    }
};


struct StockFuturesOrderbookResponse {
    std::string FUTS_SHRN_ISCD;
    std::string BSOP_HOUR;
    std::string ASKP1, ASKP2, ASKP3, ASKP4, ASKP5, ASKP6, ASKP7, ASKP8, ASKP9, ASKP10;
    std::string BIDP1, BIDP2, BIDP3, BIDP4, BIDP5, BIDP6, BIDP7, BIDP8, BIDP9, BIDP10;
    std::string ASKP_CSNU1, ASKP_CSNU2, ASKP_CSNU3, ASKP_CSNU4, ASKP_CSNU5;
    std::string ASKP_CSNU6, ASKP_CSNU7, ASKP_CSNU8, ASKP_CSNU9, ASKP_CSNU10;
    std::string BIDP_CSNU1, BIDP_CSNU2, BIDP_CSNU3, BIDP_CSNU4, BIDP_CSNU5;
    std::string BIDP_CSNU6, BIDP_CSNU7, BIDP_CSNU8, BIDP_CSNU9, BIDP_CSNU10;
    std::string ASKP_RSQN1, ASKP_RSQN2, ASKP_RSQN3, ASKP_RSQN4, ASKP_RSQN5;
    std::string ASKP_RSQN6, ASKP_RSQN7, ASKP_RSQN8, ASKP_RSQN9, ASKP_RSQN10;
    std::string BIDP_RSQN1, BIDP_RSQN2, BIDP_RSQN3, BIDP_RSQN4, BIDP_RSQN5;
    std::string BIDP_RSQN6, BIDP_RSQN7, BIDP_RSQN8, BIDP_RSQN9, BIDP_RSQN10;
    std::string TOTAL_ASKP_CSNU, TOTAL_BIDP_CSNU, TOTAL_ASKP_RSQN, TOTAL_BIDP_RSQN;
    std::string TOTAL_ASKP_RSQN_ICDC, TOTAL_BIDP_RSQN_ICDC;

    static constexpr size_t field_cnt = 68;

    OrderbookData normalize() {
        return OrderbookData{
            .bid_book = {
                {std::stod(BIDP1), std::stod(BIDP_RSQN1)},
                {std::stod(BIDP2), std::stod(BIDP_RSQN2)},
                {std::stod(BIDP3), std::stod(BIDP_RSQN3)},
                {std::stod(BIDP4), std::stod(BIDP_RSQN4)},
                {std::stod(BIDP5), std::stod(BIDP_RSQN5)},
                {std::stod(BIDP6), std::stod(BIDP_RSQN6)},
                {std::stod(BIDP7), std::stod(BIDP_RSQN7)},
                {std::stod(BIDP8), std::stod(BIDP_RSQN8)},
                {std::stod(BIDP9), std::stod(BIDP_RSQN9)},
                {std::stod(BIDP10), std::stod(BIDP_RSQN10)}
            },
            .ask_book = {
                {std::stod(ASKP1), std::stod(ASKP_RSQN1)},
                {std::stod(ASKP2), std::stod(ASKP_RSQN2)},
                {std::stod(ASKP3), std::stod(ASKP_RSQN3)},
                {std::stod(ASKP4), std::stod(ASKP_RSQN4)},
                {std::stod(ASKP5), std::stod(ASKP_RSQN5)},
                {std::stod(ASKP6), std::stod(ASKP_RSQN6)},
                {std::stod(ASKP7), std::stod(ASKP_RSQN7)},
                {std::stod(ASKP8), std::stod(ASKP_RSQN8)},
                {std::stod(ASKP9), std::stod(ASKP_RSQN9)},
                {std::stod(ASKP10), std::stod(ASKP_RSQN10)}
            }
        };
    }
};


struct StockOptionOrderbookResponse {
    std::string OPTN_SHRN_ISCD;
    std::string BSOP_HOUR;
    std::string OPTN_ASKP1, OPTN_ASKP2, OPTN_ASKP3, OPTN_ASKP4, OPTN_ASKP5;
    std::string OPTN_BIDP1, OPTN_BIDP2, OPTN_BIDP3, OPTN_BIDP4, OPTN_BIDP5;
    std::string ASKP_CSNU1, ASKP_CSNU2, ASKP_CSNU3, ASKP_CSNU4, ASKP_CSNU5;
    std::string BIDP_CSNU1, BIDP_CSNU2, BIDP_CSNU3, BIDP_CSNU4, BIDP_CSNU5;
    std::string ASKP_RSQN1, ASKP_RSQN2, ASKP_RSQN3, ASKP_RSQN4, ASKP_RSQN5;
    std::string BIDP_RSQN1, BIDP_RSQN2, BIDP_RSQN3, BIDP_RSQN4, BIDP_RSQN5;
    std::string TOTAL_ASKP_CSNU, TOTAL_BIDP_CSNU, TOTAL_ASKP_RSQN, TOTAL_BIDP_RSQN;
    std::string TOTAL_ASKP_RSQN_ICDC, TOTAL_BIDP_RSQN_ICDC;
    std::string OPTN_ASKP6, OPTN_ASKP7, OPTN_ASKP8, OPTN_ASKP9, OPTN_ASKP10;
    std::string OPTN_BIDP6, OPTN_BIDP7, OPTN_BIDP8, OPTN_BIDP9, OPTN_BIDP10;
    std::string ASKP_CSNU6, ASKP_CSNU7, ASKP_CSNU8, ASKP_CSNU9, ASKP_CSNU10;
    std::string BIDP_CSNU6, BIDP_CSNU7, BIDP_CSNU8, BIDP_CSNU9, BIDP_CSNU10;
    std::string ASKP_RSQN6, ASKP_RSQN7, ASKP_RSQN8, ASKP_RSQN9, ASKP_RSQN10;
    std::string BIDP_RSQN6, BIDP_RSQN7, BIDP_RSQN8, BIDP_RSQN9, BIDP_RSQN10;

    static constexpr size_t field_cnt = 68;

    OrderbookData normalize() {
        return OrderbookData{
            .bid_book = {
                {std::stod(OPTN_BIDP1), std::stod(BIDP_RSQN1)},
                {std::stod(OPTN_BIDP2), std::stod(BIDP_RSQN2)},
                {std::stod(OPTN_BIDP3), std::stod(BIDP_RSQN3)},
                {std::stod(OPTN_BIDP4), std::stod(BIDP_RSQN4)},
                {std::stod(OPTN_BIDP5), std::stod(BIDP_RSQN5)},
                {std::stod(OPTN_BIDP6), std::stod(BIDP_RSQN6)},
                {std::stod(OPTN_BIDP7), std::stod(BIDP_RSQN7)},
                {std::stod(OPTN_BIDP8), std::stod(BIDP_RSQN8)},
                {std::stod(OPTN_BIDP9), std::stod(BIDP_RSQN9)},
                {std::stod(OPTN_BIDP10), std::stod(BIDP_RSQN10)}
            },
            .ask_book = {
                {std::stod(OPTN_ASKP1), std::stod(ASKP_RSQN1)},
                {std::stod(OPTN_ASKP2), std::stod(ASKP_RSQN2)},
                {std::stod(OPTN_ASKP3), std::stod(ASKP_RSQN3)},
                {std::stod(OPTN_ASKP4), std::stod(ASKP_RSQN4)},
                {std::stod(OPTN_ASKP5), std::stod(ASKP_RSQN5)},
                {std::stod(OPTN_ASKP6), std::stod(ASKP_RSQN6)},
                {std::stod(OPTN_ASKP7), std::stod(ASKP_RSQN7)},
                {std::stod(OPTN_ASKP8), std::stod(ASKP_RSQN8)},
                {std::stod(OPTN_ASKP9), std::stod(ASKP_RSQN9)},
                {std::stod(OPTN_ASKP10), std::stod(ASKP_RSQN10)}
            }
        };
    }
};


struct CommodityFuturesOrderbookResponse {
    std::string FUTS_SHRN_ISCD;
    std::string BSOP_HOUR;
    std::string FUTS_ASKP1, FUTS_ASKP2, FUTS_ASKP3, FUTS_ASKP4, FUTS_ASKP5;
    std::string FUTS_BIDP1, FUTS_BIDP2, FUTS_BIDP3, FUTS_BIDP4, FUTS_BIDP5;
    std::string ASKP_CSNU1, ASKP_CSNU2, ASKP_CSNU3, ASKP_CSNU4, ASKP_CSNU5;
    std::string BIDP_CSNU1, BIDP_CSNU2, BIDP_CSNU3, BIDP_CSNU4, BIDP_CSNU5;
    std::string ASKP_RSQN1, ASKP_RSQN2, ASKP_RSQN3, ASKP_RSQN4, ASKP_RSQN5;
    std::string BIDP_RSQN1, BIDP_RSQN2, BIDP_RSQN3, BIDP_RSQN4, BIDP_RSQN5;
    std::string TOTAL_ASKP_CSNU, TOTAL_BIDP_CSNU, TOTAL_ASKP_RSQN, TOTAL_BIDP_RSQN;
    std::string TOTAL_ASKP_RSQN_ICDC, TOTAL_BIDP_RSQN_ICDC;

    static constexpr size_t field_cnt = 38;

    OrderbookData normalize() {
        return OrderbookData{
            .bid_book = {
                {std::stod(FUTS_BIDP1), std::stod(BIDP_RSQN1)},
                {std::stod(FUTS_BIDP2), std::stod(BIDP_RSQN2)},
                {std::stod(FUTS_BIDP3), std::stod(BIDP_RSQN3)},
                {std::stod(FUTS_BIDP4), std::stod(BIDP_RSQN4)},
                {std::stod(FUTS_BIDP5), std::stod(BIDP_RSQN5)}
            },
            .ask_book = {
                {std::stod(FUTS_ASKP1), std::stod(ASKP_RSQN1)},
                {std::stod(FUTS_ASKP2), std::stod(ASKP_RSQN2)},
                {std::stod(FUTS_ASKP3), std::stod(ASKP_RSQN3)},
                {std::stod(FUTS_ASKP4), std::stod(ASKP_RSQN4)},
                {std::stod(FUTS_ASKP5), std::stod(ASKP_RSQN5)}
            }
        };
    }
};


struct NightFuturesOrderbookResponse {
    std::string FUTS_SHRN_ISCD;
    std::string BSOP_HOUR;
    std::string FUTS_ASKP1, FUTS_ASKP2, FUTS_ASKP3, FUTS_ASKP4, FUTS_ASKP5;
    std::string FUTS_BIDP1, FUTS_BIDP2, FUTS_BIDP3, FUTS_BIDP4, FUTS_BIDP5;
    std::string ASKP_CSNU1, ASKP_CSNU2, ASKP_CSNU3, ASKP_CSNU4, ASKP_CSNU5;
    std::string BIDP_CSNU1, BIDP_CSNU2, BIDP_CSNU3, BIDP_CSNU4, BIDP_CSNU5;
    std::string ASKP_RSQN1, ASKP_RSQN2, ASKP_RSQN3, ASKP_RSQN4, ASKP_RSQN5;
    std::string BIDP_RSQN1, BIDP_RSQN2, BIDP_RSQN3, BIDP_RSQN4, BIDP_RSQN5;
    std::string TOTAL_ASKP_CSNU, TOTAL_BIDP_CSNU, TOTAL_ASKP_RSQN, TOTAL_BIDP_RSQN;
    std::string TOTAL_ASKP_RSQN_ICDC, TOTAL_BIDP_RSQN_ICDC;

    static constexpr size_t field_cnt = 38;

    OrderbookData normalize() {
        return OrderbookData{
            .bid_book = {
                {std::stod(FUTS_BIDP1), std::stod(BIDP_RSQN1)},
                {std::stod(FUTS_BIDP2), std::stod(BIDP_RSQN2)},
                {std::stod(FUTS_BIDP3), std::stod(BIDP_RSQN3)},
                {std::stod(FUTS_BIDP4), std::stod(BIDP_RSQN4)},
                {std::stod(FUTS_BIDP5), std::stod(BIDP_RSQN5)}
            },
            .ask_book = {
                {std::stod(FUTS_ASKP1), std::stod(ASKP_RSQN1)},
                {std::stod(FUTS_ASKP2), std::stod(ASKP_RSQN2)},
                {std::stod(FUTS_ASKP3), std::stod(ASKP_RSQN3)},
                {std::stod(FUTS_ASKP4), std::stod(ASKP_RSQN4)},
                {std::stod(FUTS_ASKP5), std::stod(ASKP_RSQN5)}
            }
        };
    }
};


struct NightOptionOrderbookResponse {
    std::string OPTN_SHRN_ISCD;
    std::string BSOP_HOUR;
    std::string OPTN_ASKP1, OPTN_ASKP2, OPTN_ASKP3, OPTN_ASKP4, OPTN_ASKP5;
    std::string OPTN_BIDP1, OPTN_BIDP2, OPTN_BIDP3, OPTN_BIDP4, OPTN_BIDP5;
    std::string ASKP_CSNU1, ASKP_CSNU2, ASKP_CSNU3, ASKP_CSNU4, ASKP_CSNU5;
    std::string BIDP_CSNU1, BIDP_CSNU2, BIDP_CSNU3, BIDP_CSNU4, BIDP_CSNU5;
    std::string ASKP_RSQN1, ASKP_RSQN2, ASKP_RSQN3, ASKP_RSQN4, ASKP_RSQN5;
    std::string BIDP_RSQN1, BIDP_RSQN2, BIDP_RSQN3, BIDP_RSQN4, BIDP_RSQN5;
    std::string TOTAL_ASKP_CSNU, TOTAL_BIDP_CSNU, TOTAL_ASKP_RSQN, TOTAL_BIDP_RSQN;
    std::string TOTAL_ASKP_RSQN_ICDC, TOTAL_BIDP_RSQN_ICDC;

    static constexpr size_t field_cnt = 38;

    OrderbookData normalize() {
        return OrderbookData{
            .bid_book = {
                {std::stod(OPTN_BIDP1), std::stod(BIDP_RSQN1)},
                {std::stod(OPTN_BIDP2), std::stod(BIDP_RSQN2)},
                {std::stod(OPTN_BIDP3), std::stod(BIDP_RSQN3)},
                {std::stod(OPTN_BIDP4), std::stod(BIDP_RSQN4)},
                {std::stod(OPTN_BIDP5), std::stod(BIDP_RSQN5)}
            },
            .ask_book = {
                {std::stod(OPTN_ASKP1), std::stod(ASKP_RSQN1)},
                {std::stod(OPTN_ASKP2), std::stod(ASKP_RSQN2)},
                {std::stod(OPTN_ASKP3), std::stod(ASKP_RSQN3)},
                {std::stod(OPTN_ASKP4), std::stod(ASKP_RSQN4)},
                {std::stod(OPTN_ASKP5), std::stod(ASKP_RSQN5)}
            }
        };
    }
};

} // namespace Omni::KIS::KoreanDerivatives
