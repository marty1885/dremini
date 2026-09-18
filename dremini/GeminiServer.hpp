#pragma once

#include <dremini/Titan.hpp>
#include <drogon/HttpRequest.h>
#include <drogon/utils/FunctionTraits.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <trantor/net/EventLoop.h>
#include <trantor/net/EventLoopThreadPool.h>
#include <trantor/net/InetAddress.h>
#include <trantor/net/TcpServer.h>
#include <trantor/utils/NonCopyable.h>

namespace dremini
{
class ConnectionLimiter final
{
  public:
    explicit ConnectionLimiter(std::size_t maximum);

    [[nodiscard]] bool tryAcquire() noexcept;
    void release() noexcept;

  private:
    const std::size_t maximum_;
    std::atomic_size_t active_{0};
};

class ConnectionPermit final
{
  public:
    explicit ConnectionPermit(std::shared_ptr<ConnectionLimiter> limiter) noexcept;
    ~ConnectionPermit();

    ConnectionPermit(const ConnectionPermit &) = delete;
    ConnectionPermit &operator=(const ConnectionPermit &) = delete;

  private:
    std::shared_ptr<ConnectionLimiter> limiter_;
};

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
            TitanOptions titanOptions = {},
            std::shared_ptr<ConnectionLimiter> connectionLimiter = {});
    void start();
    void setIoThreadNum(size_t n);
    void setIoLoopThreadPool(const std::shared_ptr<trantor::EventLoopThreadPool>& pool);

protected:
    void sendResponseBack(const trantor::TcpConnectionPtr& conn, const drogon::HttpResponsePtr& resp);
    void sendClaimedResponseBack(const trantor::TcpConnectionPtr& conn,
                                 const drogon::HttpResponsePtr& resp);
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
    std::shared_ptr<ConnectionLimiter> connectionLimiter_;
    std::atomic_uint32_t roundRobbinIdx_{0};
};

}
