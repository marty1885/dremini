#pragma once

#include <string>
#include <vector>
#include <string>

namespace dremini
{
struct GeminiASTNode
{
    std::string orig_text;
    std::string text;
    std::string type;
    std::string meta;
};

std::vector<GeminiASTNode> parseGemini(const std::string_view sv);
}
