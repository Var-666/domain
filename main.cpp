#include "EpollServer.h"

#include <iostream>

int main() {
    std::cout << "Hello, World!" << std::endl;

    EpollServer server(8080);

    if(!server.init()) {
        std::cerr << "Failed to initialize server." << std::endl;
        return -1;
    }

    std::cout << "Server is running on port 8080..." << std::endl;
    server.loop();
}