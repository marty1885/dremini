#include "GeminiRenderer.hpp"
#include "GeminiParser.hpp"
#include <drogon/utils/Utilities.h>
#include <drogon/HttpViewData.h>

#include <stack>
#include <deque>
#include <string_view>

using namespace drogon;
using namespace dremini;

struct ParserState
{
    std::string result;
    size_t pos = 0;
    bool in_code = false;
    bool in_italic = false;
    bool in_strong = false;
    std::stack<std::string> styles;
    std::stack<char> style_symbols;
};

static std::string renderPlainText(const std::string_view input)
{
    // No special character -> No need to parse
    if(input.find_first_of("*_`") == std::string_view::npos)
        return std::string(input);

    std::deque<ParserState> state_stack;
    state_stack.push_front({});

    while(!state_stack.empty()) {
        auto& state = state_stack.front();
        // Finish parsing. Return if we are in an accept state.
        if(state.pos >= input.size()) {
            if(state.styles.empty())
                return state.result;
            else {
                std::swap(*state_stack.begin(), *state_stack.rbegin());
                state_stack.pop_back();
                continue;
            }
        }

        // Process all normal text in a single go. Faster then character by character
        auto remain = input.substr(state.pos);
        auto n = remain.find_first_of("`*_");
        if(n == std::string_view::npos) {
            state.result += remain;
            state.pos = input.size();
            continue;
        }
        state.result += remain.substr(0, n);
        state.pos += n;
        const char ch = input[state.pos++];
        if(ch == '`') {
            if(state.in_code && !state.styles.empty() && state.styles.top() == "code") {
                state.in_code = false;
                state.result += "</code>";
                state.styles.pop();
            } else if(state.in_code == false) {
                auto backtrack_state = state;
                backtrack_state.result += ch;
                state_stack.push_back(backtrack_state);

                state.in_code = true;
                state.result += "<code>";
                state.styles.push("code");
            }
        }
        else if((ch == '*' || ch == '_') && !state.in_code) {
            bool could_be_strong = state.pos < input.size() && input[state.pos] == ch;

            if(could_be_strong) {
                bool prev_is_allowed = false;
                // * is allowed anywhere in the text. But _ must proceed with a space, * or line start
                if(ch == '*') {
                    prev_is_allowed = true;
                } else {
                    // _ must be preceded by a space, * or line start
                    if(state.pos > 2 && (input[state.pos - 2] == ' ' || input[state.pos - 2] == '*'))
                        prev_is_allowed = true;
                    else if(state.pos-1 == 0)
                        prev_is_allowed = true;
                }
                bool prev_is_space = state.pos > 2 && input[state.pos - 2] == ' ';
                state.pos += 1;
                bool next_is_space = state.pos < input.size() && input[state.pos] == ' ';

                if(state.in_strong && !state.styles.empty() && state.styles.top() == "strong" && !prev_is_space
                    && state.style_symbols.top() == ch) {
                    state.in_strong = false;
                    state.result += "</strong>";
                    state.styles.pop();
                    state.style_symbols.pop();
                } else if(state.in_strong == false && !next_is_space && prev_is_allowed) {
                    auto backtrack_state = state;
                    backtrack_state.result += std::string(2, ch);
                    state_stack.push_back(backtrack_state);

                    state.in_strong = true;
                    state.result += "<strong>";
                    state.styles.push("strong");
                    state.style_symbols.push(ch);
                } else {
                    state.result += std::string(2, ch);
                }
            }
            else {
                bool next_is_space = state.pos < input.size() && input[state.pos] == ' ';
                bool prev_is_space = state.pos > 2 && input[state.pos - 2] == ' ';
                bool prev_is_allowed = false;
                // * is allowed anywhere in the text. But _ must proceed with a space, * or line start
                if(ch == '*') {
                    prev_is_allowed = true;
                } else {
                    // _ must be preceded by a space, * or line start
                    if(state.pos > 2 && (input[state.pos - 2] == ' ' || input[state.pos - 2] == '*'))
                        prev_is_allowed = true;
                    else if(state.pos-1 == 0)
                        prev_is_allowed = true;
                }
                if(state.in_italic && !state.styles.empty() && state.styles.top() == "italic" && !prev_is_space
                    && state.style_symbols.top() == ch) {
                    state.in_italic = false;
                    state.result += "</i>";
                    state.styles.pop();
                    state.style_symbols.pop();
                } else if(state.in_italic == false && !next_is_space && prev_is_allowed) {
                    auto backtrack_state = state;
                    backtrack_state.result += ch;
                    state_stack.push_back(backtrack_state);

                    state.in_italic = true;
                    state.result += "<i>";
                    state.styles.push("italic");
                    state.style_symbols.push(ch);
                } else {
                    state.result += ch;
                }
            }
        }
        else {
            state.result += ch;
        }
    }

    throw std::runtime_error("Parser ended in an invalid state. This is a bug.");
}

