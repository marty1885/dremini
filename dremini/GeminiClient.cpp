#include <dremini/GeminiClient.hpp>
#include <regex>
#include <string>

using namespace drogon;

static bool isIPString(const std::string& str)
{
    bool isIpV6 = str.find(":") != std::string::npos;
    return trantor::InetAddress(str, 0, isIpV6).isUnspecified();
}

ContentType parseContentType(const string_view &contentType)
{
    static const std::unordered_map<string_view, ContentType> map_{
        {"text/html", CT_TEXT_HTML},
        {"application/x-www-form-urlencoded", CT_APPLICATION_X_FORM},
        {"application/xml", CT_APPLICATION_XML},
        {"application/json", CT_APPLICATION_JSON},
        {"application/x-javascript", CT_APPLICATION_X_JAVASCRIPT},
        {"text/css", CT_TEXT_CSS},
        {"text/xml", CT_TEXT_XML},
        {"text/xsl", CT_TEXT_XSL},
        {"application/octet-stream", CT_APPLICATION_OCTET_STREAM},
        {"image/svg+xml", CT_IMAGE_SVG_XML},
        {"application/x-font-truetype", CT_APPLICATION_X_FONT_TRUETYPE},
        {"application/x-font-opentype", CT_APPLICATION_X_FONT_OPENTYPE},
        {"application/font-woff", CT_APPLICATION_FONT_WOFF},
        {"application/font-woff2", CT_APPLICATION_FONT_WOFF2},
        {"application/vnd.ms-fontobject", CT_APPLICATION_VND_MS_FONTOBJ},
        {"application/pdf", CT_APPLICATION_PDF},
        {"image/png", CT_IMAGE_PNG},
        {"image/webp", CT_IMAGE_WEBP},
        {"image/avif", CT_IMAGE_AVIF},
        {"image/jpeg", CT_IMAGE_JPG},
        {"image/gif", CT_IMAGE_GIF},
        {"image/x-icon", CT_IMAGE_XICON},
        {"image/bmp", CT_IMAGE_BMP},
        {"image/icns", CT_IMAGE_ICNS},
        {"application/wasm", CT_APPLICATION_WASM},
        {"text/plain", CT_TEXT_PLAIN},
        {"multipart/form-data", CT_MULTIPART_FORM_DATA}};
    auto iter = map_.find(contentType);
    if (iter == map_.end())
        return CT_CUSTOM;
    return iter->second;
}



