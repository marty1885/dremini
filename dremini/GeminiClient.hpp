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
    GeminiClient(std::string url, trantor::EventLoop* loop, double timeout = 0);
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
};

}

void sendRequest(const std::string& url, const drogon::HttpReqCallback& callback, double timeout = 0, trantor::EventLoop* loop=drogon::app().getLoop());

#ifdef __cpp_impl_coroutine
namespace internal
{

struct GeminiRespAwaiter : public drogon::CallbackAwaiter<HttpResponsePtr>
{
    HttpRespAwaiter(std::string url, trantor::EventLoop* loop, double timeout = 10)
        url_(url), loop_(loop)
    {
    }

    void await_suspend(std::coroutine_handle<> handle)
    {
        sendRequest(url_, [](drogon::ReqResult res, HttpResponsePtr resp){
            if (result == ReqResult::Ok)
                setValue(resp);
            else
            {
                std::string reason;
                if (result == ReqResult::BadResponse)
                    reason = "BadResponse";
                else if (result == ReqResult::NetworkFailure)
                    reason = "NetworkFailure";
                else if (result == ReqResult::BadServerAddress)
                    reason = "BadServerAddress";
                else if (result == ReqResult::Timeout)
                    reason = "Timeout";
                setException(
                    std::make_exception_ptr(std::runtime_error(reason)));
            }
            handle.resume();
        }, timeout, loop_);
    }

private:
    std::string url_;
    trantor::EventLoop* loop_;
};

internal::GeminiRespAwaiter sendRequestCoro(const std::string& url, double timeout = 10, trantor::EventLoop* loop=drogon::app().getLoop())
{
    return internal::GeminiRespAwaiter(url, loop);
}

#endif


}
