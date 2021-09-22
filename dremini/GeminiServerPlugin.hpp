#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/plugins/Plugin.h>
#include <dremini/GeminiServer.hpp>
#include <memory>
#include <trantor/net/EventLoopThreadPool.h>
#include <vector>

namespace dremini
{
class GeminiServer;

class GeminiServerPlugin : public drogon::Plugin<GeminiServerPlugin>
{
public:
    GeminiServerPlugin() {}
    void initAndStart(const Json::Value &config) override;
    void shutdown() override;

protected:
    std::shared_ptr<trantor::EventLoopThreadPool> pool_;
    std::vector<std::unique_ptr<GeminiServer>> servers_;
};
}

