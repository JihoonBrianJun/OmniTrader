#pragma once
#include "base_order_gateway.hpp"

namespace Omni::KIS::OrderGateway {

class KoreanStockOrderGateway : public BaseOrderGateway {
    public:
        KoreanStockOrderGateway(quill::Logger* logger, const std::string& http_domain);

        // Drains the in-flight order requests while this object is still whole. They
        // call parse_order_response, overridden below, so waiting for them in the base
        // destructor would be too late -- the override would already be gone.
        ~KoreanStockOrderGateway() override { drain_pending(); }

        void parse_order_response(
            const std::string& order_response, OG::OrderResponse& parsed_response
        ) override;

    private:
        std::string convert_product_for_order(const std::string& product) override;

        void write_balance_url() override;
        void write_order_place_url() override;
        void write_order_change_url() override;

        void write_balance_header() override;
        void write_order_place_header(bool is_bid) override;
        void write_order_change_header() override;

        void write_balance_params_list() override;

        void write_order_place_params(
            std::string& order_place_params, const OG::OrderPlaceInfo& order_place_info
        ) override;
        void write_order_amend_params(
            std::string& order_amend_params, const OG::OrderAmendInfo& order_amend_info
        ) override;
        void write_order_cancel_params(
            std::string& order_cancel_params, const OG::OrderCancelInfo& order_cancel_info
        ) override;
};

} // namespace Omni::KIS::OrderGateway
