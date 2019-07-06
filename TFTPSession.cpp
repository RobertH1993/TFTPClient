#include "TFTPSession.hpp"
#include <exception>
#include <fstream>
#include <vector>
#include <boost/filesystem.hpp>

const unsigned short TFTP_OPCODE_RRQ = 1;
const unsigned short TFTP_OPCODE_WRQ = 2;
const unsigned short TFTP_OPCODE_DATA = 3;
const unsigned short TFTP_OPCODE_ACK = 4;
const unsigned short TFTP_OPCODE_ERR = 5;

const std::size_t TFTP_BLOCK_SIZE = 512;
const std::size_t TFTP_DATA_HEADER_SIZE = 4;


TFTPSession::TFTPSession(boost::asio::io_service& aIoService, std::string aHost) : 
      io_service(aIoService), sock(io_service), host(aHost)
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
  sock.receive_from(boost::asio::buffer(recv_buffer), server_endpoint);
  
  
  send_file_data(local_file);
  return true;
}

//Private
bool TFTPSession::send_RQ_packet(const std::string& filename, bool is_write_request)
{
  std::vector<unsigned char> header_buffer;
  
  //Choose the right opcode
  unsigned short network_order_opcode = 0;
  if(is_write_request){
    network_order_opcode = boost::asio::detail::socket_ops::host_to_network_short(TFTP_OPCODE_WRQ);
  }else{
    network_order_opcode = boost::asio::detail::socket_ops::host_to_network_short(TFTP_OPCODE_RRQ);
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
    return false;
  }
  
  //Init send buffer
  std::array<unsigned char, (TFTP_DATA_HEADER_SIZE + TFTP_BLOCK_SIZE)> data_packet;
  unsigned short network_order_opcode = boost::asio::detail::socket_ops::host_to_network_short(TFTP_OPCODE_DATA);
  data_packet.at(0) = static_cast<char>(network_order_opcode);
  data_packet.at(1) = static_cast<char>(network_order_opcode >> 8);
  
  //init recv buffer
  std::array<unsigned char, 128> rx;
  
  
  //Send data to server
  unsigned short current_block = 1;
  while(!local_fp.eof()){
    //Set current block
    unsigned short network_order_current_block = boost::asio::detail::socket_ops::host_to_network_short(current_block);
    data_packet.at(2) = static_cast<char>(network_order_current_block);
    data_packet.at(3) = static_cast<char>(network_order_current_block >> 8);
    
    //Read data into data_buffer, recast because STL uses chars...
    local_fp.read(reinterpret_cast<char*>(&data_packet[TFTP_DATA_HEADER_SIZE]), TFTP_BLOCK_SIZE);
    
    //Send the data out
    sock.send_to(boost::asio::buffer(data_packet, TFTP_DATA_HEADER_SIZE + local_fp.gcount()), server_endpoint);
    
    //wait for ack
    sock.receive_from(boost::asio::buffer(rx), server_endpoint);
    
    current_block++;
  }
  
  return true;
}
