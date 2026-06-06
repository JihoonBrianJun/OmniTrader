#pragma once
#include <string>
#include "common/market_msg_types.hpp"

namespace Omni::KIS::KoreanDerivatives {

using Omni::TradeData;

struct IndexFuturesTradeResponse {
    std::string FUTS_SHRN_ISCD, BSOP_HOUR, FUTS_PRDY_VRSS, PRDY_VRSS_SIGN, FUTS_PRDY_CTRT;
    std::string FUTS_PRPR, FUTS_OPRC, FUTS_HGPR, FUTS_LWPR, LAST_CNQN, ACML_VOL, ACML_TR_PBMN;
    std::string HTS_THPR, MRKT_BASIS, DPRT, NMSC_FCTN_STPL_PRC, FMSC_FCTN_STPL_PRC, SPEAD_PRC;
    std::string HTS_OTST_STPL_QTY, OTST_STPL_QTY_ICDC, OPRC_HOUR, OPRC_VRSS_PRPR_SIGN, OPRC_VRSS_NMIX_PRPR;
    std::string HGPR_HOUR, HGPR_VRSS_PRPR_SIGN, HGPR_VRSS_NMIX_PRPR, LWPR_HOUR, LWPR_VRSS_PRPR_SIGN, LWPR_VRSS_NMIX_PRPR;
    std::string SHNU_RATE, CTTR, ESDG, OTST_STPL_RGBF_QTY_ICDC, THPR_BASIS;
    std::string FUTS_ASKP1, FUTS_BIDP1, ASKP_RSQN1, BIDP_RSQN1;
    std::string SELN_CNTG_CSNU, SHNU_CNTG_CSNU, NTBY_CNTG_CSNU, SELN_CNTG_SMTN, SHNU_CNTG_SMTN;
    std::string TOTAL_ASKP_RSQN, TOTAL_BIDP_RSQN, PRDY_VOL_VRSS_ACML_VOL_RATE, DSCS_BLTR_ACML_QTY;
    std::string DYNM_MXPR, DYNM_LLAM, DYNM_PRC_LIMT_YN;

    static constexpr size_t field_cnt = 50;

    TradeData normalize() {
        return TradeData{
            .trade_price = std::stod(FUTS_PRPR),
            .cum_trade_qty = std::stod(LAST_CNQN),
            .cum_buy_trade_qty = std::stod(SHNU_CNTG_SMTN)
        };
    }
};


struct IndexOptionTradeResponse {
    std::string OPTN_SHRN_ISCD, BSOP_HOUR, OPTN_PRPR, PRDY_VRSS_SIGN, OPTN_PRDY_VRSS, PRDY_CTRT;
    std::string OPTN_OPRC, OPTN_HGPR, OPTN_LWPR, LAST_CNQN, ACML_VOL, ACML_TR_PBMN, HTS_THPR;
    std::string HTS_OTST_STPL_QTY, OTST_STPL_QTY_ICDC, OPRC_HOUR, OPRC_VRSS_PRPR_SIGN, OPRC_VRSS_NMIX_PRPR;
    std::string HGPR_HOUR, HGPR_VRSS_PRPR_SIGN, HGPR_VRSS_NMIX_PRPR, LWPR_HOUR, LWPR_VRSS_PRPR_SIGN, LWPR_VRSS_NMIX_PRPR;
    std::string SHNU_RATE, PRMM_VAL, INVL_VAL, TMVL_VAL, DELTA, GAMA, VEGA, THETA, RHO;
    std::string HTS_INTS_VLTL, ESDG, OTST_STPL_RGBF_QTY_ICDC, THPR_BASIS, UNAS_HIST_VLTL, CTTR, DPRT, MRKT_BASIS;
    std::string OPTN_ASKP1, OPTN_BIDP1, ASKP_RSQN1, BIDP_RSQN1;
    std::string SELN_CNTG_CSNU, SHNU_CNTG_CSNU, NTBY_CNTG_CSNU, SELN_CNTG_SMTN, SHNU_CNTG_SMTN;
    std::string TOTAL_ASKP_RSQN, TOTAL_BIDP_RSQN, PRDY_VOL_VRSS_ACML_VOL_RATE, AVRG_VLTL, DSCS_LRQN_VOL;
    std::string DYNM_MXPR, DYNM_LLAM, DYNM_PRC_LIMT_YN;

    static constexpr size_t field_cnt = 58;

