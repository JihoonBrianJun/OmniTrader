#include "ws_response_types/orderbook_response_types.hpp"
#include "ws_response_types/trade_response_types.hpp"
#include "ws_response_types/execution_response_types.hpp"
#include "product_manager.hpp"


namespace Omni::KIS::KoreanDerivatives {

ProductManager::ProductManager(
    const std::vector<std::string>& products,
    const std::string& products_db_path,
    const std::string& hts_id,
    bool is_night,
    quill::Logger* logger
)
:   CM::BaseProductManger<ProductType>(products, products_db_path, hts_id, is_night, logger)
{
    product_manager_name_ = "KoreanDerivatives";
    load_all_products();
    tr_id_types_ = std::map<std::string, CM::TrIdType>{
        {"H0IFASP0", CM::TrIdType::OrderbookTrId},
        {"H0IOASP0", CM::TrIdType::OrderbookTrId},
        {"H0ZFASP0", CM::TrIdType::OrderbookTrId},
        {"H0ZOASP0", CM::TrIdType::OrderbookTrId},
        {"H0CFASP0", CM::TrIdType::OrderbookTrId},
        {"H0MFASP0", CM::TrIdType::OrderbookTrId},
        {"H0EUASP0", CM::TrIdType::OrderbookTrId},
        {"H0IFCNT0", CM::TrIdType::TradeTrId},
        {"H0IOCNT0", CM::TrIdType::TradeTrId},
        {"H0ZFCNT0", CM::TrIdType::TradeTrId},
        {"H0ZOCNT0", CM::TrIdType::TradeTrId},
        {"H0CFCNT0", CM::TrIdType::TradeTrId},
        {"H0MFCNT0", CM::TrIdType::TradeTrId},
        {"H0EUCNT0", CM::TrIdType::TradeTrId},
        {"H0IFCNI0", CM::TrIdType::ExecutionTrId},
        {"H0MFCNI0", CM::TrIdType::ExecutionTrId}
    };
}


void ProductManager::load_all_products() {
    idx_fo_products_ = read_products_file("fo_idx_code_mts.csv", "단축코드");
    stock_fo_products_ = read_products_file("fo_stk_code_mts.csv", "단축코드");
    com_fo_products_ = read_products_file("fo_com_code.csv", "단축코드");
    night_option_products_ = read_products_file("fo_eurex_code.csv", "단축코드");
}


std::string ProductManager::get_full_product(const std::string& product) {
    return product;
}


std::string ProductManager::get_short_product(const std::string& full_product) {
    return full_product;
}


ProductType ProductManager::get_product_type(const std::string& product, bool verbose) {
    if (is_night_) {
        if (idx_fo_products_.find(product) != idx_fo_products_.end()) {
            return ProductType::NightFutures;
        } else if (stock_fo_products_.find(product) != stock_fo_products_.end()) {
            return ProductType::NightFutures;
        } else if (com_fo_products_.find(product) != com_fo_products_.end()) {
            return ProductType::NightFutures;
        } else if (night_option_products_.find(product) != night_option_products_.end()) {
            return ProductType::NightOption;
        } else {
            if (verbose) LOG_WARNING(logger_, "Product {} does not exist in the DB", product);
        }
    } else {
        if (idx_fo_products_.find(product) != idx_fo_products_.end()) {
            if (product.length() <= 6) return ProductType::IndexFutures;
            else return ProductType::IndexOption;
        } else if (stock_fo_products_.find(product) != stock_fo_products_.end()) {
            if (product.length() <= 6) return ProductType::StockFutures;
            else return ProductType::StockOption;
        } else if (com_fo_products_.find(product) != com_fo_products_.end()) {
            return ProductType::CommodityFutures;
        } else {
            if (verbose) LOG_WARNING(logger_, "Product {} does not exist in the DB", product);
        }
    }
    return ProductType::Error;
}


CM::SubscriptionInput ProductManager::get_orderbook_subscription_input(const std::string& product) {
    switch (get_product_type(product)) {
        case ProductType::IndexFutures:     return {"H0IFASP0", get_full_product(product)};
        case ProductType::IndexOption:      return {"H0IOASP0", get_full_product(product)};
        case ProductType::StockFutures:     return {"H0ZFASP0", get_full_product(product)};
        case ProductType::StockOption:      return {"H0ZOASP0", get_full_product(product)};
        case ProductType::CommodityFutures: return {"H0CFASP0", get_full_product(product)};
        case ProductType::NightFutures:     return {"H0MFASP0", get_full_product(product)};
        case ProductType::NightOption:      return {"H0EUASP0", get_full_product(product)};
        case ProductType::Error:            return {"", ""};
    }
    return {"", ""};
}


CM::SubscriptionInput ProductManager::get_trade_subscription_input(const std::string& product) {
    switch (get_product_type(product)) {
        case ProductType::IndexFutures:     return {"H0IFCNT0", get_full_product(product)};
        case ProductType::IndexOption:      return {"H0IOCNT0", get_full_product(product)};
        case ProductType::StockFutures:     return {"H0ZFCNT0", get_full_product(product)};
        case ProductType::StockOption:      return {"H0ZOCNT0", get_full_product(product)};
        case ProductType::CommodityFutures: return {"H0CFCNT0", get_full_product(product)};
        case ProductType::NightFutures:     return {"H0MFCNT0", get_full_product(product)};
        case ProductType::NightOption:      return {"H0EUCNT0", get_full_product(product)};
        case ProductType::Error:            return {"", ""};
    }
    return {"", ""};
}


CM::SubscriptionInput ProductManager::get_execution_subscription_input(const std::string& product) {
    switch (get_product_type(product)) {
        case ProductType::IndexFutures:
        case ProductType::IndexOption:
        case ProductType::StockFutures:
        case ProductType::StockOption:
        case ProductType::CommodityFutures: return {"H0IFCNI0", get_full_product(product)};
        case ProductType::NightFutures:
        case ProductType::NightOption:      return {"H0MFCNI0", hts_id_};
        case ProductType::Error:            return {"", ""};
    }
    return {"", ""};
}


size_t ProductManager::parse_orderbook_data(
    const std::string& product, size_t offset, const std::vector<std::string>& data,
    Omni::OrderbookMsg& parsed_msg
) {
    parsed_msg.product = product;

    switch (get_product_type(get_short_product(product))) {
        case ProductType::IndexFutures:
            parsed_msg.orderbook_data = vector_to_struct_impl<IndexFuturesOrderbookResponse>(
                data, offset, std::make_index_sequence<IndexFuturesOrderbookResponse::field_cnt>{}
            ).normalize();
            return IndexFuturesOrderbookResponse::field_cnt;
        case ProductType::IndexOption:
            parsed_msg.orderbook_data = vector_to_struct_impl<IndexOptionOrderbookResponse>(
                data, offset, std::make_index_sequence<IndexOptionOrderbookResponse::field_cnt>{}
            ).normalize();
            return IndexOptionOrderbookResponse::field_cnt;
        case ProductType::StockFutures:
            parsed_msg.orderbook_data = vector_to_struct_impl<StockFuturesOrderbookResponse>(
                data, offset, std::make_index_sequence<StockFuturesOrderbookResponse::field_cnt>{}
            ).normalize();
            return StockFuturesOrderbookResponse::field_cnt;
        case ProductType::StockOption:
            parsed_msg.orderbook_data = vector_to_struct_impl<StockOptionOrderbookResponse>(
                data, offset, std::make_index_sequence<StockOptionOrderbookResponse::field_cnt>{}
            ).normalize();
            return StockOptionOrderbookResponse::field_cnt;
        case ProductType::CommodityFutures:
            parsed_msg.orderbook_data = vector_to_struct_impl<CommodityFuturesOrderbookResponse>(
                data, offset, std::make_index_sequence<CommodityFuturesOrderbookResponse::field_cnt>{}
            ).normalize();
            return CommodityFuturesOrderbookResponse::field_cnt;
        case ProductType::NightFutures:
            parsed_msg.orderbook_data = vector_to_struct_impl<NightFuturesOrderbookResponse>(
                data, offset, std::make_index_sequence<NightFuturesOrderbookResponse::field_cnt>{}
            ).normalize();
            return NightFuturesOrderbookResponse::field_cnt;
        case ProductType::NightOption:
            parsed_msg.orderbook_data = vector_to_struct_impl<NightOptionOrderbookResponse>(
                data, offset, std::make_index_sequence<NightOptionOrderbookResponse::field_cnt>{}
            ).normalize();
            return NightOptionOrderbookResponse::field_cnt;
        case ProductType::Error:
            parsed_msg.orderbook_data = Omni::OrderbookData{};
            return 0;
    }
    return 0;
}


size_t ProductManager::parse_trade_data(
    const std::string& product, size_t offset, const std::vector<std::string>& data,
    Omni::TradeMsg& parsed_msg
) {
    parsed_msg.product = product;

    switch (get_product_type(get_short_product(product))) {
        case ProductType::IndexFutures:
            parsed_msg.trade_data = vector_to_struct_impl<IndexFuturesTradeResponse>(
                data, offset, std::make_index_sequence<IndexFuturesTradeResponse::field_cnt>{}
            ).normalize();
            return IndexFuturesTradeResponse::field_cnt;
        case ProductType::IndexOption:
            parsed_msg.trade_data = vector_to_struct_impl<IndexOptionTradeResponse>(
                data, offset, std::make_index_sequence<IndexOptionTradeResponse::field_cnt>{}
            ).normalize();
            return IndexOptionTradeResponse::field_cnt;
        case ProductType::StockFutures:
            parsed_msg.trade_data = vector_to_struct_impl<StockFuturesTradeResponse>(
                data, offset, std::make_index_sequence<StockFuturesTradeResponse::field_cnt>{}
            ).normalize();
            return StockFuturesTradeResponse::field_cnt;
        case ProductType::StockOption:
            parsed_msg.trade_data = vector_to_struct_impl<StockOptionTradeResponse>(
                data, offset, std::make_index_sequence<StockOptionTradeResponse::field_cnt>{}
            ).normalize();
            return StockOptionTradeResponse::field_cnt;
        case ProductType::CommodityFutures:
            parsed_msg.trade_data = vector_to_struct_impl<CommodityFuturesTradeResponse>(
                data, offset, std::make_index_sequence<CommodityFuturesTradeResponse::field_cnt>{}
            ).normalize();
            return CommodityFuturesTradeResponse::field_cnt;
        case ProductType::NightFutures:
            parsed_msg.trade_data = vector_to_struct_impl<NightFuturesTradeResponse>(
                data, offset, std::make_index_sequence<NightFuturesTradeResponse::field_cnt>{}
            ).normalize();
            return NightFuturesTradeResponse::field_cnt;
        case ProductType::NightOption:
            parsed_msg.trade_data = vector_to_struct_impl<NightOptionTradeResponse>(
                data, offset, std::make_index_sequence<NightOptionTradeResponse::field_cnt>{}
            ).normalize();
            return NightOptionTradeResponse::field_cnt;
        case ProductType::Error:
            parsed_msg.trade_data = Omni::TradeData{};
            return 0;
    }
    return 0;
}


size_t ProductManager::parse_execution_data(
    size_t offset, const std::vector<std::string>& data,
    Omni::ExecutionMsg& parsed_msg
) {
    ProductType product_type = ProductType::Error;
    for (size_t idx = offset; idx < data.size(); ++idx) {
        if (data[idx].empty()) continue;
        product_type = get_product_type(data[idx], false);
        if (product_type != ProductType::Error) {
            parsed_msg.product = get_full_product(data[idx]);
            break;
        }
    }
    if (product_type == ProductType::Error) {
        LOG_WARNING(logger_, "Failed to parse execution data");
        parsed_msg.execution_data = Omni::ExecutionData{};
        return 0;
    }

    switch (product_type) {
        case ProductType::NightFutures:
        case ProductType::NightOption:
            parsed_msg.execution_data = vector_to_struct_impl<NightExecutionResponse>(
                data, offset, std::make_index_sequence<NightExecutionResponse::field_cnt>{}
            ).normalize();
            return NightExecutionResponse::field_cnt;
        default:
            parsed_msg.execution_data = vector_to_struct_impl<DayExecutionResponse>(
                data, offset, std::make_index_sequence<DayExecutionResponse::field_cnt>{}
            ).normalize();
            return DayExecutionResponse::field_cnt;
    }
}

} // namespace Omni::KIS::KoreanDerivatives
