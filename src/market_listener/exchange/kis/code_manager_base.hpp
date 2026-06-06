#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <fstream>
#include <sstream>
#include <utility>

#include <fmt/core.h>
#include <quill/Logger.h>
#include <quill/LogMacros.h>

#include "common/market_msg_types.hpp"


namespace Omni::KIS::CodeManager {

struct SubscriptionInput {
    std::string tr_id;
    std::string tr_key;
};

enum TrIdType {
    OrderbookTrId,
    TradeTrId,
    ExecutionTrId,
    Error
};


class ICodeManager {
    public:
        virtual ~ICodeManager() = default;

        virtual std::string get_code_manager_name() = 0;

        virtual SubscriptionInput get_orderbook_subscription_input(const std::string& code) = 0;
        virtual SubscriptionInput get_trade_subscription_input(const std::string& code) = 0;
        virtual SubscriptionInput get_execution_subscription_input(const std::string& code) = 0;

        virtual std::string get_full_code(const std::string& code) = 0;
        virtual TrIdType get_tr_id_type(const std::string& tr_id) = 0;

        virtual size_t parse_orderbook_data(
            const std::string& code, size_t offset, const std::vector<std::string>& data,
            Omni::OrderbookMsg& parsed_msg
        ) = 0;
        virtual size_t parse_trade_data(
            const std::string& code, size_t offset, const std::vector<std::string>& data,
            Omni::TradeMsg& parsed_msg
        ) = 0;
        virtual size_t parse_execution_data(
            size_t offset, const std::vector<std::string>& data,
            Omni::ExecutionMsg& parsed_msg
        ) = 0;
};


template<typename ProductTypeEnum>
class BaseCodeManger : public ICodeManager {
    public:
        struct CodeInfo {
            std::string short_code;
            ProductTypeEnum product_type;
        };

        BaseCodeManger(
            const std::vector<std::string>& codes,
            const std::string& codes_db_path,
            const std::string& hts_id,
            bool is_night,
            quill::Logger* logger
        )
        :   codes_(codes),
            codes_db_path_(codes_db_path),
            hts_id_(hts_id),
            is_night_(is_night),
            logger_(logger)
        {
        }

        virtual ~BaseCodeManger() = default;

        std::string get_code_manager_name() override {
            return code_manager_name_;
        }

        TrIdType get_tr_id_type(const std::string& tr_id) override {
            auto tr_id_it = tr_id_types_.find(tr_id);
            if (tr_id_it == tr_id_types_.end()) return TrIdType::Error;
            else return tr_id_it->second;
        }

    protected:
        std::string code_manager_name_;
        const std::vector<std::string> codes_;
        const std::string codes_db_path_, hts_id_;
        bool is_night_;
        quill::Logger* logger_;

        std::map<std::string, TrIdType> tr_id_types_;

        std::set<std::string> read_codes_file(
            const std::string& file_name, const std::string& codes_col_name
        ) {
            std::set<std::string> result;
            std::ifstream file(fmt::format("{}/{}", codes_db_path_, file_name));
            if (!file.is_open()) {
                return result;
            }

            std::string line;

            int target_column_index = -1;
            if (std::getline(file, line)) {
                std::stringstream ss(line);
                std::string cell;
                int col_index = 0;
                while (std::getline(ss, cell, ',')) {
                    if (cell == codes_col_name) {
                        target_column_index = col_index;
                        break;
                    }
                    col_index++;
                }
            }
            if (target_column_index == -1) {
                return result;
            }

            while (std::getline(file, line)) {
                std::stringstream ss(line);
                std::string cell;
                int col_index = 0;
                while (std::getline(ss, cell, ',')) {
                    if (col_index == target_column_index) {
                        result.insert(cell);
                        break;
                    }
                    col_index++;
                }
            }

            return result;
        }

        virtual void load_all_codes() = 0;

        virtual std::string get_short_code(const std::string& full_code) = 0;
        virtual ProductTypeEnum get_product_type(const std::string& code, bool verbose = true) = 0;

        template<typename T, std::size_t... Indices>
        T vector_to_struct_impl(
            const std::vector<std::string>& vec,
            size_t offset,
            std::index_sequence<Indices...>
        ) {
            if (offset + sizeof...(Indices) > vec.size()) {
                LOG_WARNING(
                    logger_, "Wrong data size: vec.size()={}, offset={}, sizeof(Indices)={}",
                    vec.size(), offset, sizeof...(Indices)
                );
                return T{};
            }
            return T{vec[offset+Indices]...};
        }
};

} // namespace Omni::KIS::CodeManager
