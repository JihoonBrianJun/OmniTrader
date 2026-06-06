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
};

} // namespace Omni::Listener
