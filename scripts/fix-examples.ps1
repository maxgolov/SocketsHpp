# Fix examples 06-09 namespace issues
$ErrorActionPreference = "Stop"

Write-Host "Fixing example 06..."
(Get-Content "examples\06-proxy-aware\main.cpp" -Raw) `
    -replace 'using namespace SOCKETSHPP_NS::tcp;', 'using namespace SOCKETSHPP_NS::net::common;' `
    | Set-Content "examples\06-proxy-aware\main.cpp"

Write-Host "Fixing example 07..."
(Get-Content "examples\07-authentication\main.cpp" -Raw) `
    -replace 'using namespace SOCKETSHPP_NS::tcp;', 'using namespace SOCKETSHPP_NS::net::common;' `
    -replace 'AuthenticationMiddleware<HttpRequest, HttpResponse>', 'SOCKETSHPP_NS::http::server::AuthenticationMiddleware<HttpRequest, HttpResponse>' `
    -replace 'BearerTokenAuth<HttpRequest>', 'SOCKETSHPP_NS::http::server::BearerTokenAuth<HttpRequest>' `
    -replace 'ApiKeyAuth<HttpRequest>', 'SOCKETSHPP_NS::http::server::ApiKeyAuth<HttpRequest>' `
    -replace 'BasicAuth<HttpRequest>', 'SOCKETSHPP_NS::http::server::BasicAuth<HttpRequest>' `
    -replace '([^:])AuthResult([^:])', '$1SOCKETSHPP_NS::http::server::AuthResult$2' `
    | Set-Content "examples\07-authentication\main.cpp"

Write-Host "Fixing example 08..."
(Get-Content "examples\08-compression\main.cpp" -Raw) `
    -replace 'using namespace SOCKETSHPP_NS::tcp;', 'using namespace SOCKETSHPP_NS::net::common;' `
    -replace 'using namespace SOCKETSHPP_NS::http::server::compression;', '' `
    -replace '([^:])CompressionMiddleware([^:])', '$1SOCKETSHPP_NS::http::server::compression::CompressionMiddleware$2' `
    -replace '([^:])registerSimpleCompression', '$1SOCKETSHPP_NS::http::server::compression::registerSimpleCompression' `
    -replace '([^:])registerWindowsCompression', '$1SOCKETSHPP_NS::http::server::compression::registerWindowsCompression' `
    -replace 'CompressionRegistry::instance', 'SOCKETSHPP_NS::http::server::compression::CompressionRegistry::instance' `
    | Set-Content "examples\08-compression\main.cpp"

Write-Host "Fixing example 09..."
(Get-Content "examples\09-full-featured\main.cpp" -Raw) `
    -replace 'using namespace SOCKETSHPP_NS::tcp;', 'using namespace SOCKETSHPP_NS::net::common;' `
    -replace 'using namespace SOCKETSHPP_NS::http::server::compression;', '' `
    -replace '([^:])TrustProxyConfig([^:])', '$1SOCKETSHPP_NS::http::server::TrustProxyConfig$2' `
    -replace 'TrustMode::TrustSpecific', 'SOCKETSHPP_NS::http::server::TrustProxyConfig::TrustMode::TrustSpecific' `
    -replace 'AuthenticationMiddleware<HttpRequest, HttpResponse>', 'SOCKETSHPP_NS::http::server::AuthenticationMiddleware<HttpRequest, HttpResponse>' `
    -replace 'BearerTokenAuth<HttpRequest>', 'SOCKETSHPP_NS::http::server::BearerTokenAuth<HttpRequest>' `
    -replace 'ApiKeyAuth<HttpRequest>', 'SOCKETSHPP_NS::http::server::ApiKeyAuth<HttpRequest>' `
    -replace 'BasicAuth<HttpRequest>', 'SOCKETSHPP_NS::http::server::BasicAuth<HttpRequest>' `
    -replace '([^:])AuthResult([^:])', '$1SOCKETSHPP_NS::http::server::AuthResult$2' `
    -replace '([^:])CompressionMiddleware([^:])', '$1SOCKETSHPP_NS::http::server::compression::CompressionMiddleware$2' `
    -replace '([^:])registerSimpleCompression', '$1SOCKETSHPP_NS::http::server::compression::registerSimpleCompression' `
    -replace '([^:])registerWindowsCompression', '$1SOCKETSHPP_NS::http::server::compression::registerWindowsCompression' `
    -replace 'SOCKETSHPP_NS::http::server::ProxyAwareHelpers::getProtocol\(req.headers, req.remoteAddr, config\)', 'SOCKETSHPP_NS::http::server::ProxyAwareHelpers::getProtocol(req.headers, remoteAddr, config)' `
    -replace 'SOCKETSHPP_NS::http::server::ProxyAwareHelpers::getClientIP\(req.headers, req.remoteAddr, config\)', 'SOCKETSHPP_NS::http::server::ProxyAwareHelpers::getClientIP(req.headers, remoteAddr, config)' `
    -replace 'SOCKETSHPP_NS::http::server::ProxyAwareHelpers::isSecure\(req.headers, req.remoteAddr, config\)', 'SOCKETSHPP_NS::http::server::ProxyAwareHelpers::isSecure(req.headers, remoteAddr, config)' `
    | Set-Content "examples\09-full-featured\main.cpp"

Write-Host "All examples fixed!"