namespace dremini
{
namespace internal
{

GeminiClient::GeminiClient(std::string url, trantor::EventLoop* loop, double timeout, intmax_t maxBodySize, double maxTransferDuration)
    : loop_(loop), timeout_(timeout), maxBodySize_(maxBodySize), maxTransferDuration_(maxTransferDuration)
{
    static const std::regex re(R"(([a-z]+):\/\/([^\/:]+)(?:\:([0-9]+))?($|\/.*))");
    std::smatch match;
    if(!std::regex_match(url, match, re))
        throw std::invalid_argument(url + " is no a valid url");

    std::string protocol = match[1];
    host_ = match[2];
    std::string port = match[3];
    std::string path = match[4];

    if(protocol != "gemini")
        throw std::invalid_argument("Must be a gemini URL");
    needNameResolve_ = isIPString(host_);
    port_ = 1965;
    if(port.empty() == false)
    {
        int portNum = std::stoi(port);
        if(portNum >= 65536 || portNum <= 0)
        {
            LOG_ERROR << port << "is not a valid port number";
        }
        port_ = portNum;
    }

    if(path.empty() && url.back() != '/')
        url_ = url + "/";
    else
        url_ = url;
}

void GeminiClient::fire()
{
    if(!needNameResolve_)
    {
        bool isIpV6 = host_.find(":") != std::string::npos;
        client_ = std::make_shared<trantor::TcpClient>(loop_, trantor::InetAddress(host_, port_, isIpV6), "GeminiClient");
        sendRequestInLoop();
        return;
    }
    loop_->runInLoop([thisPtr = shared_from_this()](){
        thisPtr->resolver_ = trantor::Resolver::newResolver(thisPtr->loop_, 10);
        thisPtr->resolver_->resolve(thisPtr->host_, [thisPtr](const trantor::InetAddress &addr){
            if(addr.ipNetEndian() == 0)
            {
                thisPtr->haveResult(ReqResult::BadServerAddress, nullptr);
                return;
            }
            trantor::InetAddress address(addr.toIp(), thisPtr->port_, addr.isIpV6());
            thisPtr->client_ = std::make_shared<trantor::TcpClient>(thisPtr->loop_, address, "GeminiClient");
            thisPtr->sendRequestInLoop();
        });
    });
}

void GeminiClient::haveResult(drogon::ReqResult result, const trantor::MsgBuffer* msg)
{
    loop_->assertInLoopThread();
    if(callbackCalled_ == true)
        return;
    callbackCalled_ = true;

    if(timeout_ > 0)
        loop_->invalidateTimer(timeoutTimerId_);
    if(maxTransferDuration_ > 0)
        loop_->invalidateTimer(transferTimerId_);
    if(result != ReqResult::Ok)
    {
        client_ = nullptr;
        callback_(result, nullptr);
        return;
    }
    if(!gotHeader_)
    {
        client_ = nullptr;
        callback_(ReqResult::BadResponse, nullptr);
        return;
    }

    // check ok. now we can get the body
    auto resp = HttpResponse::newHttpResponse();
    resp->setBody(std::string(msg->peek(), msg->peek()+msg->readableBytes()));
    resp->addHeader("meta", meta_);
    resp->addHeader("gemini-status", std::to_string(status_));
    int httpStatus;
    if(status_ == 20)
        httpStatus = 200;
    else if(status_ == 59)
        httpStatus = 400;
    else if(status_ == 51)
        httpStatus = 404;
    else if(status_ == 43)
        httpStatus = 504;
    else if(status_ == 44)
        httpStatus = 503;
    else if(status_%10 == 4)
        httpStatus = 500;
    else if(status_%10 == 5)
        httpStatus = 400;
    else
        httpStatus = status_/10*100 + status_%10;
    resp->setStatusCode((HttpStatusCode)httpStatus);
    if(status_ >= 20 && status_ < 30)
    {
        auto end = meta_.find(";");
        if(end == std::string::npos)
            end = meta_.size();
        std::string_view ct(meta_.c_str(), end);
        resp->setContentTypeCodeAndCustomString(parseContentType(ct), meta_);
        resp->addHeader("content-type", meta_);
    }
    else
        resp->setContentTypeCode(CT_NONE);
    // we need the client no more. Let's release this as soon as possible to save open file descriptors
    client_ = nullptr;
    callback_(ReqResult::Ok, resp);
}

void GeminiClient::sendRequestInLoop()
{
    // TODO: Validate certificate
    auto weakPtr = weak_from_this();
    client_->enableSSL(false, false, host_);
    client_->setMessageCallback([weakPtr](const trantor::TcpConnectionPtr &connPtr,
              trantor::MsgBuffer *msg) {
        auto thisPtr = weakPtr.lock();
        if (thisPtr)
        {
            thisPtr->onRecvMessage(connPtr, msg);
        }
    });
    client_->setConnectionCallback([weakPtr, this](const trantor::TcpConnectionPtr &connPtr) {
        auto thisPtr = weakPtr.lock();
        if(!thisPtr)
            return;
        LOG_TRACE << "This is " << (void*)thisPtr.get();

        if(connPtr->connected())
        {
            LOG_TRACE << "Connected to server. Sending request. URL is: " << url_; 
            connPtr->send(url_ + "\r\n");
        }
        else
        {
            haveResult(ReqResult::Ok, connPtr->getRecvBuffer());
        }
    });
    client_->setSSLErrorCallback([weakPtr](trantor::SSLError err) {
        auto thisPtr = weakPtr.lock();
        if (!thisPtr)
            return;
        if(thisPtr->timeout_ > 0)
            thisPtr->loop_->invalidateTimer(thisPtr->timeoutTimerId_);
        if(thisPtr->maxTransferDuration_ > 0)
            thisPtr->loop_->invalidateTimer(thisPtr->transferTimerId_);
        if (err == trantor::SSLError::kSSLHandshakeError)
            thisPtr->haveResult(ReqResult::HandshakeError, nullptr);
        else if (err == trantor::SSLError::kSSLInvalidCertificate)
            thisPtr->haveResult(ReqResult::InvalidCertificate, nullptr);
        else
        {
            LOG_FATAL << "Invalid value for SSLError";
            abort();
        }
    });

    client_->setConnectionErrorCallback([weakPtr]() {
        auto thisPtr = weakPtr.lock();
        if (!thisPtr)
            return;
        // can't connect to server
        thisPtr->haveResult(ReqResult::NetworkFailure, nullptr);
    });

    if(timeout_ > 0)
    {
        timeoutTimerId_ = loop_->runAfter(timeout_, [weakPtr](){
            auto thisPtr = weakPtr.lock();
            if(!thisPtr)
                return;
            thisPtr->haveResult(ReqResult::Timeout, nullptr);
        });
    }
    if(maxTransferDuration_ > 0)
    {
        transferTimerId_ = loop_->runAfter(maxTransferDuration_, [weakPtr](){
            auto thisPtr = weakPtr.lock();
            if(!thisPtr)
                return;
            thisPtr->haveResult(ReqResult::Timeout, nullptr);
        });
    }
    client_->connect();
}

void GeminiClient::onRecvMessage(const trantor::TcpConnectionPtr &connPtr,
              trantor::MsgBuffer *msg)
{
    if(timeout_ > 0)
        loop_->invalidateTimer(timeoutTimerId_);
    LOG_TRACE << "Got data from Gemini server";
    if(!gotHeader_)
    {
        const char* crlf = msg->findCRLF();
        if(crlf == nullptr)
        {
            return;
        }
        gotHeader_ = true;

        const string_view header(msg->peek(), std::distance(msg->peek(), crlf));
        LOG_TRACE << "Gemini header is: " << header;
        if(header.size() < 2 || (header.size() >= 3 && header[2] != ' '))
        {
            // bad response
            haveResult(ReqResult::BadResponse, nullptr);
            return;
        }

        status_ = std::stoi(std::string(header.begin(), header.begin()+2));
        if(header.size() >= 4)
            meta_ = std::string(header.begin()+3, header.end());
        if(!downloadMimes_.empty() && status_ / 10 == 2)
        {
            std::string mime = meta_.substr(0, meta_.find_first_of("; ,"));
            if(std::find(downloadMimes_.begin(), downloadMimes_.end(), mime) == downloadMimes_.end()) {
                msg->retrieveAll();
                LOG_TRACE << "Ignoring file of MIME " << mime;
                connPtr->forceClose(); // this triggers the connection close handler which will call haveResult
                return;
            }
        }
        msg->read(std::distance(msg->peek(), crlf)+2);
    }
    if(maxBodySize_ > 0 && msg->readableBytes() > size_t(maxBodySize_))
    {
        LOG_DEBUG << "Recived more data than " << maxBodySize_ << " bites";
        // bad response
        haveResult(ReqResult::BadResponse, nullptr);

        return;
    }

    if(timeout_ > 0)
    {
        auto weakPtr = weak_from_this();
        timeoutTimerId_ = loop_->runAfter(timeout_, [weakPtr](){
            auto thisPtr = weakPtr.lock();
            if(!thisPtr)
                return;
           thisPtr->haveResult(ReqResult::Timeout, nullptr);
        });
    }       
}


}

static std::map<int, std::shared_ptr<internal::GeminiClient>> holder;
static std::mutex holderMutex;
void sendRequest(const std::string& url, const HttpReqCallback& callback, double timeout
    , trantor::EventLoop* loop, intmax_t maxBodySize, const std::vector<std::string>& mimes
    , double maxTransferDuration)
{
    auto client = std::make_shared<::dremini::internal::GeminiClient>(url, loop, timeout, maxBodySize, maxTransferDuration);
    int id;
    {
        std::lock_guard lock(holderMutex);
        for(id = std::abs(rand())+1; holder.find(id) != holder.end(); id = std::abs(rand())+1);
        holder[id] = client;
    }
    client->setCallback([callback, id, loop] (ReqResult result, const HttpResponsePtr& resp) mutable {
        callback(result, resp);

        std::lock_guard lock(holderMutex);
        auto it = holder.find(id);
        assert(it != holder.end());
        loop->queueInLoop([client = it->second]() {
            // client is destroyed here
        });
        holder.erase(it);
    });
    client->setMimes(mimes);
    client->fire();
}
}
