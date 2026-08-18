#include <dremini/Titan.hpp>

#include <drogon/drogon_test.h>

using namespace dremini;

DROGON_TEST(TitanRequestParsing)
{
    const auto upload = parseTitanRequest(
        "/mail/draft;token=opaque%20value;mime=text%2Fgemini;size=12", 64);
    REQUIRE(upload);
    CHECK(upload.request->operation == TitanOperation::Upload);
    CHECK(upload.request->resourcePath == "/mail/draft");
    CHECK(upload.request->mimeType == "text/gemini");
    REQUIRE(upload.request->token);
    CHECK(*upload.request->token == "opaque value");
    CHECK(upload.request->size == 12);

    // Parameters are unordered. Lagrange sends this raw MIME value before
    // size, and its slash must not be considered part of the resource path.
    const auto mimeFirst = parseTitanRequest(
        "/mail/draft;mime=text/plain;size=12", 64);
    REQUIRE(mimeFirst);
    CHECK(mimeFirst.request->resourcePath == "/mail/draft");
    CHECK(mimeFirst.request->mimeType == "text/plain");
    CHECK(mimeFirst.request->size == 12);

    const auto defaultMime = parseTitanRequest("/mail/draft;size=0", 64);
    REQUIRE(defaultMime);
    CHECK(defaultMime.request->mimeType == "text/gemini");
    CHECK(defaultMime.request->size == 0);

    const auto edit = parseTitanRequest("/mail/draft;edit", 64);
    REQUIRE(edit);
    CHECK(edit.request->operation == TitanOperation::Edit);
    CHECK(edit.request->resourcePath == "/mail/draft");

    CHECK(parseTitanRequest("/mail/draft", 64).error == TitanParseError::MissingParameters);
    CHECK(parseTitanRequest("/mail/draft;mime=text%2Fgemini", 64).error == TitanParseError::MissingSize);
    CHECK(parseTitanRequest("/mail/draft;size=12;size=13", 64).error == TitanParseError::DuplicateParameter);
    CHECK(parseTitanRequest("/mail/draft;size=65", 64).error == TitanParseError::SizeExceeded);
    CHECK(parseTitanRequest("/mail/draft;edit;size=0", 64).error == TitanParseError::ConflictingOperation);
    CHECK(parseTitanRequest("/mail/draft;size=1;unknown=value", 64).error == TitanParseError::InvalidParameter);
}
