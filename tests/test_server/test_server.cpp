#include <drogon/drogon.h>
#include <dremini/GeminiServer.hpp>
#include <memory>
#include <trantor/net/EventLoopThreadPool.h>
#include <dremini/GeminiServerPlugin.hpp>

using namespace drogon;
using namespace dremini;
using namespace trantor;

int main()
{
    app().setLogLevel(Logger::LogLevel::kTrace);
    app().registerHandler("/apitest",
        [](const HttpRequestPtr& req,
           std::function<void (const HttpResponsePtr &)> &&callback)
        {
            auto resp = HttpResponse::newHttpResponse();
            resp->setBody("Hello");
            resp->setContentTypeCodeAndCustomString(CT_CUSTOM, "text/gemini");
            callback(resp);
        });
    app().registerHandler("/apitest2",
        [](const HttpRequestPtr& req,
           std::function<void (const HttpResponsePtr &)> &&callback)
        {
            auto data = req->getParameter("query");
            auto resp = HttpResponse::newHttpResponse();
            resp->setBody(data);
            resp->setContentTypeCode(CT_TEXT_PLAIN);
            callback(resp);
        });
    app().loadConfigFile("drogon.config.json");

    app().run();
}
