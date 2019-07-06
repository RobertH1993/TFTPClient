#include "TFTPSession.hpp"
#include <iostream>

int main(int argc, char **argv) {
    boost::asio::io_service io_service;
    TFTPSession local(io_service, "127.0.0.1");
    
    if(local.write_file("ls", "ls")){
      std::cout << "[+] Succesfully send data to the server!" << std::endl;
    }
    //if(local.read_file("ls", "ls.back")){
    //  std::cout << "[+] Received file back from server!" << std::endl;
    //}
    
    io_service.run();
    
    std::cout << "done!" << std::endl;
    return 0;
}
