#include <drogon/drogon_test.h>
#include <dremini/GeminiClient.hpp>
#include <dremini/GeminiRenderer.hpp>

using namespace dremini;

DROGON_TEST(GeminiRenderer)
{
    CHECK(render2Html("asd").first == "<p>asd</p>\n");
    CHECK(render2Html("asd*asd*").first == "<p>asd*asd*</p>\n");
    CHECK(render2Html("asd**asd**").first == "<p>asd**asd**</p>\n");
    CHECK(render2Html("asd `asd`").first == "<p>asd `asd`</p>\n");
    CHECK(render2Html("asd `*asd*`").first == "<p>asd `*asd*`</p>\n");
    CHECK(render2Html("1*1=1").first == "<p>1*1=1</p>\n");
    CHECK(render2Html("_asd_").first == "<p>_asd_</p>\n");
    CHECK(render2Html("__asd__").first == "<p>__asd__</p>\n");
    CHECK(render2Html("*asd*").first == "<p>*asd*</p>\n");
    CHECK(render2Html("**asd**").first == "<p>**asd**</p>\n");
    CHECK(render2Html("* asd").first == "<ul>\n  <li>asd</li>\n</ul>\n");
    CHECK(render2Html("* *asd*").first == "<ul>\n  <li>*asd*</li>\n</ul>\n");
    CHECK(render2Html("*asd_").first == "<p>*asd_</p>\n");
}

DROGON_TEST(GeminiRendererExtended)
{
    CHECK(render2Html("asd", true).first == "<p>asd</p>\n");
    CHECK(render2Html("asd*asd*", true).first == "<p>asd<i>asd</i></p>\n");
    CHECK(render2Html("asd `asd`", true).first == "<p>asd <code>asd</code></p>\n");
    CHECK(render2Html("asd `*asd*`", true).first == "<p>asd <code>*asd*</code></p>\n");
    CHECK(render2Html("asd**asd**", true).first == "<p>asd<strong>asd</strong></p>\n");
    CHECK(render2Html("1*1=1", true).first == "<p>1*1=1</p>\n");
    CHECK(render2Html("_asd_", true).first == "<p><i>asd</i></p>\n");
    CHECK(render2Html("__asd__", true).first == "<p><strong>asd</strong></p>\n");
    CHECK(render2Html("_**asd**_", true).first == "<p><i><strong>asd</strong></i></p>\n");
    CHECK(render2Html("a**_asd_**", true).first == "<p>a<strong><i>asd</i></strong></p>\n");
    CHECK(render2Html("a**_asd**_", true).first == "<p>a<strong>_asd</strong>_</p>\n");
    CHECK(render2Html("*asd*", true).first == "<p><i>asd</i></p>\n");
    CHECK(render2Html("**asd**", true).first == "<p><strong>asd</strong></p>\n");
    CHECK(render2Html("* asd", true).first == "<ul>\n  <li>asd</li>\n</ul>\n");
    CHECK(render2Html("* *asd*", true).first == "<ul>\n  <li><i>asd</i></li>\n</ul>\n");
    CHECK(render2Html("*asd_", true).first == "<p>*asd_</p>\n");
    CHECK(render2Html("*asd_*", true).first == "<p><i>asd_</i></p>\n");
    CHECK(render2Html("one_two three_four", true).first == "<p>one_two three_four</p>\n");
    CHECK(render2Html("**", true).first == "<p>**</p>\n");
    CHECK(render2Html("__", true).first == "<p>__</p>\n");

    CHECK(render2Html("~~123~~", true).first == "<p><strike>123</strike></p>\n");
    CHECK(render2Html("~~**123**~~", true).first == "<p><strike><strong>123</strong></strike></p>\n");
    CHECK(render2Html("~~**~~", true).first == "<p><strike>**</strike></p>\n");
    CHECK(render2Html("**~~**", true).first == "<p><strong>~~</strong></p>\n");
    CHECK_NOTHROW(render2Html("~~**unclosed bold~~", true));

}

