#include "GeminiRenderer.hpp"
#include "GeminiParser.hpp"
#include <drogon/utils/Utilities.h>
#include <drogon/HttpViewData.h>

using namespace drogon;
using namespace dremini;

std::pair<std::string, std::string> dremini::render2Html(const std::string_view gmi_source)
{
    std::string res;
    std::string title;
    auto nodes = parseGemini(gmi_source);

    bool last_is_list = false;
    for(const auto& node : nodes) {
        std::string text = HttpViewData::htmlTranslate(node.text);

        if(node.type == "heading1" && title.empty())
                title = node.text;

        if(node.type != "list" && last_is_list == true)
            res += "</ul>\n";

        if(node.type == "text")
            res += "<p>"+text+"</p>\n";
        else if(node.type == "heading1")
            res += "<h1>"+text+"</h1>\n";
        else if(node.type == "heading2")
            res += "<h2>"+text+"</h2>\n";
        else if(node.type == "heading3")
            res += "<h3>"+text+"</h3>\n";
        else if(node.type == "quote")
            res += "<blockquote>"+text+"</blockquote>\n";
        else if(node.type == "link")
            res += "<a href=\""+node.meta+"\">"+text+"</a>";
        else if(node.type == "preformatted_text")
            res += "<pre>"+text+"</pre>\n";
        else if(node.type == "list") {
            if(last_is_list == false)
                res += "<ul>\n";
            res += "  <li>"+text+"</li>\n";
        }
    }
    return {res, title};
}