static bool isSingleCharRepeat(const std::string_view str)
{
    if(str.empty())
        return false;
    const char ch = str[0];
    for(const auto c : str) {
        if(c != ch)
            return false;
    }
    return true;
}

std::pair<std::string, std::string> dremini::render2Html(const std::string_view gmi_source, bool extended_mode)
{
    auto nodes = parseGemini(gmi_source);
    return render2Html(nodes, extended_mode);
}

std::pair<std::string, std::string> dremini::render2Html(const std::vector<GeminiASTNode>& nodes, bool extended_mode)
{
    std::string res;
    std::string title;
    bool last_is_list = false;
    bool last_is_backquote = false;
    for(const auto& node : nodes) {
        std::string text = HttpViewData::htmlTranslate(node.text);

        if(node.type == "heading1" && title.empty())
                title = node.text;

        if(node.type != "list" && last_is_list == true)
            res += "</ul>\n";
        if(node.type != "quote" && last_is_backquote == true)
            res += "</blockquote>\n";

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
        else if(node.type == "link")
        {
            if(text.empty())
                text = node.meta;
            std::string meta = node.meta;
            if(extended_mode) {
                // If link to image. We convert it to <img> tag
                if(meta.rfind(".png") != std::string::npos || meta.rfind(".jpg") != std::string::npos
                    || meta.rfind(".webp") != std::string::npos) {
                    const std::string& alt = text;
                    res += "<img src=\"" + meta + "\" alt=\"" + alt + "\" title=\"" + alt + "\">";
                    continue;
                }
                // link to audio (mp3, ogg, wav) => <audio> tag
                if(meta.rfind(".mp3") != std::string::npos || meta.rfind(".ogg") != std::string::npos
                    || meta.rfind(".wav") != std::string::npos) {
                    res += "<audio controls preload=\"none\"><source src=\"" + meta + "\">Your browser does not support the audio element.</audio>";
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
            bool is_external = meta.find("://") != std::string::npos;
            std::string target = is_external ? "_blank" : "_self";
            res += "<div class=\"link\"><a href=\""+meta+"\" target=\""+target+"\">"+text+"</a></div>\n";
        }
        else if(node.type == "preformatted_text") {
            bool meta_could_be_language = node.meta.find_first_of(" *'\"/\\()[]{};><`") == std::string::npos
                && node.meta == utils::urlEncode(node.meta) && !node.meta.empty();
            if(extended_mode && meta_could_be_language) {
                res += "<pre><code class=\"language-"+node.meta+"\">"+text+"</code></pre>\n";
            }
            else
                res += "<pre><code>"+text+"</code></pre>\n";
        }
        if(node.type == "list") {
            if(last_is_list == false)
                res += "<ul>\n";
            res += "  <li>"+(extended_mode ? renderPlainText(text) : text)+"</li>\n";
            last_is_list = true;
        }
        else {
            last_is_list = false;
        }

        if(node.type == "quote")
        {
            if(last_is_backquote == false)
                res += "<blockquote>\n";
            res += (last_is_backquote ? "<br>" : "") + text;
            last_is_backquote = true;
        }
        else {
            last_is_backquote = false;
        }
    }
    if(last_is_backquote)
        res += "</backquote>\n";
    else if(last_is_list)
        res += "</ul>\n";
    return {res, HttpViewData::htmlTranslate(title)};
}