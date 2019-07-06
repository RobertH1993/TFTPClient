#ifndef __TFTP_HPP
#define __TFTP_HPP

#include <string>
#include <array>
#include <boost/asio.hpp>

class TFTPSession{
public:
  TFTPSession(boost::asio::io_service& aIoService, std::string aHost);
  
  //TODO implement copy constuctor, move enz.
  
  /**
   * @brief Start sending a file to the remote end
   */
  bool write_file(const std::string& local_file, const std::string& remote_file);
  
  /**
   * @brief Start receiving a file from the remote end
   */
  bool read_file(const std::string& remote_file, const std::string& local_file);
  
  
private:
  boost::asio::io_service& io_service;
  boost::asio::ip::udp::socket sock;
  std::string host;
  boost::asio::ip::udp::endpoint server_endpoint;
  std::array<unsigned char, 516> recv_buffer;

  //Read file and send data block by block to server
  bool send_file_data(const std::string& local_file);
  
  //Send write or read request
  bool send_RQ_packet(const std::string& filename, bool is_write_request);
  
};

#endif
