#pragma once

#include <drogon/HttpTypes.h>
#include <drogon/drogon.h>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <trantor/net/EventLoop.h>
#include <trantor/net/InetAddress.h>
#include <trantor/net/Resolver.h>
#include <trantor/net/TcpClient.h>
#include <trantor/utils/Logger.h>
#include <trantor/utils/MsgBuffer.h>
#include <stdexcept>
#include <algorithm>

#ifdef __cpp_impl_coroutine
#include <drogon/utils/coroutine.h>
#endif

namespace dremini
{

namespace internal
{

class GeminiClient : public std::enable_shared_from_this<GeminiClient>
{
public:
    GeminiClient(std::string url, trantor::EventLoop* loop, double timeout = 0, intmax_t maxBodySize = 0x2000000, double maxTransferDuration = 900);
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
    short port_;
    trantor::InetAddress peerAddress_;
    bool headerReceived_ = false;
    int responseStatus_ = 0;
    std::string resoneseMeta_;
    std::shared_ptr<trantor::Resolver> resolver_;
    trantor::TimerId timeoutTimerId_;
    std::vector<std::string> downloadMimes_;
    trantor::TimerId transferTimerId_;
    bool callbackCalled_ = false;
};

}

void sendRequest(const std::string& url, const drogon::HttpReqCallback& callback, double timeout = 0
    , trantor::EventLoop* loop=drogon::app().getLoop(), intmax_t maxBodySize = -1, const std::vector<std::string>& mimes = {}
    , double maxTransferDuration=0);

#ifdef __cpp_impl_coroutine
namespace internal
{

struct [[nodiscard]] GeminiRespAwaiter : public drogon::CallbackAwaiter<drogon::HttpResponsePtr>
{
    GeminiRespAwaiter(std::string url, trantor::EventLoop* loop, double timeout = 10, intmax_t maxBodySize = -1, const std::vector<std::string>& mimes = {}
        , double maxTransferDuration=0)
        : url_(url), loop_(loop), timeout_(timeout), maxBodySize_(maxBodySize), mimes_(mimes), maxTransferDuration_(maxTransferDuration)
    {
    }

    void await_suspend(std::coroutine_handle<> handle)
    {
        using namespace drogon;
        sendRequest(url_, [this, handle](ReqResult res, HttpResponsePtr resp){
            if (res == ReqResult::Ok)
                setValue(resp);
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
                setException(
                    std::make_exception_ptr(std::runtime_error(reason)));
            }
            handle.resume();
        }, timeout_, loop_, maxBodySize_, mimes_, maxTransferDuration_);
    }

private:
    std::string url_;
    trantor::EventLoop* loop_;
    double timeout_;
    intmax_t maxBodySize_;
    std::vector<std::string> mimes_;
    double maxTransferDuration_;
};
}

inline internal::GeminiRespAwaiter sendRequestCoro(const std::string& url, double timeout = 10
    , trantor::EventLoop* loop=drogon::app().getLoop(), intmax_t maxBodySize = -1, const std::vector<std::string>& mimes = {}
    , double maxTransferDuration = 0)
{
    return internal::GeminiRespAwaiter(url, loop, timeout, maxBodySize, mimes, maxTransferDuration);
}

#endif


}
