#include "AsioServer.h"

#include <iostream>

int main()
{
    try
    {
        AsioServer server(8080, 2, 2);
        server.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
    }

    std::cout << "Hello, World!" << std::endl;
}