#include <dremini/GeminiServerPlugin.hpp>
#include <dremini/GeminiServer.hpp>
#include <dremini/GeminiRenderer.hpp>
#include <dremini/Titan.hpp>
#include <drogon/HttpAppFramework.h>
#include <drogon/utils/Utilities.h>
#include <json/value.h>
#include <charconv>
#include <cstdint>
#include <memory>
#include <string>
#include <trantor/net/EventLoopThreadPool.h>

using namespace dremini;
using namespace drogon;
using namespace trantor;

static const std::string_view cssTemplate = R"zz(
html {
	color-scheme: dark;
	background: #171a1d;
	color: #c5cbd3;
	font-family: "SFMono-Regular", Consolas, "Noto Sans Mono", "DejaVu Sans Mono", "Liberation Mono", monospace;
	font-size: 12pt;
	line-height: 1.45;
}

body {
	max-width: 62.841em;
	margin: 0 auto;
	padding: 1.5rem 2rem 3rem;
}

h1, h2, h3 {
	color: #f2c7dd;
	line-height: 1.3;
	margin: 1.75rem 0 0.75rem;
}

h1 {
	font-size: 20pt;
}

h2 {
	font-size: 15pt;
}

h3 {
	font-size: 12pt;
}

h1:before, h2:before, h3:before, a:before {
	color: #8090a5;
	font-weight: normal;
	text-decoration: none;
}

h1:before {
	content: '# ';
}

h2:before {
	content: '## ';
}

h3:before {
	content: '### ';
}

blockquote {
	margin: 1rem 0;
	padding-left: 1rem;
	border-left: 3px solid #4a5d74;
	color: #aeb7c2;
}

pre {
	margin: 1rem 0;
	padding: 1rem;
	background: #20282d;
	border: 1px solid #506878;
	border-radius: 0.25rem;
	color: #d9e7e8;
	font-family: "SFMono-Regular", Consolas, "Noto Sans Mono", "DejaVu Sans Mono", "Liberation Mono", monospace;
	font-variant-ligatures: none;
	line-height: 1.35;
	overflow-x: auto;
}

pre code {
	color: inherit;
}

.preformatted {
	margin: 1rem 0;
}

.preformatted figcaption {
	display: inline-block;
	padding: 0.2rem 0.55rem;
	background: #28343c;
	border: 1px solid #506878;
	border-bottom: 0;
	border-radius: 0.25rem 0.25rem 0 0;
	color: #aebdcb;
	font-size: 0.82em;
	line-height: 1.25;
}

.preformatted pre {
	margin: 0;
	border-top-left-radius: 0;
}

ul {
	padding-left: 1.5rem;
}

li + li {
	margin-top: 0.35rem;
}

a {
	color: #a9c8ff;
	text-decoration-color: #7196cb;
	text-underline-offset: 0.16em;
}

a:visited {
	color: #c6abd8;
}

a:before {
	content: '⇒ ';
}

a:focus-visible, input:focus-visible {
	outline: 2px solid #f2c7dd;
	outline-offset: 2px;
}

hr {
	border: 0;
	border-top: 1px solid #4a5d74;
	margin: 2rem 0;
}

details:not([open]) summary,
details:not([open]) summary a {
	color: #9aa6b5;
}

label {
	display: block;
	color: #f2c7dd;
	font-weight: bold;
	margin-bottom: 0.45rem;
}

input {
	display: block;
	border: 1px solid #506878;
	border-radius: 0;
	background: #20282d;
	color: #d9e7e8;
	padding: 0.65rem 0.75rem;
	font: inherit;
	width: 100%;
}

input[type="submit"]{
	margin-top: 0.85rem;
	width: auto;
	cursor: pointer;
	background: #29363e;
	border-color: #7196cb;
	color: #d9e7e8;
	font-weight: bold;
}

.link {
	margin: 0.5rem 0;
}

@media (max-width: 38rem) {
	html {
		font-size: 16px;
	}

	body {
		padding: 1.25rem 1rem 2rem;
	}
}

)zz";

static const std::string_view htmlTemplate = R"zz(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>__THIS_IS_THIS_TITLE_123456789__</title>
</head>
<body>
<style type="text/css">
    __THIS_IS_THIS_CSS_123456789__
</style>
    __THIS_IS_THIS_BODY_123456789__
    <br><br><br>
    <hr>
    <blockquote>This page is rendered from Gemini Gemtext to HTML. We recommened to get a proper Gemini client for the best experience.</blockquote>
</body>
</html>

)zz";
static const std::string_view userInputTemplate = R"zz(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>__THIS_IS_THIS_TITLE_123456789__</title>
</head>
<body>
<style type="text/css">
    __THIS_IS_THIS_CSS_123456789__
</style>
<form action="" method="get">
    <p>__THIS_IS_THIS_TITLE_123456789__</p>
    <input type="__THIS_IS_TYPE_123456789__" name="query">
    <input type="submit">
</form>
</body>
</html>
)zz";

static const std::string_view certificateRequiredTemplate = R"zz(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>__THIS_IS_THIS_TITLE_123456789__</title>
</head>
<body>
<style type="text/css">
    __THIS_IS_THIS_CSS_123456789__
