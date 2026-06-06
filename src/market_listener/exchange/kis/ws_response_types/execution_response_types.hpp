#pragma once
#include <string>
#include <cmath>
#include "common/market_msg_types.hpp"

namespace Omni::KIS::KoreanDerivatives {

using Omni::ExecutionData;

struct DayExecutionResponse {
    std::string CUST_ID, ACNT_NO, ODER_NO, OODER_NO, SELN_BYOV_CLS, RCTF_CLS, ODER_KIND2;
    std::string STCK_SHRN_ISCD, CNTG_QTY, CNTG_UNPR, STCK_CNTG_HOUR, RFUS_YN, CNTG_YN, ACPT_YN;
    std::string BRNC_NO, ODER_QTY, ACNT_NAME, CNTG_ISNM, ODER_COND, ORD_GRP, ORD_GRPSEQ, ORDER_PRC;

    static constexpr size_t field_cnt = 22;

    ExecutionData normalize() {
        bool is_executed = (CNTG_YN == "2");
        return ExecutionData{
            .account_no = ACNT_NO,
            .order_no = ODER_NO,
            .original_order_no = OODER_NO,
            .short_product = STCK_SHRN_ISCD,
            .full_product = CNTG_ISNM,
            .is_accept_data = !is_executed,
            .is_place = (RCTF_CLS == "0"),
            .is_cancel = (RCTF_CLS == "2"),
            .is_bid = (SELN_BYOV_CLS == "02"),
            .is_accepted = is_executed ? false : (ACPT_YN == "1"),
            .is_rejected = is_executed ? false : !RFUS_YN.empty(),
            .is_executed = is_executed,
            .order_price = ORDER_PRC.empty() ? NAN : std::stod(ORDER_PRC),
            .order_qty = ODER_QTY.empty() ? NAN : std::stod(ODER_QTY),
            .execute_price = CNTG_UNPR.empty() ? NAN : std::stod(CNTG_UNPR),
            .execute_qty = CNTG_QTY.empty() ? NAN : std::stod(CNTG_QTY)
        };
    }
};


struct NightExecutionResponse {
    std::string CUST_ID, ACNT_NO, ODER_NO, OODER_NO, SELN_BYOV_CLS, RCTF_CLS, ODER_KIND2;
    std::string STCK_SHRN_ISCD, CNTG_QTY, CNTG_UNPR, STCK_CNTG_HOUR, RFUS_YN, CNTG_YN, ACPT_YN;
    std::string BRNC_NO, ODER_QTY, ACNT_NAME, CNTG_ISNM, ODER_COND;

    static constexpr size_t field_cnt = 19;

    ExecutionData normalize() {
        bool is_executed = (CNTG_YN == "2");
        return ExecutionData{
            .account_no = ACNT_NO,
            .order_no = ODER_NO,
            .original_order_no = OODER_NO,
            .short_product = STCK_SHRN_ISCD,
            .full_product = CNTG_ISNM,
            .is_accept_data = !is_executed,
            .is_bid = (SELN_BYOV_CLS == "02"),
            .is_accepted = is_executed ? false : (ACPT_YN == "1"),
            .is_rejected = is_executed ? false : !RFUS_YN.empty(),
            .is_executed = is_executed,
            .order_qty = ODER_QTY.empty() ? NAN : std::stod(ODER_QTY),
            .execute_price = CNTG_UNPR.empty() ? NAN : std::stod(CNTG_UNPR),
            .execute_qty = CNTG_QTY.empty() ? NAN : std::stod(CNTG_QTY)
        };
    }
};

} // namespace Omni::KIS::KoreanDerivatives
