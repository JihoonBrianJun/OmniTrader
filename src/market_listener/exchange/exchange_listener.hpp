#pragma once
#include <memory>

#include <quill/Logger.h>
#include <moodycamel/blockingconcurrentqueue.h>

#include "market_listener/market_listener_dtypes.hpp"

namespace Omni::Listener {

// An exchange adapter fully owns its connection topology (one socket for KIS;
// market + user sockets plus REST snapshot resync for Binance). It translates
// native wire data into normalized ListenerEvents and pushes them into the
// queue handed in at construction. Reconnect/snapshot orchestration lives here
// because it is inherently exchange-specific.
class IExchangeListener {
    public:
        virtual ~IExchangeListener() = default;

        virtual void start() = 0;
        virtual void stop() = 0;

        // Fetch this exchange's per-product trading grids and enqueue one
        // ProductInfoMsg per product. MarketListener calls this once at startup and
        // then on --product_info_refresh_sec, always from the same thread, so an
        // implementation needs no locking of its own.
        //
        // Default: nothing. An exchange with no per-product grid to query (KIS,
        // where tick size is a function of price rather than a product constant)
        // simply does not override it, and publishes no product_info at all. Making
        // that a defaulted no-op rather than a pure virtual is what keeps "this
        // exchange has no product info" a statement in the type system instead of an
        // empty body every adapter has to remember to write.
        virtual void publish_product_info() {}
};

} // namespace Omni::Listener
