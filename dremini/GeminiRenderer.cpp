#include "GeminiRenderer.hpp"
#include "GeminiParser.hpp"
#include <algorithm>
#include <deque>
#include <drogon/utils/Utilities.h>
#include <drogon/HttpViewData.h>

#include <deque>
#include <stack>
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
    bool in_strike = false;
    std::stack<std::string> styles;
    std::stack<char> style_symbols;
};

static std::string renderPlainText(const std::string_view input)
{
    // No special character -> No need to parse
    if(input.find_first_of("*_`~") == std::string_view::npos)
        return std::string(input);

    std::deque<ParserState> state_stack;
    state_stack.push_back({});

    while(!state_stack.empty()) {
        auto& state = state_stack.front();
        // Finish parsing. Return if we are in an accept state.
        if(state.pos >= input.size()) {
            if(state.styles.empty())
                return state.result;
            else {
                if(state_stack.size() != 1) {
                    std::swap(*state_stack.begin(), *state_stack.rbegin());
                }
                state_stack.pop_back();
                continue;
            }
        }

        // Process all normal text in a single go. Faster then character by character
        auto remain = input.substr(state.pos);
        auto n = remain.find_first_of("`*_~");
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
        else if(ch == '~' && !state.in_code) {
            bool could_be_strike = state.pos < input.size() && input[state.pos] == ch;
            if(could_be_strike) {
                state.pos += 1;
                bool prev_is_space = state.pos > 2 && input[state.pos - 2] == ' ';
                bool next_is_space = state.pos < input.size() && input[state.pos] == ' ';

                if(state.in_strike && !state.styles.empty() && state.styles.top() == "strike" && !prev_is_space
                    && state.style_symbols.top() == ch) {
                    state.in_strike = false;
                    state.result += "</strike>";
                    state.styles.pop();
                    state.style_symbols.pop();
                    state_stack.pop_back();
                } else if(state.in_strike == false && !next_is_space) {
                    auto backtrack_state = state;
                    backtrack_state.result += std::string(2, ch);
                    state_stack.push_back(backtrack_state);

                    state.in_strike = true;
                    state.result += "<strike>";
                    state.styles.push("strike");
                    state.style_symbols.push(ch);
                } else {
                    state.result += std::string(2, ch);
                }
            }
            else {
                state.result += ch;
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

static std::string urlFriendly(const std::string_view str)
{
    std::string res;
    bool last_is_dash = false;
    const std::string_view replace_with_dash = " -_";
    const std::string_view ignore_chars = ".,:;!?\"'()[]{}<>*/\\@#$%^&+=~`|";
    auto in = [](auto ch, auto set) { return set.find(ch) != std::string_view::npos; };
    for(auto ch : str) {
        if(in(ch, ignore_chars))
            continue;

        ch = std::tolower(ch);

        if(! in(ch, replace_with_dash)) {
            last_is_dash = false;
            res += ch;
            continue;
        }

        if(!last_is_dash) {
            res += '-';
            last_is_dash = true;
        }
    }
    return utils::urlEncode(res);
}

static std::string_view trim(std::string_view str, std::string_view ignore_chars) {
    auto start = str.find_first_not_of(ignore_chars);
    auto end = str.find_last_not_of(ignore_chars);
    return start == std::string_view::npos ? "" : str.substr(start, end - start + 1);
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
    std::set<std::string> paragrah_names;

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
                // TODO: Do we need to translate text in extended mode?
                res += "<p>"+renderPlainText(text)+"</p>\n";
            }
            else
                res += "<p>"+text+"</p>\n";
        }
        else if(node.type == "heading1" || node.type == "heading2" || node.type == "heading3")
        {
            std::string tag = "h1";
            if(node.type == "heading2")
                tag = "h2";
            else if(node.type == "heading3")
                tag = "h3";

            if(!extended_mode || tag == "h1")
                res += "<"+tag+">"+text+"</"+tag+">\n";
            else {
                std::string id = urlFriendly(text);
                if(paragrah_names.find(id) != paragrah_names.end()) {
                    int i = 1;
                    while(paragrah_names.find(id+"-"+std::to_string(i)) != paragrah_names.end())
                        i++;
                    id += "-"+std::to_string(i);
                }
                paragrah_names.insert(id);
                res += "<"+tag+"><a href=\"#"+id+"\" id=\""+id+"\">"+text+"</a></"+tag+">\n";
            }
            continue;
        }
        else if(node.type == "link")
        {
            if(text.empty())
                text = HttpViewData::htmlTranslate(node.meta);
            std::string meta = node.meta;
            if(extended_mode) {
                // If link to image. We convert it to <img> tag
                const std::array<std::string_view, 6> img_exts = {".png", ".jpg", ".webp", ".gif", ".jpeg", ".bmp"};
                if(std::any_of(img_exts.begin(), img_exts.end(), [&meta](const std::string_view ext) { return meta.rfind(ext) != std::string::npos; })) {
                    const std::string& alt = text;
                    res += "<figure><a href=\"" + meta + "\"><img loading=\"lazy\" src=\"" + meta + "\" alt=\"" + alt + "\" title=\"Image: " + alt + "\"></a><figcaption>Image: "+alt+"</figcaption></figure>";
                    continue;
                }
                // link to audio (mp3, ogg, wav) => <audio> tag
                const std::array<std::string_view, 4> audio_exts = {".mp3", ".ogg", ".wav", ".opus"};
                if(std::any_of(audio_exts.begin(), audio_exts.end(), [&meta](const std::string_view ext) { return meta.rfind(ext) != std::string::npos; })) {
                    res += "<figure><audio preload=\"none\" controls><source src=\"" + meta + "\">Your browser does not support the audio element.</audio><figcaption>Audio: "+text+"</figcaption></figure>";
                    continue;
                }
                // link to video (webm, mkv, mp4) => <video> tag
                const std::array<std::string_view, 3> video_exts = {".webm", ".mkv", ".mp4"};
                if(std::any_of(video_exts.begin(), video_exts.end(), [&meta](const std::string_view ext) { return meta.rfind(ext) != std::string::npos; })) {
                    res += "<figure><video style=\"max-width: 100%;\" preload=\"none\" controls><source src=\"" + meta + "\">Your browser does not support the video element.</video><figcaption>Video: "+text+"</figcaption></figure>";
                    continue;
                }

                // Youtube video embed
                std::array<std::string_view, 3> youtube_url_prefixes = {"https://youtube.com/watch?v=", "https://youtu.be/", "https://www.youtube.com/watch?v="};
                std::string youtube_id;
                std::string timecode;
                auto it = std::find_if(youtube_url_prefixes.begin(), youtube_url_prefixes.end(), [&meta](const std::string_view prefix) { return meta.find(prefix) == 0; });
                if(it != youtube_url_prefixes.end()) {
                    size_t param_pos = meta.find_first_of("?&");
                    size_t id_len = param_pos != std::string::npos ? param_pos-it->size() : std::string::npos;
                    youtube_id = meta.substr(it->size(), id_len);
                    if(param_pos != std::string::npos) {
                        size_t t_pos = meta.find("t=", param_pos);
                        if(t_pos != std::string::npos) {
                            size_t t_end = meta.find('&', t_pos);
                            size_t t_len = t_end != std::string::npos ? t_end-t_pos-2 : std::string::npos;
                            timecode = meta.substr(t_pos+2, t_len);
                        }
                    }
                }
                if(!youtube_id.empty()) {
                    auto embed_url = "https://www.youtube.com/embed/"+youtube_id;
                    if(!timecode.empty())
                        embed_url += "?start="+timecode;
                    auto iframe = "<iframe width=\"560\" height=\"315\" src=\""+embed_url+"\" frameborder=\"0\" allow=\"accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture\" allowfullscreen></iframe>";
                    res += "<figure><div class=\"ytwrapper_outer\"><div class=\"ytwrapper\">"
                        + iframe + "</div></div><figcaption>Youtube video: <a href=\""+meta+"\" target=\"_blank\">"+text+"</a></figcaption></figure>";
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
            if(extended_mode) {
                // detect tables and render as table
                if(node.meta == "markdown" || node.meta == "md" || node.meta.empty()) {
                    // Detect if the block contains a Markdown table
                    std::istringstream stream(text);
                    std::string line;
                    std::vector<std::vector<std::string>> table;
                    bool is_table = true;
                    while (std::getline(stream, line)) {
                        std::istringstream line_stream(line);
                        std::string cell;
                        std::vector<std::string> row;
                        while (std::getline(line_stream, cell, '|')) {
                            cell = trim(cell, " \t");
                            row.push_back(renderPlainText(cell));
                        }
                        if (!row.empty()) {
                            table.push_back(row);
                        } else {
                            is_table = false;
                            break;
                        }
                    }
                    if(table.size() <= 3) {
                        is_table = false;
                    }
                    if(table.size() > 1) {
                        const auto& delim_row = table[1];
                        for (const auto& cell : delim_row) {
                            if (cell.find_first_not_of(" \t-:=") != std::string::npos) {
                                is_table = false;
                                break;
                            }
                        }
                    }
                    if (is_table) {
                        table.erase(table.begin() + 1);
                        res += "<table>\n";
                        for (const auto& row : table) {
                            res += "<tr>";
                            for (const auto& cell : row) {
                                res += "<td>" + cell + "</td>";
                            }
                            res += "</tr>\n";
                        }
                        res += "</table>\n";
                        continue;
                    }
                }
            }
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
        res += "</blockquote>\n";
    else if(last_is_list)
        res += "</ul>\n";
    return {res, HttpViewData::htmlTranslate(title)};
}