</style>
<h1>__THIS_IS_THIS_TITLE_123456789__</h1>
<br>
<p>A certificate is required to access this page. This feature may not be avaliable over HTTP. We recommend accessing this resource using a native Gemini porotocol browser.</p>
</body>
</html>
)zz";

static std::optional<int> try_stoi(const std::string_view sv)
{
    int value = 0;
    const auto result = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    if (result.ec == std::errc{} && result.ptr == sv.data() + sv.size())
        return value;
    return std::nullopt;
}

void GeminiServerPlugin::initAndStart(const Json::Value& config)
{
    int numThread = config.get("numThread", 1).asInt();
    if(numThread <= 0)
    {
        LOG_FATAL << "numThread must be greater than or equal to 1";
        exit(1);
    }

    pool_ = std::make_shared<trantor::EventLoopThreadPool>(numThread, "GeminiServerThreadPool");

    installTitanRoutingAdvice();


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

            TitanOptions titanOptions;
            const auto& titan = listener.isMember("titan") ? listener["titan"] : config["titan"];
            if (!titan.isNull())
            {
                titanOptions.enabled = titan.get("enabled", false).asBool();
                const auto maximum = titan.get(
                    "max_upload_bytes", static_cast<Json::Int64>(titanOptions.maxUploadBytes)).asInt64();
                if (maximum < 0 || static_cast<std::uint64_t>(maximum) > std::numeric_limits<std::size_t>::max())
                {
                    LOG_FATAL << "Titan max_upload_bytes must fit in size_t";
                    exit(1);
                }
                titanOptions.maxUploadBytes = static_cast<std::size_t>(maximum);
            }

            bool isV6 = ip.find(":") != std::string::npos;
            InetAddress addr(ip, port, isV6);
            if(addr.isUnspecified())
            {
                LOG_FATAL << ip << " is not a valid IP address";
            }

            auto server = std::make_unique<GeminiServer>(app().getLoop(), addr, key, cert, titanOptions);
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
            // if the response is a gemini response, ignore post handling
            if (req->getHeader("protocol") != "")
                return;

            resp->addHeader("gemini-status", std::to_string(resp->getStatusCode()));
            // Fix gemini-style query parameter. Gemini only accepts a single query apameter in the form of
            // gemini://example.com/foo?some_data . But HTTP expects a query parameter in the form of
            // http://example.com/foo?query=some_data . So we need to fix it.
            if(int(resp->statusCode())/100 == 3) {
                auto query = resp->getHeader("location").find('?');
                if(query == std::string::npos)
                    return;
                auto param = resp->getHeader("location").substr(query+1);
                if(param.find('=') != std::string::npos)
                    return;
                std::string path = resp->getHeader("location").substr(0, query+1);
                resp->addHeader("location", path + "query=" + param);
            }
        });
        app().registerPreSendingAdvice([](const HttpRequestPtr& req, const HttpResponsePtr& resp) {
            if(req->getHeader("protocol") != "")
                return;

            if(resp->contentTypeString().find("text/gemini") == 0)
            {
                auto [body, title] = render2Html(resp->body());
                if(title.empty())
                    title = HttpViewData::htmlTranslate(req->path());
                std::string html = std::string(htmlTemplate);
                // HACK: Should use a more effectent way to compile HTML
                drogon::utils::replaceAll(html, "__THIS_IS_THIS_BODY_123456789__", body);
                drogon::utils::replaceAll(html, "__THIS_IS_THIS_TITLE_123456789__", title);
                drogon::utils::replaceAll(html, "__THIS_IS_THIS_CSS_123456789__", std::string(cssTemplate));
                resp->setBody(html);
                resp->setContentTypeCode(CT_TEXT_HTML);
            }
            else if(resp->getHeader("gemini-status") != "" && try_stoi(resp->getHeader("gemini-status")).value_or(-1)/10 == 1)
            {
                bool sensitive_input = resp->getHeader("gemini-status") == "11";
                std::string html = std::string(userInputTemplate);
                std::string title = HttpViewData::htmlTranslate(resp->getHeader("meta"));
                // HACK: Should use a more effectent way to compile HTML
                drogon::utils::replaceAll(html, "__THIS_IS_THIS_TITLE_123456789__", title);
                drogon::utils::replaceAll(html, "__THIS_IS_THIS_CSS_123456789__", std::string(cssTemplate));
                drogon::utils::replaceAll(html, "__THIS_IS_TYPE_123456789__", sensitive_input ? "password" : "text");
                resp->setBody(html);
                resp->setStatusCode(k200OK);
                resp->setContentTypeCode(CT_TEXT_HTML);
            }
            else if(resp->getHeader("gemini-status") != "" && try_stoi(resp->getHeader("gemini-status")).value_or(-1) == 60)
            {
                std::string html = std::string(certificateRequiredTemplate);
                std::string title = HttpViewData::htmlTranslate(resp->getHeader("meta"));
                // HACK: Should use a more effectent way to compile HTML
                drogon::utils::replaceAll(html, "__THIS_IS_THIS_TITLE_123456789__", title);
                drogon::utils::replaceAll(html, "__THIS_IS_THIS_CSS_123456789__", std::string(cssTemplate));
                resp->setBody(html);
                resp->setStatusCode(k412PreconditionFailed);
                resp->setContentTypeCode(CT_TEXT_HTML);
            }
        });
    }
}

void GeminiServerPlugin::shutdown()
{
}
