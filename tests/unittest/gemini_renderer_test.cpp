#include <drogon/drogon_test.h>
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
}
