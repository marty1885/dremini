#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace dremini
{
inline constexpr char kTitanEditRequestAttribute[] = "dremini.titan.edit";
// The parsed authority comes from the Gemini/Titan request line, rather than
// an HTTP header supplied by a translator client.
inline constexpr char kRequestAuthorityAttribute[] = "dremini.request_authority";

enum class TitanOperation { Upload, Edit };

enum class TitanParseError
{
    None,
    MissingParameters,
    InvalidParameter,
    DuplicateParameter,
    MissingSize,
    InvalidSize,
    SizeExceeded,
    ConflictingOperation,
};

struct TitanRequest
{
    TitanOperation operation = TitanOperation::Upload;
    std::string resourcePath;
    std::string mimeType = "text/gemini";
    std::optional<std::string> token;
    std::size_t size = 0;
};

struct TitanParseResult
{
    std::optional<TitanRequest> request;
    TitanParseError error = TitanParseError::None;

    explicit operator bool() const noexcept { return request.has_value(); }
};

// Parse Titan's semicolon-delimited parameters from an already-separated URI
// path. The query is deliberately not part of this input: Titan parameters
// are a semicolon-delimited suffix before '?'.
TitanParseResult parseTitanRequest(std::string_view path, std::size_t maxUploadBytes);
std::string_view titanParseErrorMessage(TitanParseError error) noexcept;

// Install the one pre-routing transformation required by Titan's optional
// ';edit' request. It is idempotent for Drogon's process-wide application.
void installTitanRoutingAdvice();
}  // namespace dremini
