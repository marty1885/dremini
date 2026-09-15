#include <trantor/net/Resolver.h>
#include <trantor/net/TcpClient.h>
#include <trantor/utils/MsgBuffer.h>

#include <algorithm>
#include <atomic>
#include <dremini/GeminiClient.hpp>
#include <list>
#include <random>
#include <regex>
#include <sstream>
#include <string>

using namespace drogon;

static bool isIPString(const std::string& str) {
    bool isIpV6 = str.find(":") != std::string::npos;
    return !trantor::InetAddress(str, 0, isIpV6).isUnspecified();
}

static ContentType parseContentType(const std::string_view& contentType) {
    static const std::unordered_map<std::string_view, ContentType> map_{
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

static std::optional<int> try_stoi(const std::string_view sv) {
    try {
        size_t pos = 0;
        return std::stoi(sv.data(), &pos);
        if (pos != sv.size())
            return std::nullopt;
    } catch (const std::exception& e) {
        return std::nullopt;
    }
}

namespace dremini {
namespace internal {

GeminiClient::GeminiClient(std::string url, trantor::EventLoop* loop, double timeout,
                           intmax_t maxBodySize, double maxTransferDuration, ServerTrust trust,
                           PeerAddressPolicy peerAddressPolicy)
    : loop_(loop),
      timeout_(timeout),
      maxBodySize_(maxBodySize),
      maxTransferDuration_(maxTransferDuration),
      trust_(std::move(trust)),
      peerAddressPolicy_(std::move(peerAddressPolicy)) {
    static const std::regex re(R"(([a-z]+):\/\/([^\/:]+)(?:\:([0-9]+))?($|\/.*))");
    std::smatch match;
    if (!std::regex_match(url, match, re))
        throw std::invalid_argument(url + " is no a valid url");

    std::string protocol = match[1];
    host_ = match[2];
    std::string port = match[3];
    std::string path = match[4];

    if (protocol != "gemini")
        throw std::invalid_argument("Must be a gemini URL");
    port_ = 1965;
    if (port.empty() == false) {
        int portNum = std::stoi(port);
        if (portNum >= 65536 || portNum <= 0) {
            LOG_ERROR << port << "is not a valid port number";
        }
        port_ = portNum;
    }

    if (path.empty() && url.back() != '/')
        url_ = url + "/";
    else
        url_ = url;
}

static thread_local std::shared_ptr<trantor::Resolver> resolver;

const char* GeminiClient::phaseName() const {
    switch (phase_) {
        case Phase::Resolving:
            return "dns";
        case Phase::ConnectingOrTls:
            return "tcp-or-tls";
        case Phase::AwaitingHeader:
            return "gemini-header";
        case Phase::ReceivingBody:
            return "gemini-body";
        case Phase::Complete:
            return "complete";
    }
    return "unknown";
}

void GeminiClient::fire() {
    if (isIPString(host_)) {
        bool isIpV6 = host_.find(":") != std::string::npos;
        trantor::InetAddress address(host_, port_, isIpV6);
        if (!peerAddressPolicy_(address)) {
            if (loop_->isInLoopThread())
                haveResult(ReqResult::BadServerAddress, nullptr);
            else
                loop_->queueInLoop([thisPtr = shared_from_this()] {
                    thisPtr->haveResult(ReqResult::BadServerAddress, nullptr);
                });
            return;
        }
        peerAddresses_.push_back(address);
        if (loop_->isInLoopThread())
            connectNextAddressInLoop();
        else
            loop_->queueInLoop(
                [thisPtr = shared_from_this()] { thisPtr->connectNextAddressInLoop(); });
        return;
    }

    loop_->runInLoop([thisPtr = shared_from_this()]() {
        if (!resolver)
            resolver = trantor::Resolver::newResolver(thisPtr->loop_, 10);
        resolver->resolve(
            thisPtr->host_, [thisPtr](const std::vector<trantor::InetAddress>& addresses) {
                // NormalResolver completes on its blocking DNS worker pool, while
                // AresResolver completes on the EventLoop.  GeminiClient state,
                // timers, and TcpClient must only be touched by the owning loop.
                // Always enqueue so both resolver implementations have identical
                // callback affinity.
                thisPtr->loop_->queueInLoop([thisPtr, addresses]() {
                    for (const auto& addr : addresses) {
                        const auto ip = addr.toIp();
                        // Resolver failure sentinels constructed from sockaddr
                        // carry a valid-family flag, so isUnspecified() alone is
                        // insufficient.
                        if (!addr.isUnspecified() && ip != "0.0.0.0" && ip != "::") {
                            trantor::InetAddress peer(ip, thisPtr->port_, addr.isIpV6());
                            if (thisPtr->peerAddressPolicy_(peer))
                                thisPtr->peerAddresses_.push_back(peer);
                        }
                    }
                    if (thisPtr->peerAddresses_.empty()) {
                        thisPtr->haveResult(ReqResult::BadServerAddress, nullptr);
                        return;
                    }
                    // Prefer IPv4 on machines whose IPv6 route may be absent or
                    // incomplete, while retaining every result as a fallback.
                    std::stable_sort(
                        thisPtr->peerAddresses_.begin(), thisPtr->peerAddresses_.end(),
                        [](const auto& a, const auto& b) { return !a.isIpV6() && b.isIpV6(); });
                    thisPtr->connectNextAddressInLoop();
                });
            });
    });
}

void GeminiClient::haveResult(drogon::ReqResult result, const trantor::MsgBuffer* msg) {
    loop_->assertInLoopThread();
    if (callbackCalled_ == true)
        return;
    callbackCalled_ = true;

    phase_ = Phase::Complete;
    if (requestTimerActive_) {
        loop_->invalidateTimer(timeoutTimerId_);
        requestTimerActive_ = false;
    }
    if (transferTimerActive_) {
        loop_->invalidateTimer(transferTimerId_);
        transferTimerActive_ = false;
    }
    if (result != ReqResult::Ok) {
        client_ = nullptr;
        callback_(result, nullptr);
        return;
    }
    if (!headerReceived_) {
        client_ = nullptr;
        callback_(ReqResult::BadResponse, nullptr);
        return;
    }

    // check ok. now we can get the body
    auto resp = HttpResponse::newHttpResponse();
    if (client_) {
        if (const auto connection = client_->connection()) {
            if (const auto certificate = connection->peerCertificate())
                resp->addHeader("gemini-certificate-sha256", certificate->sha256Fingerprint());
        }
    }
    resp->setBody(std::string(msg->peek(), msg->peek() + msg->readableBytes()));
    resp->addHeader("meta", resoneseMeta_);
    resp->addHeader("gemini-status", std::to_string(responseStatus_));
    int httpStatus;
    if (responseStatus_ == 20)
        httpStatus = 200;
    else if (responseStatus_ == 59)
        httpStatus = 400;
    else if (responseStatus_ == 51)
        httpStatus = 404;
    else if (responseStatus_ == 43)
        httpStatus = 504;
    else if (responseStatus_ == 44)
        httpStatus = 503;
    else if (responseStatus_ % 10 == 4)
        httpStatus = 500;
    else if (responseStatus_ % 10 == 5)
        httpStatus = 400;
    else
        httpStatus = responseStatus_ / 10 * 100 + responseStatus_ % 10;
    resp->setStatusCode((HttpStatusCode)httpStatus);
    if (responseStatus_ >= 20 && responseStatus_ < 30) {
        auto end = resoneseMeta_.find(";");
        if (end == std::string::npos)
            end = resoneseMeta_.size();
        std::string_view ct(resoneseMeta_.c_str(), end);
        resp->setContentTypeCodeAndCustomString(parseContentType(ct), resoneseMeta_);
        resp->addHeader("content-type", resoneseMeta_);
    } else
        resp->setContentTypeCode(CT_NONE);
    // we need the client no more. Let's release this as soon as possible to save open file
    // descriptors
    client_ = nullptr;
    callback_(ReqResult::Ok, resp);
}

void GeminiClient::connectNextAddressInLoop() {
    loop_->assertInLoopThread();
    if (callbackCalled_)
        return;
    if (nextPeerAddress_ >= peerAddresses_.size()) {
        haveResult(ReqResult::NetworkFailure, nullptr);
        return;
    }

    peerAddress_ = peerAddresses_[nextPeerAddress_++];
    phase_ = Phase::ConnectingOrTls;
    if (!requestTimerActive_ && timeout_ > 0) {
        auto weakPtr = weak_from_this();
        timeoutTimerId_ = loop_->runAfter(timeout_, [weakPtr]() {
            auto thisPtr = weakPtr.lock();
            if (!thisPtr)
                return;
            const char* stage = thisPtr->phaseName();
            trantor::TcpConnectionPtr connection;
            if (thisPtr->client_)
                connection = thisPtr->client_->connection();
            if (thisPtr->phase_ == Phase::ConnectingOrTls)
                stage = connection ? "tls-handshake" : "tcp-connect";
            LOG_WARN << "Gemini request timeout: host=" << thisPtr->host_
                     << " peer=" << thisPtr->peerAddress_.toIpPort() << " phase=" << stage
                     << " socket=" << (connection ? "yes" : "no")
                     << " raw_rx=" << (connection ? connection->bytesReceived() : 0)
                     << " raw_tx=" << (connection ? connection->bytesSent() : 0)
                     << " plaintext_buffered="
                     << (connection ? connection->getRecvBuffer()->readableBytes() : 0);
            thisPtr->haveResult(ReqResult::Timeout, nullptr);
        });
        requestTimerActive_ = true;
    }
    sendRequestInLoop();
}

void GeminiClient::sendRequestInLoop() {
    trustStarted_ = false;
    auto weakPtr = weak_from_this();
    client_ = std::make_shared<trantor::TcpClient>(loop_, peerAddress_, "GeminiClient");
    auto tlsPolicy = trantor::TLSPolicy::defaultClientPolicy(host_);
    // No CA or TOFU policy: accept the TLS handshake, then check only that the
    // presented leaf certificate names the requested host.
    tlsPolicy->setValidate(false).setUseSystemCertStore(false);
    client_->enableSSL(std::move(tlsPolicy));
    client_->setMessageCallback(
        [weakPtr](const trantor::TcpConnectionPtr& connPtr, trantor::MsgBuffer* msg) {
            auto thisPtr = weakPtr.lock();
            if (thisPtr) {
                thisPtr->onRecvMessage(connPtr, msg);
            }
        });
    client_->setConnectionCallback([weakPtr](const trantor::TcpConnectionPtr& connPtr) {
        auto thisPtr = weakPtr.lock();
        if (!thisPtr)
            return;
        LOG_TRACE << "This is " << (void*)thisPtr.get();

        if (connPtr->connected()) {
            if (thisPtr->trustStarted_)
                return;
            const auto certificate = connPtr->peerCertificate();
            if (!certificate) {
                thisPtr->haveResult(ReqResult::InvalidCertificate, nullptr);
                connPtr->forceClose();
                return;
            }
            thisPtr->trustStarted_ = true;
            const auto decision = std::make_shared<std::atomic_bool>(false);
            const std::weak_ptr<trantor::TcpConnection> weakConnection = connPtr;
            try {
                thisPtr->trust_(
                    thisPtr->host_ + ":" + std::to_string(thisPtr->port_), certificate,
                    [weakPtr, weakConnection, decision](bool accepted) {
                        const auto self = weakPtr.lock();
                        if (!self)
                            return;
                        bool expected = false;
                        if (!decision->compare_exchange_strong(expected, true,
                                                               std::memory_order_relaxed))
                            return;
                        self->loop_->runInLoop([weakPtr, weakConnection, accepted] {
                            const auto self = weakPtr.lock();
                            const auto connection = weakConnection.lock();
                            if (!self || !connection || self->callbackCalled_)
                                return;
                            if (!connection->connected()) {
                                self->haveResult(ReqResult::NetworkFailure, nullptr);
                                return;
                            }
                            if (!accepted) {
                                self->haveResult(ReqResult::InvalidCertificate, nullptr);
                                connection->forceClose();
                                return;
                            }
                            self->phase_ = Phase::AwaitingHeader;
                            LOG_TRACE << "Gemini server certificate accepted; sending request";
                            connection->send(self->url_ + "\r\n");
                        });
                    });
            } catch (...) {
                thisPtr->haveResult(ReqResult::InvalidCertificate, nullptr);
                connPtr->forceClose();
            }
        } else {
            thisPtr->haveResult(ReqResult::Ok, connPtr->getRecvBuffer());
        }
    });
    client_->setSSLErrorCallback([weakPtr](trantor::SSLError err) {
        auto thisPtr = weakPtr.lock();
        if (!thisPtr)
            return;
        if (err == trantor::SSLError::kSSLHandshakeError)
            thisPtr->haveResult(ReqResult::HandshakeError, nullptr);
        else if (err == trantor::SSLError::kSSLInvalidCertificate)
            thisPtr->haveResult(ReqResult::InvalidCertificate, nullptr);
        else if (err == trantor::SSLError::kSSLProtocolError)
            thisPtr->haveResult(ReqResult::EncryptionFailure, nullptr);
        else {
            LOG_FATAL << "Invalid value for SSLError";
            abort();
        }
    });

    client_->setConnectionErrorCallback([weakPtr]() {
        auto thisPtr = weakPtr.lock();
        if (!thisPtr)
            return;
        LOG_WARN << "Gemini TCP connection failed: host=" << thisPtr->host_
                 << " peer=" << thisPtr->peerAddress_.toIpPort();
        if (thisPtr->nextPeerAddress_ < thisPtr->peerAddresses_.size()) {
            // Connector invokes this callback from inside the current
            // TcpClient. Replace that client only after its callback unwinds.
            thisPtr->loop_->queueInLoop([thisPtr]() {
                thisPtr->client_.reset();
                thisPtr->connectNextAddressInLoop();
            });
            return;
        }
        thisPtr->haveResult(ReqResult::NetworkFailure, nullptr);
    });
    client_->connect();
}

void GeminiClient::onRecvMessage(const trantor::TcpConnectionPtr& connPtr,
                                 trantor::MsgBuffer* msg) {
    LOG_TRACE << "Got data from Gemini server";

    if (!headerReceived_) {
        const char* crlf = msg->findCRLF();
        if (crlf == nullptr) {
            if (msg->readableBytes() > 1024) {
                haveResult(ReqResult::BadResponse, nullptr);
                return;
            }
            return;
        }
        headerReceived_ = true;

        if (requestTimerActive_) {
            loop_->invalidateTimer(timeoutTimerId_);
            requestTimerActive_ = false;
        }
        phase_ = Phase::ReceivingBody;
        if (maxTransferDuration_ > 0 && !transferTimerActive_) {
            auto weakPtr = weak_from_this();
            transferTimerId_ = loop_->runAfter(maxTransferDuration_, [weakPtr]() {
                auto thisPtr = weakPtr.lock();
                if (!thisPtr)
                    return;
                LOG_WARN << "Gemini transfer timeout: host=" << thisPtr->host_
                         << " peer=" << thisPtr->peerAddress_.toIpPort()
                         << " phase=" << thisPtr->phaseName();
                thisPtr->haveResult(ReqResult::Timeout, nullptr);
            });
            transferTimerActive_ = true;
        }

        const std::string_view header(msg->peek(), std::distance(msg->peek(), crlf));
        LOG_TRACE << "Gemini header is: " << header;
        if (header.size() < 2 || (header.size() >= 3 && header[2] != ' ') || header.size() > 1024 ||
            header.find('\n') != std::string::npos) {
            // bad response
            haveResult(ReqResult::BadResponse, nullptr);
            return;
        }

        auto statusCode = try_stoi(std::string(header.begin(), header.begin() + 2));
        if (statusCode.has_value() == false) {
            // bad response again
            haveResult(ReqResult::BadResponse, nullptr);
            return;
        }
        responseStatus_ = statusCode.value();
        if (header.size() >= 4) {
            // remove leading spaces because some non-compliant servers send them
            auto meta = header.substr(3);
            auto idx = meta.find_first_not_of(" \t");
            if (idx != std::string::npos)
                resoneseMeta_ = std::string(meta.begin() + idx, meta.end());
            else
                resoneseMeta_ = "";
        }
        if (!downloadMimes_.empty() && responseStatus_ / 10 == 2) {
            std::string mime = resoneseMeta_.substr(0, resoneseMeta_.find_first_of("; ,"));
            if (std::find(downloadMimes_.begin(), downloadMimes_.end(), mime) ==
                downloadMimes_.end()) {
                msg->retrieveAll();
                LOG_TRACE << "Ignoring file of MIME " << mime;
                connPtr->forceClose();  // this triggers the connection close handler which will
                                        // call haveResult
                return;
            }
        }
        msg->read(std::distance(msg->peek(), crlf) + 2);
    }

    if (maxBodySize_ < 0 || msg->readableBytes() > maxBodySize_) {
        haveResult(ReqResult::Ok, msg);
        connPtr->shutdown();
        return;
    }
}

}  // namespace internal

static std::list<std::shared_ptr<internal::GeminiClient>> holder;
static std::mutex holderMutex;
void sendRequest(const std::string& url, const HttpReqCallback& callback, double timeout,
                 trantor::EventLoop* loop, intmax_t maxBodySize,
                 const std::vector<std::string>& mimes, double maxTransferDuration,
                 ServerTrust trust, PeerAddressPolicy peerAddressPolicy) {
    auto client = std::make_shared<::dremini::internal::GeminiClient>(
        url, loop, timeout, maxBodySize, maxTransferDuration, std::move(trust),
        std::move(peerAddressPolicy));
    decltype(holder)::iterator it;
    {
        std::lock_guard<std::mutex> lock(holderMutex);
        holder.push_back(client);
        it = std::prev(holder.end());
    }
    client->setCallback(
        [callback, it, loop](ReqResult result, const HttpResponsePtr& resp) mutable {
            callback(result, resp);

            std::lock_guard lock(holderMutex);
            loop->queueInLoop([client = std::move(*it)]() {
                // client is destroyed here
            });
            holder.erase(it);
        });
    client->setMimes(mimes);
    client->fire();
}
}  // namespace dremini
