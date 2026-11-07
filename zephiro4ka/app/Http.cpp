#include "Http.h"

#include "Platform.h"

#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace {
    struct Handle {
        HINTERNET h = nullptr;
        ~Handle() { if (h) WinHttpCloseHandle(h); }
    };

    int ReadResponse(HINTERNET request, const Http::OnData& onData) {
        if (!WinHttpReceiveResponse(request, nullptr)) return 0;
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        std::string chunk;
        for (DWORD available = 0; WinHttpQueryDataAvailable(request, &available) && available > 0;) {
            chunk.resize(available);
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) break;
            chunk.resize(read);
            if (!onData(chunk)) break;
        }
        return static_cast<int>(status);
    }
}

int Http::Local(unsigned short port, const wchar_t* method, const wchar_t* path, const std::string& body, int timeoutMs, const OnData& onData) {
    Handle session{ WinHttpOpen(L"Anya/3.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
    if (!session.h) return 0;
    WinHttpSetTimeouts(session.h, 1000, 1000, 5000, timeoutMs);
    Handle connection{ WinHttpConnect(session.h, L"127.0.0.1", port, 0) };
    if (!connection.h) return 0;
    Handle request{ WinHttpOpenRequest(connection.h, method, path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0) };
    if (!request.h) return 0;

    const wchar_t* headers = L"Content-Type: application/json; charset=utf-8\r\n";
    void* data = body.empty() ? nullptr : const_cast<char*>(body.data());
    const DWORD size = static_cast<DWORD>(body.size());
    if (!WinHttpSendRequest(request.h, headers, static_cast<DWORD>(-1), data, size, size, 0)) return 0;
    return ReadResponse(request.h, onData);
}

std::string Http::HttpsPost(const wchar_t* host, const std::wstring& path, const std::string& body, const std::string& contentType, int timeoutMs) {
    Handle session{ WinHttpOpen(L"Anya/3.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
    if (!session.h) return {};
    WinHttpSetTimeouts(session.h, 6000, 6000, timeoutMs, timeoutMs);
    Handle connection{ WinHttpConnect(session.h, host, INTERNET_DEFAULT_HTTPS_PORT, 0) };
    if (!connection.h) return {};
    Handle request{ WinHttpOpenRequest(connection.h, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) };
    if (!request.h) return {};
    const std::wstring headers = L"Content-Type: " + std::wstring(contentType.begin(), contentType.end()) + L"\r\n";
    void* data = body.empty() ? nullptr : const_cast<char*>(body.data());
    const DWORD size = static_cast<DWORD>(body.size());
    if (!WinHttpSendRequest(request.h, headers.c_str(), static_cast<DWORD>(-1), data, size, size, 0)) return {};
    std::string response;
    ReadResponse(request.h, [&](const std::string& chunk) { response += chunk; return true; });
    return response;
}

std::string Http::HttpsGet(const wchar_t* host, const std::wstring& path, int timeoutMs) {
    Handle session{ WinHttpOpen(L"Anya/3.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0) };
    if (!session.h) return {};
    WinHttpSetTimeouts(session.h, 4000, 4000, 6000, timeoutMs);
    Handle connection{ WinHttpConnect(session.h, host, INTERNET_DEFAULT_HTTPS_PORT, 0) };
    if (!connection.h) return {};
    Handle request{ WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) };
    if (!request.h) return {};
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return {};
    std::string body;
    ReadResponse(request.h, [&](const std::string& chunk) { body += chunk; return true; });
    return body;
}
