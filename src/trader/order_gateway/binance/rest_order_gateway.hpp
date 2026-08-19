#pragma once
#include <string>
#include <memory>
#include <map>
#include <quill/Logger.h>

#include "config_handlers/signer.hpp"
#include "trader/order_gateway/order_gateway.hpp"
#include "connection_handlers/rest/binance/binance_rest_client.hpp"
#include "utils/task_runner.hpp"


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

        // Waits for any request still in flight. Must happen here rather than be left
        // to ~IOrderGateway: the tasks call deliver(), which reads a member of the
        // base, and a base is torn down after its derived part.
        ~RestOrderGateway() override;

        bool place_order(const OG::OrderPlaceInfo& info) override;
        bool amend_order(const OG::OrderAmendInfo& info) override;
        bool cancel_order(const OG::OrderCancelInfo& info) override;

    private:
        quill::Logger* logger_;
        std::string rest_domain_;
        std::shared_ptr<Omni::Config::ISigner> signer_;
        std::shared_ptr<BinanceRestClient> rest_client_;

        // One worker, running requests in the order they were posted. curl is
        // synchronous and the trader thread cannot afford to sit inside it, but the
        // venue must still see a cancel before the place that replaces it. Declared
        // last so it is destroyed (and drained) before anything its tasks touch.
        Omni::SerialTaskRunner runner_;

        // method: POST/PUT/DELETE; params: "k=v&..." without timestamp/signature.
        // Queues the blocking HTTP call on the gateway's worker and returns; the
        // response (tagged with cid) is delivered through the sink from that worker,
        // after every request queued before it. Returns false if it could not be
        // queued at all.
        bool send_order(const std::string& method, std::string params, uint64_t cid);

        // Reports a request that never left as a failed response, so the trader stops
        // waiting on it instead of holding a phantom order until its timeout.
        void deliver_dispatch_failure(uint64_t cid, const std::string& why);
};

} // namespace Omni::Binance