    TradeData normalize() {
        return TradeData{
            .trade_price = std::stod(OPTN_PRPR),
            .cum_trade_qty = std::stod(LAST_CNQN),
            .cum_buy_trade_qty = std::stod(SHNU_CNTG_SMTN)
        };
    }
};


struct StockFuturesTradeResponse {
    std::string FUTS_SHRN_ISCD, BSOP_HOUR, STCK_PRPR, PRDY_VRSS_SIGN, PRDY_VRSS, FUTS_PRDY_CTRT;
    std::string STCK_OPRC, STCK_HGPR, STCK_LWPR, LAST_CNQN, ACML_VOL, ACML_TR_PBMN, HTS_THPR;
    std::string MRKT_BASIS, DPRT, NMSC_FCTN_STPL_PRC, FMSC_FCTN_STPL_PRC, SPEAD_PRC;
    std::string HTS_OTST_STPL_QTY, OTST_STPL_QTY_ICDC, OPRC_HOUR, OPRC_VRSS_PRPR_SIGN, OPRC_VRSS_PRPR;
    std::string HGPR_HOUR, HGPR_VRSS_PRPR_SIGN, HGPR_VRSS_PRPR, LWPR_HOUR, LWPR_VRSS_PRPR_SIGN, LWPR_VRSS_PRPR;
    std::string SHNU_RATE, CTTR, ESDG, OTST_STPL_RGBF_QTY_ICDC, THPR_BASIS;
    std::string ASKP1, BIDP1, ASKP_RSQN1, BIDP_RSQN1;
    std::string SELN_CNTG_CSNU, SHNU_CNTG_CSNU, NTBY_CNTG_CSNU, SELN_CNTG_SMTN, SHNU_CNTG_SMTN;
    std::string TOTAL_ASKP_RSQN, TOTAL_BIDP_RSQN, PRDY_VOL_VRSS_ACML_VOL_RATE;
    std::string DYNM_MXPR, DYNM_LLAM, DYNM_PRC_LIMT_YN;

    static constexpr size_t field_cnt = 49;

    TradeData normalize() {
        return TradeData{
            .trade_price = std::stod(STCK_PRPR),
            .cum_trade_qty = std::stod(LAST_CNQN),
            .cum_buy_trade_qty = std::stod(SHNU_CNTG_SMTN)
        };
    }
};


struct StockOptionTradeResponse {
    std::string OPTN_SHRN_ISCD, BSOP_HOUR, OPTN_PRPR, PRDY_VRSS_SIGN, OPTN_PRDY_VRSS, PRDY_CTRT;
    std::string OPTN_OPRC, OPTN_HGPR, OPTN_LWPR, LAST_CNQN, ACML_VOL, ACML_TR_PBMN, HTS_THPR;
    std::string HTS_OTST_STPL_QTY, OTST_STPL_QTY_ICDC, OPRC_HOUR, OPRC_VRSS_PRPR_SIGN, OPRC_VRSS_NMIX_PRPR;
    std::string HGPR_HOUR, HGPR_VRSS_PRPR_SIGN, HGPR_VRSS_NMIX_PRPR, LWPR_HOUR, LWPR_VRSS_PRPR_SIGN, LWPR_VRSS_NMIX_PRPR;
    std::string SHNU_RATE, PRMM_VAL, INVL_VAL, TMVL_VAL, DELTA, GAMA, VEGA, THETA, RHO;
    std::string HTS_INTS_VLTL, ESDG, OTST_STPL_RGBF_QTY_ICDC, THPR_BASIS, UNAS_HIST_VLTL, CTTR, DPRT, MRKT_BASIS;
    std::string OPTN_ASKP1, OPTN_BIDP1, ASKP_RSQN1, BIDP_RSQN1;
    std::string SELN_CNTG_CSNU, SHNU_CNTG_CSNU, NTBY_CNTG_CSNU, SELN_CNTG_SMTN, SHNU_CNTG_SMTN;
    std::string TOTAL_ASKP_RSQN, TOTAL_BIDP_RSQN, PRDY_VOL_VRSS_ACML_VOL_RATE;

    static constexpr size_t field_cnt = 53;

    TradeData normalize() {
        return TradeData{
            .trade_price = std::stod(OPTN_PRPR),
            .cum_trade_qty = std::stod(LAST_CNQN),
            .cum_buy_trade_qty = std::stod(SHNU_CNTG_SMTN)
        };
    }
};


struct CommodityFuturesTradeResponse {
    std::string FUTS_SHRN_ISCD, BSOP_HOUR, FUTS_PRDY_VRSS, PRDY_VRSS_SIGN, FUTS_PRDY_CTRT;
    std::string FUTS_PRPR, FUTS_OPRC, FUTS_HGPR, FUTS_LWPR, LAST_CNQN, ACML_VOL, ACML_TR_PBMN;
    std::string HTS_THPR, MRKT_BASIS, DPRT, NMSC_FCTN_STPL_PRC, FMSC_FCTN_STPL_PRC, SPEAD_PRC;
    std::string HTS_OTST_STPL_QTY, OTST_STPL_QTY_ICDC, OPRC_HOUR, OPRC_VRSS_PRPR_SIGN, OPRC_VRSS_NMIX_PRPR;
    std::string HGPR_HOUR, HGPR_VRSS_PRPR_SIGN, HGPR_VRSS_NMIX_PRPR, LWPR_HOUR, LWPR_VRSS_PRPR_SIGN, LWPR_VRSS_NMIX_PRPR;
    std::string SHNU_RATE, CTTR, ESDG, OTST_STPL_RGBF_QTY_ICDC, THPR_BASIS;
    std::string FUTS_ASKP1, FUTS_BIDP1, ASKP_RSQN1, BIDP_RSQN1;
    std::string SELN_CNTG_CSNU, SHNU_CNTG_CSNU, NTBY_CNTG_CSNU, SELN_CNTG_SMTN, SHNU_CNTG_SMTN;
    std::string TOTAL_ASKP_RSQN, TOTAL_BIDP_RSQN, PRDY_VOL_VRSS_ACML_VOL_RATE, DSCS_BLTR_ACML_QTY;
    std::string DYNM_MXPR, DYNM_LLAM, DYNM_PRC_LIMT_YN;

