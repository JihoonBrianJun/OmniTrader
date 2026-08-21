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

        // Enqueue this account's authoritative user state -- positions/balances and
        // resting orders -- as normalized messages, from whatever snapshot endpoint
        // the exchange provides.
        //
        // Called on every (re)connect of the user stream, and again whenever a client
        // subscribes: the stream only reports *changes*, so a trader that connects
        // between two changes would otherwise never learn the position it already
        // has, and would trade as though it were flat. That was not visible while
        // the user socket was dropping every few minutes and re-seeding as it came
        // back; with a stable socket the seed happens once, and a late subscriber
        // misses it for good.
        //
        // Default: nothing, for an exchange with no such snapshot.
        virtual void publish_user_state() {}
};

} // namespace Omni::Listener
