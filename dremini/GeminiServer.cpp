#include <dremini/GeminiServer.hpp>
#include <drogon/HttpAppFramework.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <trantor/net/TLSPolicy.h>

using namespace drogon;
using namespace dremini;
using namespace trantor;

namespace
{
constexpr std::size_t kMaxRequestLineBytes = 1024;
constexpr std::size_t kMaxResponseHeaderBytes = 1024;

struct ParsedRequestLine
{
    std::string_view scheme;
    std::string_view authority;
    std::string_view path;
    std::string_view query;
};

bool isLowercaseAsciiLetter(char value) noexcept
{
    return value >= 'a' && value <= 'z';
}

bool isDecimalDigit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

bool containsUriControl(std::string_view value) noexcept
{
    for (const auto character : value)
    {
        const auto byte = static_cast<unsigned char>(character);
        if (byte <= 0x20 || byte == 0x7f) return true;
    }
    return false;
}

std::string makeResponseHeader(int status, std::string meta)
{
    meta.erase(std::remove_if(meta.begin(), meta.end(), [](char character) {
        return character == '\r' || character == '\n';
    }), meta.end());
    // Gemini limits the complete status line, including its CRLF terminator,
    // to 1024 bytes. Status is always two digits followed by one space.
    meta.resize(std::min(meta.size(), kMaxResponseHeaderBytes - 5));
    return std::to_string(status) + " " + meta + "\r\n";
}

// Gemini request lines carry one absolute URI. Keep this parser deliberately
// linear and bounded by kMaxRequestLineBytes: std::regex is unnecessary on a
// network boundary and has implementation-dependent resource behaviour.
std::optional<ParsedRequestLine> parseRequestLine(std::string_view value) noexcept
{
    if (containsUriControl(value)) return std::nullopt;
    const auto schemeEnd = value.find("://");
    if (schemeEnd == std::string_view::npos || schemeEnd == 0) return std::nullopt;
    const auto scheme = value.substr(0, schemeEnd);
    for (const auto character : scheme)
        if (!isLowercaseAsciiLetter(character)) return std::nullopt;

    std::size_t position = schemeEnd + 3;
    if (position == value.size()) return std::nullopt;
    const auto authorityStart = position;
    if (value[position] == '[')
    {
        const auto close = value.find(']', position + 1);
        if (close == std::string_view::npos || close == position + 1) return std::nullopt;
        position = close + 1;
    }
    else
    {
        while (position < value.size() && value[position] != ':' && value[position] != '/' && value[position] != '?')
            ++position;
        if (position == authorityStart) return std::nullopt;
    }

    if (position < value.size() && value[position] == ':')
    {
        const auto portStart = ++position;
        while (position < value.size() && isDecimalDigit(value[position])) ++position;
        if (position == portStart) return std::nullopt;
    }

    const auto authority = value.substr(authorityStart, position - authorityStart);
    std::string_view path;
    if (position < value.size() && value[position] == '/')
    {
        const auto pathStart = position++;
        while (position < value.size() && value[position] != '?') ++position;
        path = value.substr(pathStart, position - pathStart);
    }

    std::string_view query;
    if (position < value.size())
    {
        if (value[position] != '?') return std::nullopt;
        query = value.substr(position + 1);
    }
    return ParsedRequestLine{scheme, authority, path, query};
}

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

    const std::string url(buf->peek(), std::distance(buf->peek(), crlf));
    buf->retrieveUntil(crlf);
    buf->retrieve(2);

    const auto parsed = parseRequestLine(url);
    if (!parsed)
    {
        LOG_TRACE << "Invalid request";
        rejectRequest(conn, 59, "Invalid request");
        return;
    }

    const auto scheme = parsed->scheme;
    if (scheme != "gemini" && scheme != "titan")
    {
        LOG_TRACE << "Unsupported request scheme";
        rejectRequest(conn, 59, "Invalid request");
        return;
    }
    const std::string authority{parsed->authority};
    std::string path{parsed->path};
    const std::string query{parsed->query};
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
    const auto threadNum = app().getThreadNum();
    if (threadNum == 0)
    {
        LOG_ERROR << "GeminiServer requires at least one application I/O thread";
        rejectRequest(conn, 40, "Temporary Failure");
        return;
    }
    const auto idx = roundRobbinIdx_.fetch_add(1, std::memory_order_relaxed) % threadNum;
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
    else if(httpStatus/100 == 2) // Success -> Success
        status = 20;
    else if(httpStatus/100 == 4) // Client Error -> Permanent Failure
        status = 50;
    else if(httpStatus/100 == 5) // Server Error -> Temporary Failure
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
        respHeader = makeResponseHeader(status, std::move(meta));
    }
    else if(status/10 == 2)
    {
        auto ct = resp->contentTypeString();
        if(ct != "")
            respHeader = makeResponseHeader(status, std::move(ct));
        else
            respHeader = makeResponseHeader(status, "application/octet-stream");
    }
    else if(status/10 == 3)
    {
        std::string meta = resp->getHeader("location");
        if(meta.empty())
            meta = resp->getHeader("meta");
        respHeader = makeResponseHeader(status, std::move(meta));
    }
    else if(status == 44)
    {
        std::string meta = resp->getHeader("Retry-After");
        if(meta.empty())
            meta = resp->getHeader("meta");
        if(meta.empty())
            meta = "30"; // XXX: Default 30s retry
        respHeader = makeResponseHeader(status, std::move(meta));
    }
    else if(status/10 == 4)
    {
        std::string meta = resp->getHeader("meta");
        if(meta.empty())
            meta = "Temporary Failure";
        respHeader = makeResponseHeader(status, std::move(meta));
    }
    else if(status/10 == 5)
    {
        std::string meta = resp->getHeader("meta");
        if(meta.empty())
            meta = "Permanent Failure";
        respHeader = makeResponseHeader(status, std::move(meta));
    }
    else
    {
        respHeader = makeResponseHeader(status, resp->getHeader("meta"));
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
