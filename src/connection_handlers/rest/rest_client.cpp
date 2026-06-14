#include <cctype>
#include <quill/LogMacros.h>
#include "rest_client.hpp"

namespace Omni::Connection {

RestClient::RestClient(quill::Logger* logger)
:   logger_(logger),
    curl_(nullptr)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_ = curl_easy_init();
    if (!curl_) {
        LOG_WARNING(logger_, "Failed to initialize curl");
    }
}


RestClient::~RestClient() {
    if (curl_) {
        curl_easy_cleanup(curl_);
    }
    curl_global_cleanup();
}


size_t RestClient::write_callback(
    void* contents, size_t size, size_t nmemb, std::string* data
) {
    size_t total_size = size * nmemb;
    data->append(static_cast<const char*>(contents), total_size);
    return total_size;
}


size_t RestClient::header_callback(
    void* contents, size_t size, size_t nmemb,
    std::map<std::string, std::string>* headers
) {
    size_t total_size = size * nmemb;
    std::string header_line(static_cast<const char*>(contents), total_size);

    size_t colon_pos = header_line.find(':');
    if (colon_pos != std::string::npos) {
        std::string key = header_line.substr(0, colon_pos);
        std::string value = header_line.substr(colon_pos + 1);

        key.erase(0, key.find_first_not_of(" \t\r\n"));
        key.erase(key.find_last_not_of(" \t\r\n") + 1);
        value.erase(0, value.find_first_not_of(" \t\r\n"));
        value.erase(value.find_last_not_of(" \t\r\n") + 1);

        if (!key.empty() && !value.empty()) {
            (*headers)[key] = value;
        }
    }

    return total_size;
}


std::string RestClient::url_encode(const std::string& value) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}


RestResponse RestClient::perform(
    const std::string& method,
    const std::string& url,
    const std::string* body,
    const std::map<std::string, std::string>& headers
) {
    if (!curl_) {
        return RestResponse{false, "CURL not initialized"};
    }

    std::string response_body;
    std::map<std::string, std::string> response_headers;
    long status_code = 0;

    try {
        curl_easy_reset(curl_);
        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());

        if (method == "POST") {
            curl_easy_setopt(curl_, CURLOPT_POST, 1L);
        } else if (method != "GET") {
            curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, method.c_str());
        }

        if (body) {
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body->c_str());
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, body->length());
        }

        struct curl_slist* header_list = nullptr;
        for (const auto& [key, value] : headers) {
            std::string header_str = key + ": " + value;
            header_list = curl_slist_append(header_list, header_str.c_str());
        }
        if (header_list) {
            curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, header_list);
        }

        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);

        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION, header_callback);
        curl_easy_setopt(curl_, CURLOPT_HEADERDATA, &response_headers);
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L);

        CURLcode res = curl_easy_perform(curl_);

        if (header_list) {
            curl_slist_free_all(header_list);
        }

        if (res != CURLE_OK) {
            auto curl_error_msg = std::string(curl_easy_strerror(res));
            return RestResponse{false, "CURL request failed: " + curl_error_msg};
        }

        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &status_code);

        return RestResponse{
            true, "",
            static_cast<int>(status_code),
            response_body,
            response_headers
        };
    } catch (const std::exception& e) {
        return RestResponse{false, "HTTP request failed: " + std::string(e.what())};
    }
}


RestResponse RestClient::get(
    const std::string& url, const std::map<std::string, std::string>& headers
) {
    return perform("GET", url, nullptr, headers);
}


RestResponse RestClient::post(
    const std::string& url, const std::string& body,
    const std::map<std::string, std::string>& headers
) {
    auto hdrs = headers;
    if (hdrs.find("content-type") == hdrs.end()) {
        hdrs["content-type"] = "application/json";
    }
    return perform("POST", url, &body, hdrs);
}


RestResponse RestClient::put(
    const std::string& url, const std::string& body,
    const std::map<std::string, std::string>& headers
) {
    return perform("PUT", url, &body, headers);
}


RestResponse RestClient::del(
    const std::string& url, const std::map<std::string, std::string>& headers
) {
    return perform("DELETE", url, nullptr, headers);
}

} // namespace Omni::Connection
