#pragma once
#include <string>
#include <memory>

namespace Omni::Config {

// Request signer abstraction. Exchanges that sign each request (e.g. Binance)
// implement this; exchanges that use opaque session tokens (e.g. KIS) do not.
class ISigner {
    public:
        virtual ~ISigner() = default;

        // Returns the signature for the given payload (typically a query string).
        virtual std::string sign(const std::string& payload) const = 0;

        // The value to send in the API-key header (e.g. Binance X-MBX-APIKEY).
        virtual std::string api_key() const = 0;
};

// HMAC-SHA256 over the payload, returned as lowercase hex (Binance scheme).
class HmacSha256Signer : public ISigner {
    public:
        HmacSha256Signer(std::string api_key, std::string secret_key);

        std::string sign(const std::string& payload) const override;
        std::string api_key() const override { return api_key_; }

    private:
        std::string api_key_;
        std::string secret_key_;
};

} // namespace Omni::Config
