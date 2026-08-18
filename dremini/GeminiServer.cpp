#include <dremini/GeminiServer.hpp>
#include <drogon/HttpAppFramework.h>

#include <cstddef>
#include <memory>
#include <regex>
#include <string>
#include <trantor/net/TLSPolicy.h>

using namespace drogon;
using namespace dremini;
using namespace trantor;

namespace
{
constexpr std::size_t kMaxRequestLineBytes = 1024;

struct ConnectionState
{
    enum class Phase { CollectingTitanBody, Dispatched };

    explicit ConnectionState(Phase phase) : phase(phase) {}

    Phase phase;
    HttpRequestPtr request;
    std::size_t expectedBodyBytes = 0;
    std::string body;
};

}  // namespace

GeminiServer::GeminiServer(EventLoop* loop,
                           const InetAddress& listenAddr,
                           const std::string& key,
                           const std::string& cert,
                           TitanOptions titanOptions)
    : loop_(loop), server_(loop, listenAddr, "GeminiServer"), titanOptions_(titanOptions)
{
    if(app().supportSSL() == false)
    {
        LOG_FATAL << "Dremini (Drogon Gemini Server) requires SSL support";
    }
    LOG_DEBUG << "Creating srver on address " << listenAddr.toIpPort();

    auto tlsPolicy = trantor::TLSPolicy::defaultServerPolicy(cert, key);
    // Gemini permits both anonymous clients and clients authenticating with a
    // self-signed certificate. Request a certificate without requiring one;
    // application routes decide whether a certificate is necessary.
    tlsPolicy->setPeerCertificateRequest(false)
        .setCertificateVerification(false);
    server_.enableSSL(std::move(tlsPolicy));
    server_.setConnectionCallback([this](const TcpConnectionPtr& conn) {onConnection(conn);});
    server_.setRecvMessageCallback([this](const TcpConnectionPtr& conn, MsgBuffer* buf){onMessage(conn, buf);});
}

void GeminiServer::onConnection(const TcpConnectionPtr& conn)
{}

void GeminiServer::start()
{
    server_.start();
}

void GeminiServer::setIoThreadNum(size_t n)
{
    server_.setIoLoopNum(n);
}

