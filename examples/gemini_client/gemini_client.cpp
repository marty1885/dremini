#include <drogon/drogon.h>
#include <dremini/GeminiClient.hpp>
#include <trantor/utils/Logger.h>

using namespace drogon;

int main()
{
    LOG_INFO << "Sending request to gemini://geminispace.info/";
    trantor::Logger::setLogLevel(trantor::Logger::LogLevel::kTrace);
    dremini::sendRequest("gemini://geminispace.info/"
        , [](ReqResult result, const HttpResponsePtr& resp) {
            if(result == ReqResult::BadResponse)
                LOG_ERROR << "BadResponse";
            else if(result == ReqResult::BadServerAddress)
                LOG_ERROR << "BadServerAddress";
            else if(result == ReqResult::HandshakeError)
                LOG_ERROR << "HandshakeError";
            else if(result == ReqResult::InvalidCertificate)
                LOG_ERROR << "InvalidCertificate";
            else if(result == ReqResult::NetworkFailure)
                LOG_ERROR << "NetworkFailure";
            else if(result == ReqResult::Timeout)
                LOG_ERROR << "Timeout";
            else if(result == ReqResult::Ok)
                LOG_INFO << "It works!";
            if(!resp) {
                LOG_ERROR << "Failed to get respond from server";
            }
            else {
                LOG_INFO << "Body size: " << resp->body().size();
            }

            app().quit();
    }, 10, app().getLoop(), 0xffffff, {}, 10);

    app().run();
}
