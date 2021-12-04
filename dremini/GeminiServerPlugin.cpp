#include <dremini/GeminiServerPlugin.hpp>
#include <dremini/GeminiServer.hpp>
#include <dremini/GeminiRenderer.hpp>
#include <drogon/HttpAppFramework.h>
#include <drogon/utils/Utilities.h>
#include <json/value.h>
#include <memory>
#include <string>
#include <trantor/net/EventLoopThreadPool.h>

using namespace dremini;
using namespace drogon;
using namespace trantor;

static const std::string_view cssTemplate = R"zz(
html {
	font-family: monospace;
	color: #a9a9a9;
	font-size: 11pt;
}

body {
	background: #202020;
	max-width: 920px;
	margin: 0 auto;
	padding: 1rem 2rem;
}

h1 {
	color: #FFAA00;
	font-size: 20pt;
}

h2 {
	color: #E08000;
	font-size: 15pt;
}

h3 {
	color: #C06000;
	font-size: 12pt;
}

blockquote {
	background-color: #333;
	border-left: 3px solid #444;
	margin: 0 -1rem 0 calc(-1rem - 3px);
	padding: 1rem;
}

ul {
	margin-left: 0;
	padding: 0;
}

li {
	padding: 0;
}

li:not(:last-child) {
	margin-bottom: 0.5rem;
}

a {
	position: relative;
}

a:before {
	content: '⇒';
	color: #999;
	text-decoration: none;
	font-weight: bold;
	position: absolute;
	left: -1.25rem;
}

h1:before {
	content: '#';
	color: #AAA;
	text-decoration: none;
	font-weight: bold;
	position: relative;
	left: -1.25rem;
}

h2:before {
	content: '##';
	color: #AAA;
	text-decoration: none;
	font-weight: bold;
	position: relative;
	left: -1.25rem;
}

h3:before {
	content: '###';
	color: #AAA;
	text-decoration: none;
	font-weight: bold;
	position: relative;
	left: -1.25rem;
}

pre {
	background-color: #202020;
	/* color: #40c9ff; */
        color: #fff;
	margin: 0 -1rem;
	padding: 1rem;
	overflow-x: auto;
        line-height: calc(1em + 1px);
}

details:not([open]) summary,
details:not([open]) summary a {
	color: gray;
}

details summary a:before {
	display: none;
}

dl dt {
	font-weight: bold;
}

dl dt:not(:first-child) {
	margin-top: 0.5rem;
}

a {
	color: #00FF00;
    line-height: 2rem;
}

a:visited {
	color: #008000;
}

label {
	display: block;
	font-weight: bold;
	margin-bottom: 0.5rem;
}

input {
	display: block;
	border: 1px solid #888;
	padding: .375rem;
	line-height: 1.25rem;
	transition: border-color .15s ease-in-out,box-shadow .15s ease-in-out;
	width: 100%;
}

input:focus {
	outline: 0;
	border-color: #80bdff;
	box-shadow: 0 0 0 0.2rem rgba(0,123,255,.25);
}

input[type="submit"]{
        margin-top: 0.5rem;
        width: auto;
}

)zz";

static const std::string_view htmlTemplate = R"zz(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>__THIS_IS_THIS_TITLE_123456789__</title>
</head>
<body>
<style type="text/css">
    __THIS_IS_THIS_CSS_123456789__
</style>
    __THIS_IS_THIS_BODY_123456789__
    <br><br><br>
    <hr>
    <blockquote>This page is rended from Gemini Gemtext to HTML. We recommened to get a proper Gemini client for the best experience.</blockquote>
</body>
</html>

)zz";
static const std::string_view userInputTemplate = R"zz(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>__THIS_IS_THIS_TITLE_123456789__</title>
</head>
<body>
<style type="text/css">
    __THIS_IS_THIS_CSS_123456789__
</style>
<form action="" method="get">
    <p>__THIS_IS_THIS_TITLE_123456789__</p>
    <input type="text" name="query">
    <input type="submit">
</form>
</body>
</html>
)zz";

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
                LOG_FATAL << "Gemini Server IP not specsifed";
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
    const auto& translate_to_html = config["translate_to_html"];
    if(!translate_to_html.isNull() && translate_to_html.asBool())
    {
        // Convert gemini into HTML if approprate
        app().registerPostHandlingAdvice([](const HttpRequestPtr& req, const HttpResponsePtr& resp) {
            // HACK: Use 418 as a marker to indicate this is a Gemini status 10 (INPUT).
            // Otherwise drogon drops http responses < 100
            // TODO: Handle status 11 - sensitive input
            if(int(resp->statusCode())/10 == 1)
                resp->setStatusCode(k418ImATeapot);
        });
        app().registerPreSendingAdvice([](const HttpRequestPtr& req, const HttpResponsePtr& resp) {
            if(req->getHeader("protocol") == "" && resp->contentTypeString().find("text/gemini") == 0)
            {
                auto [body, title] = render2Html(resp->body());
                if(title.empty())
                    title = req->path();
                std::string html = std::string(htmlTemplate);
                // HACK: Should use a more effectent way to compile HTML
                drogon::utils::replaceAll(html, "__THIS_IS_THIS_BODY_123456789__", body);
                drogon::utils::replaceAll(html, "__THIS_IS_THIS_TITLE_123456789__", title);
                drogon::utils::replaceAll(html, "__THIS_IS_THIS_CSS_123456789__", std::string(cssTemplate));
                resp->setBody(html);
                resp->setContentTypeCode(CT_TEXT_HTML);
            }
            else if(resp->statusCode() == k418ImATeapot)
            {
                std::string html = std::string(userInputTemplate);
                std::string title = resp->getHeader("meta");
                // HACK: Should use a more effectent way to compile HTML
                drogon::utils::replaceAll(html, "__THIS_IS_THIS_TITLE_123456789__", title);
                drogon::utils::replaceAll(html, "__THIS_IS_THIS_CSS_123456789__", std::string(cssTemplate));
                resp->setBody(html);
                resp->setContentTypeCode(CT_TEXT_HTML);
            }
        });
    }
}

void GeminiServerPlugin::shutdown()
{
}

