#include <drogon/drogon.h>
#include <dremini/GeminiServerPlugin.hpp>
#include <memory>
#include <random>
#include <string>

using namespace drogon;
using namespace dremini;

int main()
{
    //app().setLogLevel(Logger::LogLevel::kTrace);
    app().registerHandler("/random_number",
        [](const HttpRequestPtr& req,
           std::function<void (const HttpResponsePtr &)> &&callback)
        {
            static std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> dist(0, 1000); 
            auto resp = HttpResponse::newHttpResponse();
            resp->setBody("# Reandom Number\n"
                    "Your random number is " + std::to_string(dist(rng)) + "\n"
                    "\n"
                    "Refresh this page to get another number\n");
            resp->setContentTypeCodeAndCustomString(CT_CUSTOM, "text/gemini");
            callback(resp);
        });

    app().registerHandler("/unix_epoch",
        [](const HttpRequestPtr& req,
           std::function<void (const HttpResponsePtr &)> &&callback)
        {
            // Also can generate Gemini files from drogon's CSP templates
            auto resp = HttpResponse::newHttpViewResponse("unix_time");
            resp->setContentTypeCodeAndCustomString(CT_CUSTOM, "text/gemini");
            callback(resp);
        });

    app().registerHandler("/user_input",
        [](const HttpRequestPtr& req,
           std::function<void (const HttpResponsePtr &)> &&callback)
        {
            const auto& input = req->getParameter("query");
            if(input.empty())
            {
                auto resp = HttpResponse::newHttpResponse();
                resp->addHeader("meta", "Something you want to say");
                resp->setStatusCode((HttpStatusCode)10);
                callback(resp);
                return;
            }


            auto decoded = utils::urlDecode(input);
            auto resp = HttpResponse::newHttpResponse();
            resp->setBody("# User input\nYou said " + decoded + "\n");
            resp->setContentTypeCodeAndCustomString(CT_CUSTOM, "text/gemini");
            callback(resp);
        });

    app().loadConfigFile("drogon.config.json");

    app().run();
}
