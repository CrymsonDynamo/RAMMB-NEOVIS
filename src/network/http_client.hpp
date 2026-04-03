#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Synchronous HTTP client (single connection).
// Phase 2 will extend this to async multi-handle for parallel tile downloads.
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // Download URL into memory buffer. Returns true on HTTP 200.
    bool get(const std::string& url, std::vector<uint8_t>& out);

    const std::string& last_error() const { return m_error; }

private:
    struct Impl;
    Impl*       m_impl{nullptr};
    std::string m_error;
};
