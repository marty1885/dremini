#pragma once

#include <dremini/Titan.hpp>
#include <drogon/HttpRequest.h>
#include <drogon/utils/FunctionTraits.h>
#include <memory>
#include <trantor/net/EventLoop.h>
#include <trantor/net/EventLoopThreadPool.h>
#include <trantor/net/InetAddress.h>
#include <trantor/net/TcpServer.h>
#include <trantor/utils/NonCopyable.h>

namespace dremini
{
struct TitanOptions
{
    bool enabled = false;
    std::size_t maxUploadBytes = 4096;
};

class GeminiServer : public trantor::NonCopyable
{
public:
    GeminiServer(
            trantor::EventLoop* loop,
            const trantor::InetAddress& listenAddr,
            const std::string& key,
            const std::string& cert,
            TitanOptions titanOptions = {});
    void start();
    void setIoThreadNum(size_t n);
    void setIoLoopThreadPool(const std::shared_ptr<trantor::EventLoopThreadPool>& pool);

protected:
    void sendResponseBack(const trantor::TcpConnectionPtr& conn, const drogon::HttpResponsePtr& resp);
    void onConnection(const trantor::TcpConnectionPtr &conn);
    void onMessage(const trantor::TcpConnectionPtr &conn, trantor::MsgBuffer *buf);
    void dispatchRequest(const trantor::TcpConnectionPtr &conn,
                         drogon::HttpRequestPtr request);
    void rejectRequest(const trantor::TcpConnectionPtr &conn,
                       int status,
                       std::string meta);
    trantor::EventLoop* loop_;
    trantor::TcpServer server_;
    TitanOptions titanOptions_;
    std::atomic<int> roundRobbinIdx_{0};
};

}
