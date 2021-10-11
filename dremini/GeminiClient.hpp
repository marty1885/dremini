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
    GeminiClient(std::string url, trantor::EventLoop* loop, double timeout = 0, intmax_t maxBodySize = -1);
    void fire();
    void setCallback(const drogon::HttpReqCallback& callback)
    {
        callback_ = callback;
    }

protected:
    void sendRequestInLoop();
    void onRecvMessage(const trantor::TcpConnectionPtr &connPtr,
                    trantor::MsgBuffer *msg);

    bool gotHeader_ = false;
    double timeout_;
    drogon::HttpReqCallback callback_;
    std::string meta_;
    int status_;
    std::string url_;
    std::string host_;
    short port_;
    bool needNameResolve_;
    trantor::EventLoop* loop_;
    std::shared_ptr<trantor::TcpClient> client_;
    std::shared_ptr<trantor::Resolver> resolver_;
    trantor::InetAddress address_;
    trantor::TimerId timeoutTimerId_;
    intmax_t maxBodySize_;
};

}

void sendRequest(const std::string& url, const drogon::HttpReqCallback& callback, double timeout = 0
    , trantor::EventLoop* loop=drogon::app().getLoop(), intmax_t maxBodySize = -1);

#ifdef __cpp_impl_coroutine
namespace internal
{

struct GeminiRespAwaiter : public drogon::CallbackAwaiter<drogon::HttpResponsePtr>
{
    GeminiRespAwaiter(std::string url, trantor::EventLoop* loop, double timeout = 10, intmax_t maxBodySize = -1)
        : url_(url), loop_(loop), timeout_(timeout), maxBodySize_(maxBodySize)
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
        }, timeout_, loop_, maxBodySize_);
    }

private:
    std::string url_;
    double timeout_;
    intmax_t maxBodySize_;
    trantor::EventLoop* loop_;
};
}

inline internal::GeminiRespAwaiter sendRequestCoro(const std::string& url, double timeout = 10
    , trantor::EventLoop* loop=drogon::app().getLoop(), intmax_t maxBodySize = -1)
{
    return internal::GeminiRespAwaiter(url, loop, timeout, maxBodySize);
}

#endif


}
