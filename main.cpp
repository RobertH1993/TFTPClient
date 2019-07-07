#include "TFTPSession.hpp"
#include <iostream>

int main(int argc, char **argv) {
    boost::asio::io_service io_service;
    TFTPSession local(io_service, "127.0.0.1");
    
    local.write_file_async("ls", "ls");
    //if(local.read_file("ls", "ls.back")){
    //  std::cout << "[+] Received file back from server!" << std::endl;
    //}
    
    io_service.run();
    
    std::cout << "done!" << std::endl;
    return 0;
}
