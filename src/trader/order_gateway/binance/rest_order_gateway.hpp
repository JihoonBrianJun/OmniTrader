#pragma once
#include <string>
#include <memory>
#include <map>
#include <quill/Logger.h>

#include "config_handlers/signer.hpp"
#include "trader/order_gateway/order_gateway.hpp"
#include "connection_handlers/rest/binance/binance_rest_client.hpp"


namespace Omni::Binance {

namespace OG = Omni::OrderGateway;

// Signed REST order gateway: POST/PUT/DELETE /fapi/v1/order (HMAC-SHA256).
class RestOrderGateway : public OG::IOrderGateway {
    public:
        RestOrderGateway(
            quill::Logger* logger,
            const std::string& rest_domain,
            std::shared_ptr<Omni::Config::ISigner> signer,
            std::shared_ptr<BinanceRestClient> rest_client
        );

        void place_order(const OG::OrderPlaceInfo& info, OG::OrderResponse& response) override;
        void amend_order(const OG::OrderAmendInfo& info, OG::OrderResponse& response) override;
        void cancel_order(const OG::OrderCancelInfo& info, OG::OrderResponse& response) override;

    private:
        quill::Logger* logger_;
        std::string rest_domain_;
        std::shared_ptr<Omni::Config::ISigner> signer_;
        std::shared_ptr<BinanceRestClient> rest_client_;

        // method: POST/PUT/DELETE; params: "k=v&..." without timestamp/signature.
        void send_order(
            const std::string& method, const std::string& params, OG::OrderResponse& response
        );
};

} // namespace Omni::Binance
