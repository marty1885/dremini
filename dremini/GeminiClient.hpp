#pragma once

#include <drogon/HttpTypes.h>
#include <drogon/HttpResponse.h>
#include <drogon/drogon.h>
#include <trantor/net/EventLoop.h>
#include <trantor/net/InetAddress.h>
#include <trantor/net/callbacks.h>

#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <functional>
#include <trantor/net/Certificate.h>

#ifdef __cpp_impl_coroutine
#include <drogon/utils/coroutine.h>
#endif

// Forward declaration of heavy trantor classes
namespace trantor
{
class TcpClient;
class Resolver;
}

namespace dremini
{

using ServerTrustDecision = std::function<void(bool accept)>;
// May retain and invoke the decision asynchronously. The request is not sent
// until the callback accepts the certificate.
using ServerTrust = std::function<void(std::string endpoint,
                                       trantor::CertificatePtr certificate,
                                       ServerTrustDecision decide)>;
inline const ServerTrust kNoVerification =
    [](std::string, trantor::CertificatePtr, ServerTrustDecision decide) { decide(true); };

namespace internal
{

class GeminiClient : public std::enable_shared_from_this<GeminiClient>
{
public:
    GeminiClient(std::string url, trantor::EventLoop* loop, double timeout = 0, intmax_t maxBodySize = 0x2000000, double maxTransferDuration = 900,
                 ServerTrust trust = kNoVerification);
    void fire();
    void setCallback(const drogon::HttpReqCallback& callback)
    {
        callback_ = callback;
    }
    void setCallback(drogon::HttpReqCallback&& callback)
    {
        callback_ = std::move(callback);
    }

    /**
     * @brief Set a set of allowed MIMEs for the response. If the response is 20 OK but the returned MIME is not in the set,
     *       the client immediately closes the connection and uses the header as is with empty body. Akin to the HEAD method
     *       when MIME is not in the set
     * 
     * @param mimes the set of allowed MIMEs.
     */
    void setMimes(const std::vector<std::string>& mimes)
    {
        downloadMimes_ = mimes;
    }

protected:
    void sendRequestInLoop();
    void onRecvMessage(const trantor::TcpConnectionPtr &connPtr,
                    trantor::MsgBuffer *msg);
    void haveResult(drogon::ReqResult result, const trantor::MsgBuffer* msg);

    // User specifable values
    trantor::EventLoop* loop_;
    double timeout_;
    drogon::HttpReqCallback callback_;
    std::string url_;
    intmax_t maxBodySize_;
    double maxTransferDuration_;

    // Internal state
    std::shared_ptr<trantor::TcpClient> client_;
    std::string host_;
    uint16_t port_;
    trantor::InetAddress peerAddress_;
    bool headerReceived_ = false;
    int responseStatus_ = 0;
    std::string resoneseMeta_;
    trantor::TimerId timeoutTimerId_;
    std::vector<std::string> downloadMimes_;
    trantor::TimerId transferTimerId_;
    bool callbackCalled_ = false;
    ServerTrust trust_;
    bool trustStarted_ = false;
};

}

void sendRequest(const std::string& url, const drogon::HttpReqCallback& callback, double timeout = 0
    , trantor::EventLoop* loop=drogon::app().getLoop(), intmax_t maxBodySize = -1, const std::vector<std::string>& mimes = {}
    , double maxTransferDuration=0, ServerTrust trust = kNoVerification);

#ifdef __cpp_impl_coroutine
namespace internal
{

struct [[nodiscard]] GeminiRespAwaiter
{
    GeminiRespAwaiter(std::string url, trantor::EventLoop* loop, double timeout = 10, intmax_t maxBodySize = -1, const std::vector<std::string>& mimes = {}
        , double maxTransferDuration=0, ServerTrust trust = kNoVerification)
        : url_(url), loop_(loop), timeout_(timeout), maxBodySize_(maxBodySize), mimes_(mimes), maxTransferDuration_(maxTransferDuration),
          trust_(std::move(trust))
    {
    }

    GeminiRespAwaiter(const GeminiRespAwaiter&) = delete;
    GeminiRespAwaiter& operator=(const GeminiRespAwaiter&) = delete;
    GeminiRespAwaiter(GeminiRespAwaiter&&) = default;
    GeminiRespAwaiter& operator=(GeminiRespAwaiter&&) = default;

    bool await_ready() noexcept
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> handle)
    {
        using namespace drogon;
        state_->handle = handle;
        std::weak_ptr<State> weakState = state_;
        sendRequest(url_, [weakState](ReqResult res, HttpResponsePtr resp){
            auto state = weakState.lock();
            if(!state)
                return;
            if (res == ReqResult::Ok)
                state->response = std::move(resp);
            else
            {
                std::string reason;
                if (res == ReqResult::BadResponse)
                    reason = "BadResponse";
                else if (res == ReqResult::NetworkFailure)
                    reason = "NetworkFailure";
                else if (res == ReqResult::BadServerAddress)
                    reason = "BadServerAddress";
                else if (res == ReqResult::Timeout)
                    reason = "Timeout";
                else if(res == ReqResult::HandshakeError)
                    reason = "HandshakeError";
                else if(res == ReqResult::InvalidCertificate)
                    reason = "InvalidCertificate";
                state->exception = std::make_exception_ptr(std::runtime_error(reason));
            }
            state->handle.resume();
        }, timeout_, loop_, maxBodySize_, mimes_, maxTransferDuration_, std::move(trust_));
    }

    drogon::HttpResponsePtr await_resume()
    {
        if(state_->exception)
            std::rethrow_exception(state_->exception);
        return std::move(*state_->response);
    }

private:
    struct State
    {
        std::optional<drogon::HttpResponsePtr> response;
        std::exception_ptr exception;
        std::coroutine_handle<> handle;
    };

    std::string url_;
    trantor::EventLoop* loop_;
    double timeout_;
    intmax_t maxBodySize_;
    std::vector<std::string> mimes_;
    double maxTransferDuration_;
    ServerTrust trust_;
    std::shared_ptr<State> state_ = std::make_shared<State>();
};
}

inline internal::GeminiRespAwaiter sendRequestCoro(const std::string& url, double timeout = 10
    , trantor::EventLoop* loop=drogon::app().getLoop(), intmax_t maxBodySize = -1, const std::vector<std::string>& mimes = {}
    , double maxTransferDuration = 0, ServerTrust trust = kNoVerification)
{
    return internal::GeminiRespAwaiter(url, loop, timeout, maxBodySize, mimes, maxTransferDuration, std::move(trust));
}

#endif


}
