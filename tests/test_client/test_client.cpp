#include <trantor/utils/Logger.h>
#define DROGON_TEST_MAIN
#include <dremini/GeminiClient.hpp>
#include <drogon/drogon_test.h>
using namespace drogon;


DROGON_TEST(GeminiTest)
{
    // Gemini requests can trigger HTTP Handlers
    dremini::sendRequest("gemini://127.0.0.1/apitest", [TEST_CTX](ReqResult result, const HttpResponsePtr& resp){
        REQUIRE(result == ReqResult::Ok);
        REQUIRE(resp != nullptr);

        CHECK(resp->body() == "Hello");
        CHECK((int)resp->statusCode() == 200);
        CHECK(resp->getHeader("gemini-status") == "20");
    });

    // 404 responses are correctly handled 
    dremini::sendRequest("gemini://127.0.0.1/does_not_exist", [TEST_CTX](ReqResult result, const HttpResponsePtr& resp){
        REQUIRE(result == ReqResult::Ok);
        REQUIRE(resp != nullptr);

        CHECK((int)resp->statusCode() == 404);
        CHECK(resp->getHeader("gemini-status") == "51");
        CHECK(resp->body().size() == 0);
    });

    // Static file router
    dremini::sendRequest("gemini://127.0.0.1/about.gmi", [TEST_CTX](ReqResult result, const HttpResponsePtr& resp){
        REQUIRE(result == ReqResult::Ok);
        REQUIRE(resp != nullptr);

        CHECK((int)resp->statusCode() == 200);
        CHECK(resp->getHeader("content-type") == "text/gemini");
        CHECK(resp->contentTypeString() == "text/gemini");
        CHECK(resp->body().size() != 0);
    });

    // implecit page
    dremini::sendRequest("gemini://127.0.0.1/", [TEST_CTX](ReqResult result, const HttpResponsePtr& resp){
        REQUIRE(result == ReqResult::Ok);
        REQUIRE(resp != nullptr);

        CHECK((int)resp->statusCode() == 200);
        CHECK(resp->contentTypeString() == "text/gemini");
        CHECK(resp->body().size() != 0);
    });

    // Missing / at end
    dremini::sendRequest("gemini://127.0.0.1", [TEST_CTX](ReqResult result, const HttpResponsePtr& resp){
        REQUIRE(result == ReqResult::Ok);
        REQUIRE(resp != nullptr);

        CHECK((int)resp->statusCode() == 200);
        CHECK(resp->contentTypeString() == "text/gemini");
        CHECK(resp->body().size() != 0);
    });

    // Query parameter parsed correctly
    dremini::sendRequest("gemini://127.0.0.1/apitest2?hello", [TEST_CTX](ReqResult result, const HttpResponsePtr& resp){
        REQUIRE(result == ReqResult::Ok);
        REQUIRE(resp != nullptr);

        CHECK((int)resp->statusCode() == 200);
        CHECK(resp->body() == "hello");
    });
}

int main(int argc, char** argv) 
{
    app().setLogLevel(trantor::Logger::LogLevel::kTrace);
    std::promise<void> p1;
    std::future<void> f1 = p1.get_future();

    // Start the main loop on another thread
    std::thread thr([&]() {
        // Queues the promis to be fulfilled after starting the loop
        app().getLoop()->queueInLoop([&p1]() { p1.set_value(); });
        app().run();
    });

    // The future is only satisfied after the event loop started
    f1.get();
    int status = test::run(argc, argv);

    // Ask the event loop to shutdown and wait
    app().getLoop()->queueInLoop([]() { app().quit(); });
    thr.join();
    return status;
}

