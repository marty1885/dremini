#pragma once

#include <string>
#include <string_view>

namespace dremini
{

std::pair<std::string, std::string> render2Html(const std::string_view geminiSrc);

}
