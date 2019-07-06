#include "TFTPSession.hpp"
#include <exception>
#include <fstream>
#include <vector>
#include <cstring>

#include <boost/bind.hpp>

#include <iostream> //DEBUG


#if defined(_WIN32)
//htons/noths
//setsockopt
#include "Winsock2.h"
#else
//htons/noths
#include <arpa/inet.h>
//setsockopt
#include <sys/types.h>
#include <sys/socket.h>
#endif

const std::uint16_t TFTP_OPCODE_RRQ = 1;
const std::uint16_t TFTP_OPCODE_WRQ = 2;
const std::uint16_t TFTP_OPCODE_DATA = 3;
const std::uint16_t TFTP_OPCODE_ACK = 4;
const std::uint16_t TFTP_OPCODE_ERR = 5;

const std::size_t TFTP_BLOCK_SIZE = 512;
const std::size_t TFTP_DATA_HEADER_SIZE = 4;


TFTPSession::TFTPSession(boost::asio::io_service& aIoService, std::string aHost) : 
      io_service(aIoService), sock(io_service), host(aHost), status_of_last_operation(true)
{
  //Resolve server
  boost::asio::ip::udp::resolver resolver(io_service);
  boost::asio::ip::udp::resolver::query query(
                                              boost::asio::ip::udp::v4(),
                                              host,
                                              "tftp");
  server_endpoint = *resolver.resolve(query);
  
  //Open socket
  sock.open(boost::asio::ip::udp::v4());
  
}

//Public
bool TFTPSession::write_file(const std::string& local_file, const std::string& remote_file)
{   
  //Send write request
  send_RQ_packet(remote_file, true);
  if(get_opcode_from_rx_buffer() != TFTP_OPCODE_ACK) return false;
  
  return send_file_data(local_file);
}

//Public
bool TFTPSession::read_file(const std::string& remote_file, const std::string& local_file)
{
  //Send read request
  send_RQ_packet(remote_file, false);
  return get_file_data(local_file);
}


//Private
bool TFTPSession::send_RQ_packet(const std::string& filename, bool is_write_request)
{
  std::vector<unsigned char> header_buffer;
  
  //Choose the right opcode
  std::uint16_t network_order_opcode = 0;
  if(is_write_request){
    network_order_opcode = htons(TFTP_OPCODE_WRQ);
  }else{
    network_order_opcode = htons(TFTP_OPCODE_RRQ);
  }
  header_buffer.push_back(static_cast<char>(network_order_opcode));
  header_buffer.push_back(static_cast<char>(network_order_opcode >> 8));
  
                     
  //Copy the string into the buffer
  std::copy(filename.begin(), filename.end(), std::back_inserter(header_buffer));
  header_buffer.push_back('\0');
  
  //Copy the mode into the buffer
  std::string mode = "octet";
  std::copy(mode.begin(), mode.end(), std::back_inserter(header_buffer));
  header_buffer.push_back('\0');
  
  //Send the header out to the server
  std::size_t bytes_send = sock.send_to(boost::asio::buffer(header_buffer), server_endpoint);
  return(bytes_send == header_buffer.size());
}


bool TFTPSession::send_file_data(const std::string& local_file)
{
  //Open file
  std::ifstream local_fp(local_file, std::ios::binary);
  if(!local_fp.is_open()){
    throw std::invalid_argument(std::string("Cant open local file: ") + local_file + ", Does it exists?");
  }
  
  //Init send buffer
  std::array<unsigned char, (TFTP_DATA_HEADER_SIZE + TFTP_BLOCK_SIZE)> data_packet;
  std::uint16_t network_order_opcode = htons(TFTP_OPCODE_DATA);
  data_packet[0] = static_cast<char>(network_order_opcode);
  data_packet[1] = static_cast<char>(network_order_opcode >> 8);
  
  //Send data to server
  std::uint16_t current_block = 1;
  while(!local_fp.eof()){
    //Set current block
    std::uint16_t network_order_current_block = htons(current_block);
    data_packet[2] = static_cast<char>(network_order_current_block);
    data_packet[3] = static_cast<char>(network_order_current_block >> 8);
    
    //Read data into data_buffer, recast because STL uses chars...
    local_fp.read(reinterpret_cast<char*>(&data_packet[TFTP_DATA_HEADER_SIZE]), TFTP_BLOCK_SIZE);
    
    //Send the data out
    sock.send_to(boost::asio::buffer(data_packet, TFTP_DATA_HEADER_SIZE + local_fp.gcount()), server_endpoint);
    
    //wait for ack
    sock.receive_from(boost::asio::buffer(rx_buffer), server_endpoint);
    if(get_opcode_from_rx_buffer() != TFTP_OPCODE_ACK) return false;
    
    current_block++;
  }
  
  return true;
}

bool TFTPSession::get_file_data(const std::string& local_file)
{
  //Open output file
  std::ofstream local_fp(local_file, std::ios::binary);
  if(!local_fp.is_open()){
    throw std::invalid_argument(std::string("Cant open local output file: ") + local_file + ", do we have permissions?");
  }
  
  std::size_t bytes_received = 0;
  std::uint16_t current_block = 1;
  std::array<unsigned char, 4> ack_packet;
  do {
    //Receive the next packet
    bytes_received = sock.receive_from(boost::asio::buffer(rx_buffer), server_endpoint);
    if(get_opcode_from_rx_buffer() != TFTP_OPCODE_DATA) return false;
    
    //Write bytes to file
    local_fp.write(reinterpret_cast<char*>(&rx_buffer[TFTP_DATA_HEADER_SIZE]), bytes_received - TFTP_DATA_HEADER_SIZE);
    
    //Send ack to server
    std::uint16_t network_order_opcode = htons(TFTP_OPCODE_ACK);
    std::memcpy(&ack_packet[0], &network_order_opcode, 2);
    std::uint16_t network_order_current_block = htons(current_block);
    std::memcpy(&ack_packet[2], &network_order_current_block, 2);
    sock.send_to(boost::asio::buffer(ack_packet, 4), server_endpoint);
    current_block++;
    
  } while(bytes_received == (TFTP_DATA_HEADER_SIZE + TFTP_BLOCK_SIZE));
  
  return true;
}


//Private
std::uint16_t TFTPSession::get_opcode_from_rx_buffer()
{
  std::uint16_t opcode = 0;
  std::memcpy(&opcode, &rx_buffer[0], 2);
  return ntohs(opcode);
}

