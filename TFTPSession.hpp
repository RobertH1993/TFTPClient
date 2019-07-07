#ifndef _TFTP_HPP
#define _TFTP_HPP

#include <string>
#include <array>
#include <boost/asio.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <fstream>
#include <cstdint>

class TFTPSession{
public:
  TFTPSession(boost::asio::io_service& aIoService, std::string aHost);
  TFTPSession(const TFTPSession& obj) = delete;
  
  /**
   * @brief Start sending a file to the remote end
   * If this function returns false an extended error message can be requested
   */
  void write_file_async(const std::string& local_file, const std::string& remote_file);
  
  /**
   * @brief Start receiving a file from the remote end
   * If this function returns false an extended error message can be requested
   */
  void read_file_async(const std::string& remote_file, const std::string& local_file);
  
  
private:
  boost::asio::io_service& io_service;
  boost::asio::ip::udp::socket sock;
  std::string host;
  boost::asio::ip::udp::endpoint server_endpoint;
  std::array<unsigned char, 516> rx_buffer;
  
  //File transmission states
  //Used for sending a local file out to the server
  std::ifstream input_file;
  std::uint16_t input_file_current_block;
  std::array<unsigned char, 516> input_data_packet;
  
  //Used for receiving a remote file
  std::ofstream output_file;
  std::uint16_t output_file_current_block;
  std::size_t output_file_received_bytes;
  std::array<unsigned char, 4>output_file_ack_packet;
  
  //Deadlines, timeouts and retransmissions
  boost::asio::deadline_timer retransmission_timer;
  std::uint8_t number_of_retransmissions;
  void handle_RWQ_response_received(std::string local_file);
  void send_RQ_retransmission(std::string remote_file, bool is_write_request);
  
  void handle_RWQ_ACK_received(std::string local_file);
  void send_RWQ_retransmission();
  
  void handle_RRW_data_received(std::string local_file, std::size_t bytes_transfered);
  void send_ack_retransmission();


  //Read file and send data block by block to server
  bool send_file_data(const std::string& local_file);
  
  //Start receiving a file from the server
  bool get_file_data(const std::string& local_file, std::size_t bytes_transfered);
  
  //Send write or read request
  bool send_RQ_packet(const std::string& filename, bool is_write_request);

  //Get the opcode from the received data
  std::uint16_t get_opcode_from_rx_buffer();
  
};

#endif