    static constexpr size_t field_cnt = 50;

    TradeData normalize() {
        return TradeData{
            .trade_price = std::stod(FUTS_PRPR),
            .cum_trade_qty = std::stod(LAST_CNQN),
            .cum_buy_trade_qty = std::stod(SHNU_CNTG_SMTN)
        };
    }
};


struct NightFuturesTradeResponse {
    std::string FUTS_SHRN_ISCD, BSOP_HOUR, FUTS_PRDY_VRSS, PRDY_VRSS_SIGN, FUTS_PRDY_CTRT;
    std::string FUTS_PRPR, FUTS_OPRC, FUTS_HGPR, FUTS_LWPR, LAST_CNQN, ACML_VOL, ACML_TR_PBMN;
    std::string HTS_THPR, MRKT_BASIS, DPRT, NMSC_FCTN_STPL_PRC, FMSC_FCTN_STPL_PRC, SPEAD_PRC;
    std::string HTS_OTST_STPL_QTY, OTST_STPL_QTY_ICDC, OPRC_HOUR, OPRC_VRSS_PRPR_SIGN, OPRC_VRSS_NMIX_PRPR;
    std::string HGPR_HOUR, HGPR_VRSS_PRPR_SIGN, HGPR_VRSS_NMIX_PRPR, LWPR_HOUR, LWPR_VRSS_PRPR_SIGN, LWPR_VRSS_NMIX_PRPR;
    std::string SHNU_RATE, CTTR, ESDG, OTST_STPL_RGBF_QTY_ICDC, THPR_BASIS;
    std::string FUTS_ASKP1, FUTS_BIDP1, ASKP_RSQN1, BIDP_RSQN1;
    std::string SELN_CNTG_CSNU, SHNU_CNTG_CSNU, NTBY_CNTG_CSNU, SELN_CNTG_SMTN, SHNU_CNTG_SMTN;
    std::string TOTAL_ASKP_RSQN, TOTAL_BIDP_RSQN, PRDY_VOL_VRSS_ACML_VOL_RATE;
    std::string DYNM_MXPR, DYNM_LLAM, DYNM_PRC_LIMT_YN;

    static constexpr size_t field_cnt = 49;

    TradeData normalize() {
        return TradeData{
            .trade_price = std::stod(FUTS_PRPR),
            .cum_trade_qty = std::stod(LAST_CNQN),
            .cum_buy_trade_qty = std::stod(SHNU_CNTG_SMTN)
        };
    }
};


struct NightOptionTradeResponse {
    std::string OPTN_SHRN_ISCD, BSOP_HOUR, OPTN_PRPR, PRDY_VRSS_SIGN, OPTN_PRDY_VRSS, PRDY_CTRT;
    std::string OPTN_OPRC, OPTN_HGPR, OPTN_LWPR, LAST_CNQN, ACML_VOL, ACML_TR_PBMN, HTS_THPR;
    std::string HTS_OTST_STPL_QTY, OTST_STPL_QTY_ICDC, OPRC_HOUR, OPRC_VRSS_PRPR_SIGN, OPRC_VRSS_NMIX_PRPR;
    std::string HGPR_HOUR, HGPR_VRSS_PRPR_SIGN, HGPR_VRSS_NMIX_PRPR, LWPR_HOUR, LWPR_VRSS_PRPR_SIGN, LWPR_VRSS_NMIX_PRPR;
    std::string SHNU_RATE, PRMM_VAL, INVL_VAL, TMVL_VAL, DELTA, GAMA, VEGA, THETA, RHO;
    std::string HTS_INTS_VLTL, ESDG, OTST_STPL_RGBF_QTY_ICDC, THPR_BASIS, UNAS_HIST_VLTL, CTTR, DPRT, MRKT_BASIS;
    std::string OPTN_ASKP1, OPTN_BIDP1, ASKP_RSQN1, BIDP_RSQN1;
    std::string SELN_CNTG_CSNU, SHNU_CNTG_CSNU, NTBY_CNTG_CSNU, SELN_CNTG_SMTN, SHNU_CNTG_SMTN;
    std::string TOTAL_ASKP_RSQN, TOTAL_BIDP_RSQN, PRDY_VOL_VRSS_ACML_VOL_RATE;
    std::string DYNM_MXPR, DYNM_PRC_LIMT_YN, DYNM_LLAM;

    static constexpr size_t field_cnt = 56;

    TradeData normalize() {
        return TradeData{
            .trade_price = std::stod(OPTN_PRPR),
            .cum_trade_qty = std::stod(LAST_CNQN),
            .cum_buy_trade_qty = std::stod(SHNU_CNTG_SMTN)
        };
    }
};

} // namespace Omni::KIS::KoreanDerivatives
