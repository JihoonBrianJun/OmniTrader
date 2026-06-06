#pragma once

#include <string>
#include <map>
#include <curl/curl.h>
#include <quill/Logger.h>

namespace Omni::Connection {

struct HttpResponse {
    bool success;
    std::string error_msg = "";
    int status_code = 0;
    std::string body = "";
    std::map<std::string, std::string> headers = {};
};

class HttpClient {
    public:
        HttpClient(quill::Logger* logger);
        ~HttpClient();

        HttpResponse get(
            const std::string& url,
            const std::map<std::string, std::string>& headers = {}
        );
        HttpResponse post(
            const std::string& url,
            const std::string& body,
            const std::map<std::string, std::string>& headers = {}
        );
        HttpResponse put(
            const std::string& url,
            const std::string& body,
            const std::map<std::string, std::string>& headers = {}
        );
        HttpResponse del(
            const std::string& url,
            const std::map<std::string, std::string>& headers = {}
        );

        // RFC-3986 percent-encoding (used when building signed query strings).
        static std::string url_encode(const std::string& value);

    private:
        quill::Logger* logger_;
        CURL* curl_;

        HttpResponse perform(
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
