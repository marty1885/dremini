#include <dremini/Titan.hpp>

#include <drogon/HttpAppFramework.h>
#include <drogon/utils/Utilities.h>

#include <charconv>
#include <cctype>
#include <limits>
#include <map>
#include <mutex>

namespace dremini
{
namespace
{
bool hasValidPercentEncoding(std::string_view value) noexcept
{
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        if (value[index] != '%') continue;
        if (index + 2 >= value.size() ||
            !std::isxdigit(static_cast<unsigned char>(value[index + 1])) ||
            !std::isxdigit(static_cast<unsigned char>(value[index + 2])))
            return false;
        index += 2;
    }
    return true;
}

std::optional<std::string> decodeParameter(std::string_view value)
{
    if (!hasValidPercentEncoding(value)) return std::nullopt;
    const auto decoded = drogon::utils::urlDecode(value);
    for (const auto character : decoded)
        if (character == '\0' || character == '\r' || character == '\n')
            return std::nullopt;
    return decoded;
}

TitanParseResult failure(TitanParseError error)
{
    return {{}, error};
}

// Titan parameters form a semicolon-delimited suffix of the URI path. MIME
// values may contain raw '/', so the first semicolon—not path segmentation—
// separates the resource from parameters.
std::optional<std::map<std::string, std::string>> parseParameters(
    std::string_view suffix,
    TitanParseError &error)
{
    std::map<std::string, std::string> parameters;
    while (!suffix.empty())
    {
        if (suffix.front() != ';')
        {
            error = TitanParseError::InvalidParameter;
            return std::nullopt;
        }
        suffix.remove_prefix(1);
        const auto delimiter = suffix.find(';');
        const auto parameter = suffix.substr(0, delimiter);
        if (parameter.empty())
        {
            error = TitanParseError::InvalidParameter;
            return std::nullopt;
        }
        const auto equals = parameter.find('=');
        const auto key = parameter.substr(0, equals);
        if (key.empty())
        {
            error = TitanParseError::InvalidParameter;
            return std::nullopt;
        }
        const auto value = equals == std::string_view::npos
                               ? std::string_view{}
                               : parameter.substr(equals + 1);
        if (!parameters.emplace(key, value).second)
        {
            error = TitanParseError::DuplicateParameter;
            return std::nullopt;
        }
        if (delimiter == std::string_view::npos) break;
        suffix.remove_prefix(delimiter);
    }
    return parameters;
}
}  // namespace

TitanParseResult parseTitanRequest(std::string_view path, std::size_t maxUploadBytes)
{
    const auto parameters = path.find(';');
    if (parameters == std::string_view::npos) return failure(TitanParseError::MissingParameters);

    TitanRequest request;
    request.resourcePath = std::string{path.substr(0, parameters)};
    if (request.resourcePath.empty() || request.resourcePath.front() != '/')
        return failure(TitanParseError::InvalidParameter);

    TitanParseError parameterError = TitanParseError::InvalidParameter;
    const auto parsed = parseParameters(path.substr(parameters), parameterError);
    if (!parsed) return failure(parameterError);
    const auto &parameterMap = *parsed;
    for (const auto &[key, value] : parameterMap)
    {
        if (key != "edit" && key != "size" && key != "mime" && key != "token")
            return failure(TitanParseError::InvalidParameter);
    }

    const auto edit = parameterMap.find("edit");
    if (edit != parameterMap.end())
    {
        if (!edit->second.empty()) return failure(TitanParseError::InvalidParameter);
        if (parameterMap.size() != 1) return failure(TitanParseError::ConflictingOperation);
        request.operation = TitanOperation::Edit;
        return {std::move(request), TitanParseError::None};
    }

    const auto size = parameterMap.find("size");
    if (size == parameterMap.end()) return failure(TitanParseError::MissingSize);
    const auto sizeResult = std::from_chars(size->second.data(), size->second.data() + size->second.size(), request.size);
    if (sizeResult.ec != std::errc{} || sizeResult.ptr != size->second.data() + size->second.size())
        return failure(TitanParseError::InvalidSize);
    if (const auto mime = parameterMap.find("mime"); mime != parameterMap.end())
    {
        const auto decoded = decodeParameter(mime->second);
        if (!decoded || decoded->empty()) return failure(TitanParseError::InvalidParameter);
        request.mimeType = *decoded;
    }
    if (const auto token = parameterMap.find("token"); token != parameterMap.end())
    {
        const auto decoded = decodeParameter(token->second);
        if (!decoded) return failure(TitanParseError::InvalidParameter);
        request.token = *decoded;
    }
    if (request.size > maxUploadBytes) return failure(TitanParseError::SizeExceeded);
    return {std::move(request), TitanParseError::None};
}

std::string_view titanParseErrorMessage(TitanParseError error) noexcept
{
    switch (error)
    {
    case TitanParseError::MissingParameters: return "Titan request is missing parameters";
    case TitanParseError::InvalidParameter: return "Titan request has an invalid parameter";
    case TitanParseError::DuplicateParameter: return "Titan request repeats a parameter";
    case TitanParseError::MissingSize: return "Titan request is missing size";
    case TitanParseError::InvalidSize: return "Titan request has an invalid size";
    case TitanParseError::SizeExceeded: return "Titan upload exceeds the server limit";
    case TitanParseError::ConflictingOperation: return "Titan edit request has upload parameters";
    case TitanParseError::None: break;
    }
    return "Invalid Titan request";
}

void installTitanRoutingAdvice()
{
    static std::once_flag installed;
    std::call_once(installed, [] {
        drogon::app().registerPreRoutingAdvice([](const drogon::HttpRequestPtr& request) {
            if (request->method() != drogon::Get ||
                !request->getAttributes()->get<bool>(kTitanEditRequestAttribute))
                return;
            const auto parsed = parseTitanRequest(request->path(), std::numeric_limits<std::size_t>::max());
            if (!parsed || parsed.request->operation != TitanOperation::Edit)
            {
                LOG_ERROR << "Dremini received an invalid internal Titan edit request";
                return;
            }
            request->setPath(parsed.request->resourcePath);
            request->setPathEncode(false);
            request->addHeader("titan-edit", "true");
            LOG_DEBUG << "Titan edit normalized to resource=" << parsed.request->resourcePath;
        });
    });
}
}  // namespace dremini
