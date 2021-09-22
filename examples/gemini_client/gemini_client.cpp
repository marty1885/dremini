#include <drogon/drogon.h>
#include <dremini/GeminiClient.hpp>

using namespace drogon;

int main()
{
    LOG_INFO << "Sending request to gemini://gemini.circumlunar.space/";
    dremini::sendRequest("gemini://gemini.circumlunar.space/"
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
            else if(result == ReqResult::Ok)
                LOG_INFO << "It works!";
            if(!resp)
            {
                LOG_ERROR << "Failed to get respond from server";
                return;
            }

            LOG_INFO << "Body size: " << resp->body().size();

            app().quit();
    });

    app().run();
}
