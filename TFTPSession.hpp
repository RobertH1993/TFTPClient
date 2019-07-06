#ifndef _TFTP_HPP
#define _TFTP_HPP

#include <string>
#include <array>
#include <boost/asio.hpp>
#include <boost/asio/placeholders.hpp>
#include <cstdint>

class TFTPSession{
public:
  TFTPSession(boost::asio::io_service& aIoService, std::string aHost);
  TFTPSession(const TFTPSession& obj) = delete;
  
  /**
   * @brief Start sending a file to the remote end
   * If this function returns false an extended error message can be requested
   */
  bool write_file(const std::string& local_file, const std::string& remote_file);
  
  /**
   * @brief Start receiving a file from the remote end
   * If this function returns false an extended error message can be requested
   */
  bool read_file(const std::string& remote_file, const std::string& local_file);
  
  
private:
  boost::asio::io_service& io_service;
  boost::asio::ip::udp::socket sock;
  std::string host;
  boost::asio::ip::udp::endpoint server_endpoint;
  std::array<unsigned char, 516> rx_buffer;
  
  //Status flag
  bool status_of_last_operation;

  //Read file and send data block by block to server
  bool send_file_data(const std::string& local_file);
  
  //Start receiving a file from the server
  bool get_file_data(const std::string& local_file);
  
  //Send write or read request
  bool send_RQ_packet(const std::string& filename, bool is_write_request);

  //Get the opcode from the received data
  std::uint16_t get_opcode_from_rx_buffer();
  
};

#endif