void GeminiServer::onMessage(const TcpConnectionPtr &conn, MsgBuffer *buf)
{
    if (const auto state = conn->getContext<ConnectionState>())
    {
        if (state->phase == ConnectionState::Phase::Dispatched)
        {
            LOG_WARN << "Extra message received for Gemini/Titan connection";
            return;
        }

        const auto remaining = state->expectedBodyBytes - state->body.size();
        if (buf->readableBytes() > remaining)
        {
            rejectRequest(conn, 59, "Titan request sent more bytes than declared");
            return;
        }
        state->body.append(buf->peek(), buf->readableBytes());
        buf->retrieveAll();
        if (state->body.size() != state->expectedBodyBytes) return;

        state->request->setBody(std::move(state->body));
        dispatchRequest(conn, std::move(state->request));
        return;
    }

    const char* crlf = buf->findCRLF();
    if (crlf == nullptr)
    {
        if (buf->readableBytes() > kMaxRequestLineBytes)
            rejectRequest(conn, 59, "Request line is too long");
        return;
    }
    const auto requestLineBytes = static_cast<std::size_t>(crlf - buf->peek()) + 2;
    if (requestLineBytes > kMaxRequestLineBytes)
    {
        rejectRequest(conn, 59, "Request line is too long");
        return;
    }

    std::string url(buf->peek(), std::distance(buf->peek(), crlf));
    buf->retrieveUntil(crlf);
    buf->retrieve(2);

    static const std::regex re(R"(([a-z]+):\/\/([^\/:]+)(?:\:([0-9]+))?(\/$|$|\/[^?]*)(?:\?(.*))?)");
    std::smatch match;
    if(!std::regex_match(url, match, re))
    {
        LOG_TRACE << "Invalid request";
        rejectRequest(conn, 59, "Invalid request");
        return;
    }

    const std::string scheme = match[1];
    std::string authority = match[2];
    if (match[3].matched)
        authority += ":" + match[3].str();
    std::string path = match[4];
    std::string query = match[5];
    HttpRequestPtr req = HttpRequest::newHttpRequest();
    if(path.empty())
        path = "/";
    req->setMethod(Get);
    req->setPath(path);
    req->setPeerCertificate(conn->peerCertificate());
    req->addHeader("protocol", scheme == "titan" ? "titan" : "gemini");
    req->getAttributes()->insert(kRequestAuthorityAttribute, std::move(authority));
    if(!query.empty())
        req->setParameter("query", query);

    LOG_DEBUG << "Gemini/Titan request: scheme=" << scheme << " path=" << path
              << " query_present=" << !query.empty();

    if (scheme == "titan")
    {
        if (!titanOptions_.enabled)
        {
            rejectRequest(conn, 50, "Titan uploads are not enabled");
            return;
        }
        const auto titan = parseTitanRequest(path, titanOptions_.maxUploadBytes);
        if (!titan)
        {
            rejectRequest(conn, 59, std::string{titanParseErrorMessage(titan.error)});
            return;
        }
        if (titan.request->operation == TitanOperation::Edit)
        {
            LOG_DEBUG << "Titan edit request: resource=" << titan.request->resourcePath;
            if (buf->readableBytes() != 0)
            {
                rejectRequest(conn, 59, "Titan edit request must not include a body");
                return;
            }
            // Keep the original HTTP path, including ';edit'. The plugin's
            // pre-routing interceptor turns it into the resource path before
            // Drogon routes the request.
            req->addHeader("titan-operation", "edit");
            req->getAttributes()->insert(kTitanEditRequestAttribute, true);
            dispatchRequest(conn, std::move(req));
            return;
        }

        req->setMethod(Post);
        req->setPath(titan.request->resourcePath);
        LOG_DEBUG << "Titan upload request: resource=" << titan.request->resourcePath
                  << " size=" << titan.request->size << " mime=" << titan.request->mimeType;
        req->setContentTypeString(titan.request->mimeType);
        req->addHeader("content-type", titan.request->mimeType);
        req->addHeader("titan-size", std::to_string(titan.request->size));
        if (titan.request->token)
            req->addHeader("titan-token", *titan.request->token);

        auto state = std::make_shared<ConnectionState>(ConnectionState::Phase::CollectingTitanBody);
        state->request = std::move(req);
        state->expectedBodyBytes = titan.request->size;
        // The declared size is peer-controlled. Do not reserve it: memory is
        // acquired only for bytes actually received, under the server-owned
        // maximum enforced by parseTitanRequest().
        conn->setContext(std::move(state));
        onMessage(conn, buf);
        return;
    }

    // Gemini has no body and supports exactly one request per connection. If
    // bytes have already arrived after the request line, reject the request
    // instead of silently dropping them. Data that arrives after dispatch is
    // deliberately not treated as part of this request: async receive timing
    // must not change the request state that has already been handed off.
    if (buf->readableBytes() != 0)
    {
        rejectRequest(conn, 59, "Gemini request must not include trailing data");
        return;
    }
    LOG_TRACE << "Gemini request received";
    dispatchRequest(conn, std::move(req));
}

void GeminiServer::dispatchRequest(const TcpConnectionPtr &conn, HttpRequestPtr req)
{
    conn->setContext(std::make_shared<ConnectionState>(ConnectionState::Phase::Dispatched));
    int idx = roundRobbinIdx_.fetch_add(1, std::memory_order_relaxed);
    if(idx > 0x7ffff) // random large number
    {
        // XXX: Properbally data race. But it happens rare enough
        roundRobbinIdx_.store(0, std::memory_order_relaxed);
    }
    idx = idx % app().getThreadNum();
    // Drogon only accepts request from it's own event loops
    app().getIOLoop(idx)->runInLoop([req=std::move(req), conn=std::move(conn), this](){
        app().forward(req, [req=std::move(req), conn=std::move(conn), this](const HttpResponsePtr& resp){
            sendResponseBack(conn, resp);
        });
    });
}

