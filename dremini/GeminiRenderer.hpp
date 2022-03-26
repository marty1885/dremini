#pragma once

#include <string>
#include <string_view>

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
 */
std::pair<std::string, std::string> render2Html(const std::string_view gemini_src, bool extended_mode = false);

}