DROGON_TEST(GeminiRendererMarkdownEmptyPreformattedText)
{
    const std::vector<GeminiASTNode> ast{
        {"", "intro", "text", ""},
        {"", "", "preformatted_text", "markdown"}};
    CHECK(render2Markdown(ast) == "intro\n```markdown\n\n```\n");
}

DROGON_TEST(GeminiRendererMarkdownUntaggedPreformattedText)
{
    const std::vector<GeminiASTNode> ast{{"", "code", "preformatted_text", ""}};
    CHECK(render2Markdown(ast) == "```\ncode\n```\n");
}

DROGON_TEST(GeminiRendererEscapesLinkTargets)
{
    const std::vector<GeminiASTNode> active_link{{"", "unsafe", "link", "javascript:alert(1)"}};
    CHECK(render2Html(active_link).first == "<div class=\"link\">unsafe</div>\n");

    const std::vector<GeminiASTNode> quoted_link{{
        "", "safe", "link", "https://example.test/\" onclick=\"alert(1)"}};
    const auto html = render2Html(quoted_link).first;
    CHECK(html.find("href=\"https://example.test/\" onclick") == std::string::npos);
    CHECK(html.find("href=\"https://example.test/&quot;") != std::string::npos);
}

DROGON_TEST(GeminiRendererShowsPreformattedAltText)
{
    const std::vector<GeminiASTNode> preformatted{{
        "", "example", "preformatted_text", "cryptographic material"}};
    CHECK(render2Html(preformatted).first ==
          "<figure class=\"preformatted\"><figcaption>cryptographic material</figcaption>"
          "<pre><code>example</code></pre></figure>\n");

    const std::vector<GeminiASTNode> escaped_alt{{
        "", "example", "preformatted_text", "<untrusted>"}};
    const auto escaped_html = render2Html(escaped_alt).first;
    CHECK(escaped_html.find("<figcaption>&lt;untrusted&gt;</figcaption>") != std::string::npos);
}

DROGON_TEST(GeminiParserHandlesUnclosedPreformattedText)
{
    const auto nodes = parseGemini("``` cpp  \nint main() {}\n");
    REQUIRE(nodes.size() == 1);
    CHECK(nodes[0].type == "preformatted_text");
    CHECK(nodes[0].meta == "cpp");
    CHECK(nodes[0].text == "int main() {}\n");
    CHECK(render2Html(nodes, true).first.find("class=\"language-cpp\"") != std::string::npos);
}

DROGON_TEST(GeminiRendererParsesYoutubeParameters)
{
    const std::vector<GeminiASTNode> link{{"", "video", "link",
                                           "https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=30"}};
    const auto html = render2Html(link, true).first;
    CHECK(html.find("https://www.youtube.com/embed/dQw4w9WgXcQ?start=30") != std::string::npos);
}

DROGON_TEST(GeminiRendererDoesNotMistakeParameterSuffixForYoutubeTimecode)
{
    const std::vector<GeminiASTNode> link{{"", "video", "link",
                                           "https://www.youtube.com/watch?v=dQw4w9WgXcQ&format=1080"}};
    const auto html = render2Html(link, true).first;
    CHECK(html.find("https://www.youtube.com/embed/dQw4w9WgXcQ\"") != std::string::npos);
    CHECK(html.find("?start=") == std::string::npos);
}

DROGON_TEST(GeminiClientRejectsInvalidPorts)
{
    CHECK_NOTHROW(dremini::internal::GeminiClient("gemini://example.test:65535/", nullptr));

    for(const auto& url : {"gemini://example.test:0/", "gemini://example.test:65536/",
                           "gemini://example.test:999999999999999999999/"})
    {
        bool rejected = false;
        try
        {
            dremini::internal::GeminiClient(url, nullptr);
        }
        catch(const std::invalid_argument&)
        {
            rejected = true;
        }
        CHECK(rejected);
    }
}

DROGON_TEST(GeminiRendererBoundsFormattingBacktracking)
{
    std::string input;
    for(size_t i = 0; i < 10000; ++i)
        input += "*x";
    CHECK_NOTHROW(render2Html(input, true));
}
