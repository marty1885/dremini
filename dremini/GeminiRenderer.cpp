#include "GeminiRenderer.hpp"
#include "GeminiParser.hpp"
#include <drogon/utils/Utilities.h>
#include <drogon/HttpViewData.h>

using namespace drogon;
using namespace dremini;

static bool isSingleCharRepeat(const std::string &str)
{
    if(str.empty())
        return false;
    char ch = str[0];
    for(auto c : str) {
        if(c != ch)
            return false;
    }
    return true;
}

static std::string renderPlainText(const std::string &text)
{
    // scan for `` pairs. If found, add the text in the pair as a code block
    std::string res;
    std::string current_text;
    bool in_code_block = false;
    for(auto c : text) {
        if(c == '`' && !in_code_block) {
            in_code_block = true;
            if(!current_text.empty()) {
                res += current_text;
                current_text.clear();
            }
        } else if(c == '`' && in_code_block) {
            in_code_block = false;
            if(!current_text.empty()) {
                res += "<code>" + current_text + "</code>";
                current_text.clear();
            }
        } else {
            current_text += c;
        }
    }
    if(in_code_block)
        return res+"`"+current_text;
    else
        return res+current_text;
}

std::pair<std::string, std::string> dremini::render2Html(const std::string_view gmi_source, bool extended_mode)
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

        if(node.type == "text") {
            if(extended_mode) {
                // all "-" or "=" and is at least 3 chars => horizontal line
                if(!text.empty() && (text[0] == '-' || text[0] == '=')
                    && text.size() >= 3 && isSingleCharRepeat(text)) {

                    res += "<hr>\n";
                    continue;
                }
                res += "<p>"+renderPlainText(text)+"</p>\n";
            }
            else
                res += "<p>"+text+"</p>\n";
        }
        else if(node.type == "heading1")
            res += "<h1>"+text+"</h1>\n";
        else if(node.type == "heading2")
            res += "<h2>"+text+"</h2>\n";
        else if(node.type == "heading3")
            res += "<h3>"+text+"</h3>\n";
        else if(node.type == "quote")
            res += "<blockquote>"+text+"</blockquote>\n";
        else if(node.type == "link")
        {
            if(text.empty())
                text = node.meta;
            std::string meta = node.meta;
            // If link to image. We convert it to <img> tag
            if(extended_mode) {
                if(meta.rfind(".png") != std::string::npos || meta.rfind(".jpg") != std::string::npos) {
                    const std::string& alt = text;
                    res += "<img src=\"" + meta + "\" alt=\"" + alt + "\">";
                    continue;
                }
            }
            // Quick and dirty parameter hack
            auto n = meta.find('?');
            if(n != std::string::npos && meta.find("gemini://") != 0 && meta.find("http") != 0) {
                std::string url = meta.substr(0, n);
                std::string param = meta.substr(n+1);
                meta = url + "?query=" + param;
            }
            // Open new tab if external link
            // HACK: Port TLGS's URL parser and use that to determine if external link
            bool is_external = meta.find("://") != 0;
            std::string target = is_external ? "_blank" : "_self";
            res += "<div class=\"link\"><a href=\""+meta+"\" target=\""+target+"\">"+text+"</a></div>\n";
        }
        else if(node.type == "preformatted_text")
            res += "<pre><code>"+text+"</code></pre>\n";

        if(node.type == "list") {
            if(last_is_list == false)
                res += "<ul>\n";
            res += "  <li>"+(extended_mode ? renderPlainText(text) : text)+"</li>\n";
            last_is_list = true;
        }
        else {
            last_is_list = false;
        }
    }
    return {res, HttpViewData::htmlTranslate(title)};
}
