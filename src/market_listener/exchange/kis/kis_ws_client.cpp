#include <quill/LogMacros.h>
#include <nlohmann/json.hpp>

#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/aes.h>

#include "kis_ws_client.hpp"


namespace Omni::Listener::KIS {

namespace CM = Omni::KIS::CodeManager;


KisWebsocketClient::KisWebsocketClient(
    const std::string& uri, quill::Logger* logger,
    moodycamel::BlockingConcurrentQueue<WsResponse>* queue,
    std::shared_ptr<CM::ICodeManager> code_manager
)
:   Base(uri, logger),
    queue_(queue),
    code_manager_(code_manager)
{
    set_handlers();
    init_connection(uri);
}


void KisWebsocketClient::notify_websocket_status() {
    queue_->enqueue(WsStatusUpdate{
        .connecting = connecting_.load(),
        .opened = opened_.load()
    });
}


std::string KisWebsocketClient::base64_decode(const std::string& cipher_b64) {
    BIO *bio, *b64;
    size_t len = cipher_b64.size();
    std::vector<char> out(len);

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new_mem_buf(cipher_b64.data(), static_cast<int>(len));
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    int decoded_len = BIO_read(bio, out.data(), static_cast<int>(len));
    BIO_free_all(bio);

    if (decoded_len <= 0) {
        LOG_WARNING(logger_, "Base64 decode failed");
        return "";
    }
    return std::string(out.data(), decoded_len);
}


std::string KisWebsocketClient::aes_cbc_decrypt(const std::string& cipher_b64) {
    std::string cipher_bin = base64_decode(cipher_b64);

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        LOG_WARNING(logger_, "EVP_CIPHER_CTX_new failed");
        return "";
    }

    if (
        EVP_DecryptInit_ex(
            ctx, EVP_aes_256_cbc(), nullptr,
            reinterpret_cast<const unsigned char*>(ws_key_.data()),
            reinterpret_cast<const unsigned char*>(ws_iv_.data())
        ) != 1
    ) {
        LOG_WARNING(logger_, "EVP_DecryptInit_ex failed");
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    std::vector<unsigned char> out(cipher_bin.size() + AES_BLOCK_SIZE);
    int outlen1 = 0, outlen2 = 0;

    if (
        EVP_DecryptUpdate(
            ctx, out.data(), &outlen1,
            reinterpret_cast<const unsigned char*>(cipher_bin.data()),
            static_cast<int>(cipher_bin.size())
        ) != 1
    ) {
        LOG_WARNING(logger_, "EVP_DecryptUpdate failed");
        EVP_CIPHER_CTX_free(ctx);
        return "";
    }

    if (EVP_DecryptFinal_ex(ctx, out.data() + outlen1, &outlen2) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        LOG_WARNING(logger_, "EVP_DecryptFinal_ex failed (bad key/iv/padding)");
        return "";
    }

    EVP_CIPHER_CTX_free(ctx);
    return std::string(reinterpret_cast<char*>(out.data()), outlen1 + outlen2);
}


void KisWebsocketClient::split_msg(
    const std::string_view& msg, char sep, std::vector<std::string>& msg_segments
) {
    size_t start = 0, pos = 0;
    while ((pos = msg.find(sep, start)) != std::string_view::npos) {
        msg_segments.emplace_back(msg.substr(start, pos-start));
        start = pos + 1;
    }
    msg_segments.emplace_back(msg.substr(start));
}


void KisWebsocketClient::on_stream_message(Base::message_ptr msg) {
    try {
        std::string_view msg_str = msg->get_payload();
        if (msg_str.empty()) return;

        if ((msg_str[0] == '0') || (msg_str[0] == '1')) {
            std::vector<std::string> msg_segments;
            split_msg(msg_str, '|', msg_segments);
            if (msg_segments.size() < 4) {
                LOG_WARNING(logger_, "Msg with invalid segment size: {}", msg_str);
                return;
            }

            std::vector<std::string> data_segments;
            auto tr_id_type = code_manager_->get_tr_id_type(msg_segments[1]);
            if (tr_id_type == CM::TrIdType::ExecutionTrId) {
                split_msg(aes_cbc_decrypt(msg_segments[3]), '^', data_segments);
            } else {
                split_msg(msg_segments[3], '^', data_segments);
            }

            queue_->enqueue(WsMarketDataResponse{
                .tr_id_type = tr_id_type,
                .data_num = std::stoul(msg_segments[2]),
                .market_data = data_segments
            });
        } else {
            if (msg_str.find("header") != std::string_view::npos) {
                auto msg_json = nlohmann::json::parse(msg_str);
                auto msg_body = msg_json["body"];
                if (msg_body["msg1"] == "SUBSCRIBE SUCCESS") {
                    LOG_INFO(logger_, "Received subscribe success response: {}", msg_str);
                    auto msg_output = msg_body["output"];
                    ws_iv_ = msg_output["iv"];
                    ws_key_ = msg_output["key"];
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_WARNING(logger_, "Exception within on_stream_message: {}", e.what());
    }
}

} // namespace Omni::Listener::KIS
