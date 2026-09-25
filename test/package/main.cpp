// Builds against an installed SocketsHpp and runs a loopback HTTP round trip.
#include <SocketsHpp/http/client/http_client.h>
#include <SocketsHpp/http/server/http_server.h>

#include <cstdio>

using namespace SocketsHpp::http::server;
using namespace SocketsHpp::http::client;

int main()
{
    HttpServer server("package-test", 0);
    HttpRequestCallback hello{[](const HttpRequest&, HttpResponse& res) -> int {
        res.body = "hello";
        return 200;
    }};
    server["/hello"] = hello;
    server.start();

    HttpClient client;
    HttpClientResponse response;
    bool ok = client.get("http://127.0.0.1:" + std::to_string(server.getListeningPort()) + "/hello",
                         response);
    server.stop();

    if (!ok || response.code != 200 || response.body != "hello")
    {
        std::fprintf(stderr, "package consumer: unexpected response (ok=%d code=%d)\n", ok, response.code);
        return 1;
    }
    std::puts("package consumer: OK");
    return 0;
}
