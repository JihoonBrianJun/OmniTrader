#pragma once
#include <string>
#include <vector>

namespace Omni::KIS::OrderGateway {

struct BalanceParams {
    std::string CANO;
    std::string ACNT_PRDT_CD;
    std::string AFHR_FLPR_YN;
    std::string OFL_YN;
    std::string INQR_DVSN;
    std::string UNPR_DVSN;
    std::string FUND_STTL_ICLD_YN;
    std::string FNCG_AMT_AUTO_RDPT_YN;
    std::string PRCS_DVSN;
    std::string CTX_AREA_FK100;
    std::string CTX_AREA_NK100;
};

struct OrderPlaceParams {
    std::string CANO;
    std::string ACNT_PRDT_CD;
    std::string PDNO;
    std::string ORD_DVSN;
    std::string ORD_QTY;
    std::string ORD_UNPR;
};

struct OrderAmendParams {
    std::string CANO;
    std::string ACNT_PRDT_CD;
    std::string KRX_FWDG_ORD_ORGNO;
    std::string ORGN_ODNO;
    std::string ORD_DVSN;
    std::string RVSE_CNCL_DVSN_CD;
    std::string ORD_QTY;
    std::string ORD_UNPR;
    std::string QTY_ALL_ORD_YN;
};

struct OrderCancelParams {
    std::string CANO;
    std::string ACNT_PRDT_CD;
    std::string KRX_FWDG_ORD_ORGNO;
    std::string ORGN_ODNO;
    std::string ORD_DVSN;
    std::string RVSE_CNCL_DVSN_CD;
    std::string ORD_QTY;
    std::string ORD_UNPR;
    std::string QTY_ALL_ORD_YN;
};

struct OrderResponseBodyOutput {
    std::string KRX_FWDG_ORD_ORGNO;
    std::string ODNO;
    std::string ORD_TMD;
};
struct OrderResponseBody {
    std::string rt_cd;
    std::string msg_cd;
    std::string msg1;
    std::vector<OrderResponseBodyOutput> output;
};

} // namespace Omni::KIS::OrderGateway
