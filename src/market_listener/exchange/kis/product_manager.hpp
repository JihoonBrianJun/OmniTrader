#pragma once
#include <set>
#include "product_types.hpp"
#include "product_manager_base.hpp"


namespace Omni::KIS::KoreanDerivatives {

namespace CM = Omni::KIS::ProductManager;

class ProductManager : public CM::BaseProductManger<ProductType> {
    public:
        ProductManager(
            const std::vector<std::string>& products,
            const std::string& products_db_path,
            const std::string& hts_id,
            bool is_night,
            quill::Logger* logger
        );

        std::string get_full_product(const std::string& product) override;

        size_t parse_orderbook_data(
            const std::string& product, size_t offset, const std::vector<std::string>& data,
            Omni::OrderbookMsg& parsed_msg
        ) override;
        size_t parse_trade_data(
            const std::string& product, size_t offset, const std::vector<std::string>& data,
            Omni::TradeMsg& parsed_msg
        ) override;
        size_t parse_execution_data(
            size_t offset, const std::vector<std::string>& data,
            Omni::ExecutionMsg& parsed_msg
        ) override;

    private:
        std::set<std::string> idx_fo_products_, stock_fo_products_, com_fo_products_, night_option_products_;

        void load_all_products() override;

        std::string get_short_product(const std::string& full_product) override;
        ProductType get_product_type(const std::string& product, bool verbose = true) override;

        CM::SubscriptionInput get_orderbook_subscription_input(const std::string& product) override;
        CM::SubscriptionInput get_trade_subscription_input(const std::string& product) override;
        CM::SubscriptionInput get_execution_subscription_input(const std::string& product) override;
};

} // namespace Omni::KIS::KoreanDerivatives
