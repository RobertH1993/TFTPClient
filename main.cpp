#include "TFTPSession.hpp"
#include <iostream>

int main(int argc, char **argv) {
    boost::asio::io_service io_service;
    TFTPSession local(io_service, "127.0.0.2");
    local.write_file("test.txt", "test.txt");
    io_service.run();
    
    std::cout << "done!" << std::endl;
    return 0;
}
