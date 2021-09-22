#include <dremini/GeminiServerPlugin.hpp>
#include <dremini/GeminiServer.hpp>
#include <drogon/HttpAppFramework.h>
#include <json/value.h>
#include <memory>
#include <trantor/net/EventLoopThreadPool.h>

using namespace dremini;
using namespace drogon;
using namespace trantor;

void GeminiServerPlugin::initAndStart(const Json::Value& config)
{

    int numThread = config.get("numThread", 1).asInt();
    if(numThread < 0)
    {
        LOG_FATAL << "numThread must be latger or equal to 1";
        exit(1);
    }

    pool_ = std::make_shared<trantor::EventLoopThreadPool>(numThread, "GeminiServerThreadPool");


    const auto& listeners = config["listeners"];
    if(listeners.isNull())
    {
        LOG_WARN << "Creating Gemini Server without litening to any IP";
    }
    else
    {
        for(const auto& listener : listeners)
        {
            auto key = listener.get("key", "").asString();
            auto cert = listener.get("cert", "").asString();
            auto ip = listener.get("ip", "").asString();
            short port = listener.get("port", 1965).asInt();

            if(key.empty())
            {
                LOG_FATAL << "SSL Key file not specsifed";
                exit(1);
            }
            if(cert.empty())
            {
                LOG_FATAL << "SSL Cert file not specsifed";
                exit(1);
            }
            if(ip.empty())
            {
                LOG_FATAL << "Gemini Server IP must the specsifed";
                exit(1);
            }

            bool isV6 = ip.find(":") != std::string::npos;
            InetAddress addr(ip, port, isV6);
            if(addr.isUnspecified())
            {
                LOG_FATAL << ip << " is not a valid IP address";
            }

            auto server = std::make_unique<GeminiServer>(app().getLoop(), addr, cert, key);
            server->setIoLoopThreadPool(pool_);
            server->start();
            servers_.emplace_back(std::move(server));
        }
    }
}

void GeminiServerPlugin::shutdown()
{
}

