#pragma once

#include <string>
#include <map>
#include <curl/curl.h>
#include <quill/Logger.h>

namespace Omni::Connection {

struct RestResponse {
    bool success;
    std::string error_msg = "";
    int status_code = 0;
    std::string body = "";
    std::map<std::string, std::string> headers = {};
};

class RestClient {
    public:
        RestClient(quill::Logger* logger);
        ~RestClient();

        RestResponse get(
            const std::string& url,
            const std::map<std::string, std::string>& headers = {}
        );
        RestResponse post(
            const std::string& url,
            const std::string& body,
            const std::map<std::string, std::string>& headers = {}
        );
        RestResponse put(
            const std::string& url,
            const std::string& body,
            const std::map<std::string, std::string>& headers = {}
        );
        RestResponse del(
            const std::string& url,
            const std::map<std::string, std::string>& headers = {}
        );

        // Whole-transfer timeout for subsequent calls on this client. The 30 s
        // default suits a bulk fetch (exchangeInfo, a depth snapshot); an order does
        // not, since the caller is waiting on it and a shutdown drain waits on the
        // caller. The order gateways set a much tighter bound.
        void set_timeout_sec(long seconds) { timeout_sec_ = seconds > 0 ? seconds : 1; }

        // RFC-3986 percent-encoding (used when building signed query strings).
        static std::string url_encode(const std::string& value);

    private:
        quill::Logger* logger_;
        CURL* curl_;
        long timeout_sec_ = 30;

        RestResponse perform(
            const std::string& method,
            const std::string& url,
            const std::string* body,
            const std::map<std::string, std::string>& headers
        );

        static size_t write_callback(
            void* contents, size_t size, size_t nmemb, std::string* data
        );
        static size_t header_callback(
            void* contents, size_t size, size_t nmemb,
            std::map<std::string, std::string>* headers
        );
};

} // namespace Omni::Connection
