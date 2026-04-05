#include "http_client.hpp"
#include <curl/curl.h>
#include <iostream>

struct HttpClient::Impl {
    CURL* handle{nullptr};
};

static size_t write_cb(void* data, size_t size, size_t nmemb, void* user) {
    auto* vec   = static_cast<std::vector<uint8_t>*>(user);
    auto* bytes = static_cast<const uint8_t*>(data);
    size_t total = size * nmemb;
    vec->insert(vec->end(), bytes, bytes + total);
    return total;
}

HttpClient::HttpClient() : m_impl(new Impl) {
    m_impl->handle = curl_easy_init();
    if (!m_impl->handle)
        std::cerr << "[HttpClient] curl_easy_init failed\n";
}

HttpClient::~HttpClient() {
    if (m_impl) {
        if (m_impl->handle) curl_easy_cleanup(m_impl->handle);
        delete m_impl;
    }
}

bool HttpClient::get(const std::string& url, std::vector<uint8_t>& out) {
    CURL* c = m_impl->handle;
    if (!c) { m_error = "No curl handle"; return false; }

    out.clear();
    curl_easy_reset(c);
    curl_easy_setopt(c, CURLOPT_URL,           url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA,     &out);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT,        30L);
    curl_easy_setopt(c, CURLOPT_USERAGENT,      "RAMMB-NEOVIS/0.1");
    // Accept gzip to reduce download size
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");

    CURLcode res = curl_easy_perform(c);
    if (res != CURLE_OK) {
        m_error = curl_easy_strerror(res);
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        m_error = "HTTP " + std::to_string(http_code);
        return false;
    }

    return true;
}
