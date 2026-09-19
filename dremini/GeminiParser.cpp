#include "GeminiParser.hpp"
#include <algorithm>

static bool startsWith(const std::string_view sv, const std::string_view target)
{
    if(sv.size() < target.size())
        return false;
#if 0
    auto start_idx = sv.find_first_not_of(' ');
    if(start_idx+target.size() > sv.size())
        return false;

    for(size_t i=0;i<target.size();i++) {
        if(sv[start_idx+i] != target[i])
            return false;
    }
    return true;
#else
    return sv.substr(0, target.size()) == target;
#endif
}

namespace dremini
{
static std::string_view ltrim(std::string_view s)
{
    s.remove_prefix(std::distance(s.cbegin(), std::find_if(s.cbegin(), s.cend(),
         [](int c) {return !std::isspace(c);})));

    return s;
}

static std::string_view rtrim(std::string_view s)
{
    s.remove_suffix(std::distance(s.crbegin(), std::find_if(s.crbegin(), s.crend(),
        [](int c) {return !std::isspace(c);})));

    return s;
}

static std::string_view trim(std::string_view s)
{
    return ltrim(rtrim(s));
}

std::vector<GeminiASTNode> parseGemini(const std::string_view str)
{
    std::vector<GeminiASTNode> nodes;
    nodes.reserve(std::count(str.begin(), str.end(), '\n') + (str.empty() ? 0 : 1));
    size_t last_pos = 0;
    bool in_preformatted_text = false;
    size_t preformatted_text_start = 0;
    std::string preformatted_text_meta = "";
    for(size_t i=0;i<str.size()+1;i++) {
        if((i == str.size() && i != 0 && str[i-1] != '\n')|| (i < str.size() && str[i] == '\n')) {
            if(in_preformatted_text == false) {
                std::string_view line(str.data()+last_pos, i-last_pos);
                auto crlf = line.find_last_not_of("\r\n");
                if(crlf != std::string_view::npos)
                    line = std::string_view(line.data(), crlf+1);
                else
                    line = std::string_view(line.data(), 0);

                GeminiASTNode node;
                node.orig_text = line;
                if(startsWith(line, "=>")) {
                    std::string_view sv = line.substr(2);
                    std::string_view link;
                    std::string_view text;
                    auto link_start = sv.find_first_not_of(" \t");
                    if(link_start != std::string_view::npos) {
                        sv = sv.substr(link_start);
                        auto link_end = sv.find_first_of(" \t");
                        link = sv.substr(0, link_end);
                        if(link_end != std::string_view::npos) {
                            sv = sv.substr(link_end);
                            auto text_start = sv.find_first_not_of(" \t");
                            if(text_start != std::string_view::npos) {
                                sv = sv.substr(text_start);
                                auto text_end = sv.find_last_not_of(" \t");
                                text = sv.substr(0, text_end == std::string_view::npos? text_end : text_end+1);
                            }
                        }
                    }
                    node.text = text;
                    node.meta = link;
                    node.type = "link";
                }
                else if(startsWith(line, "###")) {
                    node.text = trim(line.substr(3));
                    node.type = "heading3";
                }
                else if(startsWith(line, "##")) {
                    node.text = trim(line.substr(2));
                    node.type = "heading2";
                }
                else if(startsWith(line, "#")) {
                    node.text = trim(line.substr(1));
                    node.type = "heading1";
                }
                else if(startsWith(line, "* ")) {
                    node.text = ltrim(line.substr(1));
                    node.type = "list";
                }
                else if(startsWith(line, ">")) {
                    node.text = line.substr(1);
                    node.type = "quote";
                }
                else if(startsWith(line, "```")) {
                    preformatted_text_meta = line.substr(3);
                    in_preformatted_text = true;
                    preformatted_text_start = last_pos;
                    last_pos = i+1;
                    continue;
                }
                else {
                    node.text = line;
                    node.type = "text";
                }
                last_pos = i+1;

                nodes.emplace_back(std::move(node));
            }
            else {
                std::string_view line(str.data()+last_pos, i-last_pos);
                auto crlf = line.find_last_not_of("\r\n");
                if(crlf != std::string_view::npos)
                    line = std::string_view(line.data(), crlf+1);
                last_pos = i+1;

                if(startsWith(line, "```")) {
                    std::string_view preformatted_text(str.data()+preformatted_text_start, i-preformatted_text_start);
                    GeminiASTNode node;
                    node.orig_text = preformatted_text;

                    std::string content;
                    const auto first_newline = preformatted_text.find('\n');
                    if(first_newline != std::string_view::npos) {
                        auto content_start = first_newline + 1;
                        content.reserve(preformatted_text.size() - content_start);
                        while(content_start < preformatted_text.size()) {
                            const auto next_newline = preformatted_text.find('\n', content_start);
                            const auto content_end = next_newline == std::string_view::npos
                                                          ? preformatted_text.size()
                                                          : next_newline;
                            const auto line = preformatted_text.substr(
                                content_start, content_end - content_start);
                            if(!line.find("```") == 0)
                                content.append(line).push_back('\n');
                            if(next_newline == std::string_view::npos)
                                break;
                            content_start = next_newline + 1;
                        }
                    }
                    node.text = content;
                    node.meta = preformatted_text_meta;
                    node.type = "preformatted_text";
                    in_preformatted_text = false;
                    nodes.emplace_back(std::move(node));
                }
            }
        }
    }
    return nodes;
}

}
