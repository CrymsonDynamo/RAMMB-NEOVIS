#include <curl/curl.h>
#include <iostream>
#include "app.hpp"

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    App app;
    if (!app.init(1280, 720)) {
        std::cerr << "Startup failed.\n";
        curl_global_cleanup();
        return 1;
    }

    app.run();

    curl_global_cleanup();
    return 0;
}
