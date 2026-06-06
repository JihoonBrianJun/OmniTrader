#include <array>
#include <cstdio>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include "signer.hpp"

namespace Omni::Config {

HmacSha256Signer::HmacSha256Signer(std::string api_key, std::string secret_key)
:   api_key_(std::move(api_key)),
    secret_key_(std::move(secret_key))
{
}


std::string HmacSha256Signer::sign(const std::string& payload) const {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;

    HMAC(
        EVP_sha256(),
        secret_key_.data(), static_cast<int>(secret_key_.size()),
        reinterpret_cast<const unsigned char*>(payload.data()), payload.size(),
        digest, &digest_len
    );

    std::string hex;
    hex.reserve(digest_len * 2);
    char buf[3];
    for (unsigned int i = 0; i < digest_len; ++i) {
        std::snprintf(buf, sizeof(buf), "%02x", digest[i]);
        hex.append(buf, 2);
    }
    return hex;
}

} // namespace Omni::Config
