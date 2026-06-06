#pragma once
#include <set>
#include "product_types.hpp"
#include "code_manager_base.hpp"


namespace Omni::KIS::KoreanDerivatives {

namespace CM = Omni::KIS::CodeManager;

class CodeManager : public CM::BaseCodeManger<ProductType> {
    public:
        CodeManager(
            const std::vector<std::string>& codes,
            const std::string& codes_db_path,
            const std::string& hts_id,
            bool is_night,
            quill::Logger* logger
        );

        std::string get_full_code(const std::string& code) override;

        size_t parse_orderbook_data(
            const std::string& code, size_t offset, const std::vector<std::string>& data,
            Omni::OrderbookMsg& parsed_msg
        ) override;
        size_t parse_trade_data(
            const std::string& code, size_t offset, const std::vector<std::string>& data,
            Omni::TradeMsg& parsed_msg
        ) override;
        size_t parse_execution_data(
            size_t offset, const std::vector<std::string>& data,
            Omni::ExecutionMsg& parsed_msg
        ) override;

    private:
        std::set<std::string> idx_fo_codes_, stock_fo_codes_, com_fo_codes_, night_option_codes_;

        void load_all_codes() override;

        std::string get_short_code(const std::string& full_code) override;
        ProductType get_product_type(const std::string& code, bool verbose = true) override;

        CM::SubscriptionInput get_orderbook_subscription_input(const std::string& code) override;
        CM::SubscriptionInput get_trade_subscription_input(const std::string& code) override;
        CM::SubscriptionInput get_execution_subscription_input(const std::string& code) override;
};

} // namespace Omni::KIS::KoreanDerivatives