void GeminiServer::rejectRequest(const TcpConnectionPtr &conn, int status, std::string meta)
{
    conn->setContext(std::make_shared<ConnectionState>(ConnectionState::Phase::Dispatched));
    auto response = HttpResponse::newHttpResponse();
    response->setStatusCode(static_cast<HttpStatusCode>(status));
    response->addHeader("meta", std::move(meta));
    sendResponseBack(conn, response);
}


void GeminiServer::setIoLoopThreadPool(const std::shared_ptr<EventLoopThreadPool>& pool)
{
    server_.setIoLoopThreadPool(pool);
}

void GeminiServer::sendResponseBack(const TcpConnectionPtr& conn, const HttpResponsePtr& resp)
{
    LOG_TRACE << "Sending response back";
    const int httpStatus = resp->statusCode();
    int status;
    if(httpStatus < 100) // HTTP status starts from 100. These are Gemini status
        status = httpStatus;
    else if(httpStatus == 400) // 400 Bad Request -> 59(Bad request)
        status = 59;
    else if(httpStatus == 404) // 404 (Not Found) -> 51(Not Found)
        status = 51;
    else if(httpStatus == 502 || httpStatus == 504) // 502/504 (Bad gateway/Gateway Timeout) -> 43 (Proxy Error)
        status = 43;
    else if(httpStatus == 429) // 429 (Too Many Requests) -> 44 (Slow Down)
        status = 44;
    else if(httpStatus%100 == 2) // Success -> Success
        status = 20;
    else if(httpStatus%100 == 4) // Client Error -> Permanent Failure
        status = 50;
    else if(httpStatus%100 == 5) // Server Error -> Temporary Failure
        status = 40;
    else
        status = httpStatus/100*10;
    std::string respHeader;

    assert(status < 100 && status >= 10);

    if(status/10 == 1)
    {
        std::string meta = resp->getHeader("meta");
        if(meta.empty())
            meta = "Input";
        respHeader = std::to_string(status) + " " + meta + "\r\n";
    }
    else if(status/10 == 2)
    {
        auto ct = resp->contentTypeString();
        if(ct != "")
            respHeader = std::to_string(status) + " " + ct + "\r\n";
        else
            respHeader = std::to_string(status) + " application/octet-stream\r\n";
    }
    else if(status/10 == 3)
    {
        if(!resp->getHeader("location").empty())
            respHeader = std::to_string(status) + " " + resp->getHeader("location") + "\r\n";
        else
            respHeader = std::to_string(status) + " " + resp->getHeader("meta") + "\r\n";
    }
    else if(status == 44)
    {
        std::string meta = resp->getHeader("Retry-After");
        if(meta.empty())
            meta = resp->getHeader("meta");
        if(meta.empty())
            meta = "30"; // XXX: Default 30s retry
        respHeader = std::to_string(status) + " " + meta + "\r\n";
    }
    else if(status/10 == 4)
    {
        std::string meta = resp->getHeader("meta");
        if(meta.empty())
            meta = "Temporary Failure";
        respHeader = std::to_string(status) + " " + meta + "\r\n";
    }
    else if(status/10 == 5)
    {
        std::string meta = resp->getHeader("meta");
        if(meta.empty())
            meta = "Permanent Failure";
        respHeader = std::to_string(status) + " " + meta + "\r\n";
    }
    else
    {
        respHeader = std::to_string(status) + " " + resp->getHeader("meta") + "\r\n";
    }
    conn->send(respHeader);

    if(status/10 == 2)
    {
        const std::string &sendfileName = resp->sendfileName();
        if (!sendfileName.empty())
        {
            const auto &range = resp->sendfileRange();
            conn->sendFile(sendfileName.c_str(), range.first, range.second);
        }
        else if(resp->body().size() != 0)
        {
            conn->send(std::string(resp->body()));
        }
    }
    conn->shutdown();
}
