#include <dremini/GeminiServer.hpp>
#include <drogon/HttpAppFramework.h>
#include <memory>
#include <regex>

using namespace drogon;
using namespace dremini;
using namespace trantor;

GeminiServer::GeminiServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& key, const std::string& cert)
    : loop_(loop), server_(loop, listenAddr, "GeminiServer")
{
    if(app().supportSSL() == false)
    {
        LOG_FATAL << "Dremini (Drogon Gemini Server) requires SSL support";
    }
    LOG_DEBUG << "Creating srver on address " << listenAddr.toIpPort();

    server_.enableSSL(key, cert, false);
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
    if(buf->readableBytes() > 1024) // Gemini spec: max 1024 byte request
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode((HttpStatusCode)500);
        resp->addHeader("meta", "Invalid");
        sendResponseBack(conn, resp);
        return;
    } 
    const char* crlf = buf->findCRLF();
    if(crlf == nullptr)
        return;

    std::string url(buf->peek(), std::distance(buf->peek(), crlf));

    static const std::regex re(R"(([a-z]+):\/\/([^\/:]+)(?:\:([0-9]+))?(\/$|$|\/[^?]*)(?:\?(.*))?)");
    std::smatch match;
    if(!std::regex_match(url, match, re))
    {
        LOG_TRACE << "Invalid request";
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode((HttpStatusCode)500);
        resp->addHeader("meta", "Invalid");
        sendResponseBack(conn, resp);
        return;
    }

    std::string path = match[4];
    std::string query = match[5];
    HttpRequestPtr req = HttpRequest::newHttpRequest();
    if(path.empty())
        path = "/";
    req->setMethod(Get);
    req->setPath(path);
    req->addHeader("protocol", "gemini");
    if(!query.empty())
        req->setParameter("query", query);
    LOG_TRACE << "Gemini request recived: " << url;

    int idx = roundRobbinIdx_++;
    if(idx > 0x7ffff) // random large number
    {
        // XXX: Properbally data race. But it happens rare enough
        roundRobbinIdx_ = 0;
    }
    idx = idx % app().getThreadNum();
    // Drogon only accepts request from it's own event loops
    app().getIOLoop(idx)->runInLoop([req, conn, this](){
        app().forward(req, [req, conn, this](const HttpResponsePtr& resp){
            sendResponseBack(conn, resp);
        });
    });
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
    if(httpStatus < 100) // HTTP status starts from 400. These are Gemini status
        status = httpStatus;
    else if(httpStatus == 400) // 400 Bad Request -> 59(Bad request)
        status = 59;
    else if(httpStatus == 404) // 404 (Not Found) -> 51(Not Found)
        status = 51;
    else if(httpStatus == 502 || httpStatus == 504) // 502/504 (Bad gateway/Gateway Timeout) -> 43 (Proxy Error)
        status = 43;
    else if(httpStatus == 503) // 503(Service Unavailable) -> 44 (Slow Down)
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
            meta = "Perament Failure";
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
