#pragma once
#include <string>
#include <map>
#include <vector>
#include <quill/Logger.h>

#include "config_handlers/kis/kis_config.hpp"
#include "trader/order_gateway/order_gateway.hpp"
#include "utils/task_runner.hpp"


namespace Omni::KIS::OrderGateway {

namespace OG = Omni::OrderGateway;

// KIS REST order gateway base (ported from kis_cpp BaseOrderManager). Concrete
// markets fill in URLs, tr_id headers, param bodies and response parsing.
class BaseOrderGateway : public OG::IOrderGateway {
    public:
        BaseOrderGateway(quill::Logger* logger, const std::string& http_domain);

        // Drains as a last resort. It is NOT enough on its own: the dispatched tasks
        // call parse_order_response, which is pure virtual here, and by the time a
        // base destructor runs the override is already gone. Every concrete gateway
        // must call drain_pending() in its own destructor -- this one only catches the
        // case where one forgot, and by then the vtable is already the base's.
        ~BaseOrderGateway() override;

        void get_balances(std::vector<std::string>& balance_responses);

        bool place_order(const OG::OrderPlaceInfo& info) override;
        bool amend_order(const OG::OrderAmendInfo& info) override;
        bool cancel_order(const OG::OrderCancelInfo& info) override;

        virtual void parse_order_response(
            const std::string& order_response, OG::OrderResponse& parsed_response
        ) = 0;

    protected:
        quill::Logger* logger_;

        // One worker, running requests in the order they were posted. KIS is
        // REST-only, so leaving these synchronous meant the trader thread sat inside
        // curl for the whole round trip on every single order.
        Omni::SerialTaskRunner runner_;

        // Wait for in-flight order requests. Call this first thing in the destructor
        // of any class deriving from this one; see ~BaseOrderGateway.
        void drain_pending() { runner_.drain(); }

        // Report a request that never left as a failed response, so the trader stops
        // waiting on a reply that is not coming.
        void deliver_dispatch_failure(uint64_t cid);

        // Shared body of place/amend/cancel: queue one POST on the gateway's worker
        // and deliver the parsed reply from there. `what` only labels the log line.
        bool send_order_request(
            const std::string& what, std::string url, std::string params,
            std::map<std::string, std::string> headers, uint64_t cid
        );

        Config::AccountInfo account_info_;
        std::string http_domain_, balance_url_, order_place_url_, order_change_url_;

        std::map<std::string, std::string> common_rest_header_, balance_header_;
        std::map<std::string, std::string> bid_order_place_header_, ask_order_place_header_;
        std::map<std::string, std::string> order_change_header_;

        std::vector<std::string> balance_params_list_;

        void initalize();

        virtual std::string convert_product_for_order(const std::string& product) = 0;

        virtual void write_balance_url() = 0;
        virtual void write_order_place_url() = 0;
        virtual void write_order_change_url() = 0;

        void write_common_rest_header();
        void write_rest_header(std::map<std::string, std::string>& rest_header, const std::string& tr_id);
        virtual void write_balance_header() = 0;
        virtual void write_order_place_header(bool is_bid) = 0;
        virtual void write_order_change_header() = 0;

        virtual void write_balance_params_list() = 0;

        virtual void write_order_place_params(
            std::string& order_place_params, const OG::OrderPlaceInfo& order_place_info
        ) = 0;
        virtual void write_order_amend_params(
            std::string& order_amend_params, const OG::OrderAmendInfo& order_amend_info
        ) = 0;
        virtual void write_order_cancel_params(
            std::string& order_cancel_params, const OG::OrderCancelInfo& order_cancel_info
        ) = 0;

        std::string double_to_string(double value);
};

} // namespace Omni::KIS::OrderGateway
