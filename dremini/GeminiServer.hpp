#pragma once

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

class GeminiServer : public trantor::NonCopyable
{
public:
    GeminiServer(
            trantor::EventLoop* loop,
            const trantor::InetAddress& listenAddr,
            const std::string& key,
            const std::string& cert);
    void start();
    void setIoThreadNum(size_t n);
    void setIoLoopThreadPool(const std::shared_ptr<trantor::EventLoopThreadPool>& pool);

protected:
    void sendResponseBack(const trantor::TcpConnectionPtr& conn, const drogon::HttpResponsePtr& resp);
    void onConnection(const trantor::TcpConnectionPtr &conn);
    void onMessage(const trantor::TcpConnectionPtr &conn, trantor::MsgBuffer *buf);
    trantor::EventLoop* loop_;
    trantor::TcpServer server_;
    std::atomic<int> roundRobbinIdx_{0};
};

}
