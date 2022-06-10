#pragma once

#include <string>
#include <string_view>

#include "GeminiParser.hpp"

namespace dremini
{

/**
 * @brief Render Gemini gemtext to HTML.
 * @param gmi_source The Gemini source.
 * @param extended_mode Whether to use extended mode.
 * @return The rendered HTML.
 * 
 * Extended mode: Adds support for non gemini standard, Markdown like syntax in the following list
 * - "-" or "=" and is at least 3 chars => horizontal line:
 *    ---
 *    ===
 * - `` pairs treated as inline code block:
 *   `code`
 * - * and _ pairs treated as italic:
 *   *some text* or _some text_
 * - ** and __ pairs treated as bold:
 *  **some text** or __some text__
 */
std::pair<std::string, std::string> render2Html(const std::string_view gemini_src, bool extended_mode = false);
std::pair<std::string, std::string> render2Html(const std::vector<GeminiASTNode>& nodes, bool extended_mode = false);

}
