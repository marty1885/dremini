# Dremini

Highly concurrent and multi-threaded gemini client and server library for the [Drogon web application framework](https://github.com/drogonframework/drogon)

**This library is in early development. API may clange without notice**

## Dependencies

* [Drogon](https://github.com/drogonframework/drogon) >= 1.7.3 (or 1.7.2 with MIME patch)

## Usage

### Client

Async-callback API

There's several ways to use the Gemini client. The most general one is by the async-callback API. The supplied callback function will be invoked once the cliend recived a response from the server.

```c++
dremini::sendRequest("gemini://gemini.circumlunar.space/"
    , [](ReqResult result, const HttpResponsePtr& resp) {
        if(result != ReqResult::Ok)
        {
            LOG_ERROR << "Failed to send request";
            return;
        }

        LOG_INFO << "Body size: " << resp->body().size();
    });
```

C++ Coroutines

If C++ coroutines (requires MSVC 19.25, GCC 11 or better) is avaliable to you. You can also use the more stright forward coroutine API.

```c++
try
{
    auto resp = co_await dremini::sendRequestCoro("gemini://gemini.circumlunar.space/");
    LOG_INFO << "Body size: " << resp->body().size();
} 
catch(...)
{
    LOG_ERROR << "Failed to send request";
}
```

### Server

The `dremini::GeminiServer` plugin that parses and forwards Gemini requests as HTTP Get requests.

Thus, first enable the Gemini server plugin. And tell the server to serve gemini files (if you are also using it as a file server):

```json
// config.json
{
    "app":{
        "document_root": "./",
        "file_types": [
            "gmi"
        ],
        "use_implicit_page": true,
        "implicit_page": "index.gmi",
        "home_page": "index.gmi",
        "mime": {
            "text/gemini": ["gmi", "gemini"]
        }
    },
    "plugins": [
        {
            "name": "dremini::GeminiServerPlugin",
            "config": {
                "listeners": [
                    {
                        "key": "key.pem",
                        "cert": "cert.pem",
                        "ip": "127.0.0.1"
                    }
                ],
                "numThread": 3
            }
        }
    ]
}

```

Then let's code up ca basic request handler:

```c++
app().registerHandler("/hi", [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback)
{
    auto resp = HttpResponse::newHttpResponse();
    resp->setBody("# Hello Gemini\nHello!\n");
    resp->setContentTypeCodeAndCustomString(CT_CUSTOM, "text/gemini");
    callback(resp);
});
```

Finally, the handler can be triggered by using a Gemini client

```
> gmni gemini://localhost/hi -j once
# Hello Gemini
Hello
>
```


## Mapping from Gemini to HTTP

Drogon is a HTTP framework. Dremini reuses a large portion of it's original functionality to provide easy Gemini server programming.
We try hard to keep to Gemini sementic in HTTP.

### Status codes

Like HTTP, Gemini uses status codes for servers to indicate the status of the response. Gemini status codes must have 2 digits.
Thus a range between 0 and 99. Dremini maps common HTTP status codes into Gemini ones automatically. And converts the rest to
an apporiprate category. Here's a list of conversions

|HTTP|Gemini|
|----|------|
|200 |20    |
|400 |59    |
|404 |51    |
|429 |44    |
|502 |43    |
|504 |43    |

If the returned HTTP status is not listed above. The Gemini status is simply the HTTP status divied by 100. **Exception being 4
xx and 5xx**. They 4xx is mapped as 50 and 5xx is mapped as 40. Since there's no HTTP status code lesser than 100 exists. Any status code in that range is treads as a Gemini native code. Which does not
go through any translation.

HttpResponses from a Gemini client automaticallyi have a `gemini-status` header that stores the Gemini status code before mapping. And a `meta` header to store the meta.

### Serving Gemini files

Drogon by design is a HTTP server. Thus is has no idea about Gemini files and convension. The following config tells Drogon the following things. Which are required to properly serve gemini pages

* Sets the page root folder to `/path/to/the/files`
* Serve `*.gmi` files
    * Add more types if needed
* `*.gmi` and `*.gemini` files have the MIME type `text/gemini`
* When asked to serve a directory, Try serving `index.gmi`
* The homepage is `/index.gmi`

```json
{
    "app":{
        "document_root": "/path/to/the/files",
        "file_types": [
            "gmi",
            "gemini"
        ],
        "use_implicit_page": true,
        "implicit_page": "index.gmi",
        "home_page": "index.gmi",
        "mime": {
            "text/gemini": ["gmi", "gemini"]
        }
    }
}
```

### Gemini query

Gemini URLs supports a query parameter using the `?` symbol. For example, `gemini://localhost/search?Hello` has a query of "Hello". Dremini adds a `query` parameter to the HttpRequest when a query is detected.

### Detecting Gemini requests

Dremini adds a `protocol` header to the proxyed HTTP request to singnal it's comming from a Gemini request. Whom's value is always "gemini"

### Translating Gemini into HTML for HTTP requests

Dermini supports translating Gemini into HTML automatically! Add `"translate_to_html": true` into plugin config. This makes Dremini translate `text/gemini` contents into HTML when a regular browser requests content

## Limitations

Dremini library does not verify server and client certificates yet. Nor client certs are supported.

