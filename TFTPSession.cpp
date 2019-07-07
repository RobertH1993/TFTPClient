#include "TFTPSession.hpp"
#include <exception>
#include <vector>
#include <cstring>

#include <boost/bind.hpp>

#include <iostream> //DEBUG


#if defined(_WIN32)
//htons/noths
#include "Winsock2.h"
#else
//htons/noths
#include <arpa/inet.h>
#endif

const std::uint16_t TFTP_OPCODE_RRQ = 1;
const std::uint16_t TFTP_OPCODE_WRQ = 2;
const std::uint16_t TFTP_OPCODE_DATA = 3;
const std::uint16_t TFTP_OPCODE_ACK = 4;
const std::uint16_t TFTP_OPCODE_ERR = 5;

const std::size_t TFTP_BLOCK_SIZE = 512;
const std::size_t TFTP_DATA_HEADER_SIZE = 4;


TFTPSession::TFTPSession(boost::asio::io_service& aIoService, std::string aHost) : 
      io_service(aIoService), sock(io_service), host(aHost), retransmission_timer(io_service), number_of_retransmissions(0),
      input_file_current_block(1), output_file_current_block(1)
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
void TFTPSession::write_file_async(const std::string& local_file, const std::string& remote_file)
{   
  //Send write request
  send_RQ_packet(remote_file, true);
  sock.async_receive_from(
                            boost::asio::buffer(rx_buffer),
                            server_endpoint,
                            boost::bind(
                              &TFTPSession::handle_RWQ_response_received,
                              this,
                              local_file
                            )
                          );
  
  //Set the retransmission_timer
  retransmission_timer.expires_from_now(boost::posix_time::millisec(200));
  retransmission_timer.async_wait(boost::bind(&TFTPSession::send_RQ_retransmission,
                                              this,
                                              local_file,
                                              true)
                                  );
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
  if(!sock.is_open()) return false;
  
  //Open file, if it isnt already
  if(!input_file.is_open()){
    input_file.open(local_file, std::ios::binary);
    if(!input_file.is_open()){
      throw std::invalid_argument(std::string("Cant open local file: ") + local_file + ", Does it exists?");
    }
  }
  
  //Init send buffer
  std::uint16_t network_order_opcode = htons(TFTP_OPCODE_DATA);
  input_data_packet[0] = static_cast<char>(network_order_opcode);
  input_data_packet[1] = static_cast<char>(network_order_opcode >> 8);
  
  if(!input_file.eof()){ //Send block of data
    //Set current block
    std::uint16_t network_order_current_block = htons(input_file_current_block);
    input_data_packet[2] = static_cast<char>(network_order_current_block);
    input_data_packet[3] = static_cast<char>(network_order_current_block >> 8);
    
    //Read data into data_buffer, recast because STL uses chars...
    input_file.read(reinterpret_cast<char*>(&input_data_packet[TFTP_DATA_HEADER_SIZE]), TFTP_BLOCK_SIZE);
    
    //Send the data out
    sock.send_to(boost::asio::buffer(input_data_packet, TFTP_DATA_HEADER_SIZE + input_file.gcount()), server_endpoint);
    
    //wait for ack (async)
    sock.async_receive_from(boost::asio::buffer(rx_buffer),
                            server_endpoint,
                            boost::bind(
                                        &TFTPSession::handle_RWQ_ACK_received,
                                        this,
                                        local_file)
                            );
    
    input_file_current_block++;
    
    //Set the retransmission timer
    retransmission_timer.expires_from_now(boost::posix_time::millisec(300));
    retransmission_timer.async_wait(boost::bind(&TFTPSession::send_RWQ_retransmission,
                                                this)
                                    );
  }else{
    std::cout << "Done transfering" << std::endl;
    input_file.close();
    input_file_current_block = 1;
    retransmission_timer.expires_at(boost::posix_time::pos_infin);
    retransmission_timer.cancel();
    sock.close();
    return true;
  }
  
  return false;
}

bool TFTPSession::get_file_data(const std::string& local_file)
{
  //Check if socket is still open
  
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
//Gets called when we get a response from the first RWQ packet that is send
void TFTPSession::handle_RWQ_response_received(std::string local_file){
  //Turn off retransmission timer
  retransmission_timer.expires_at(boost::posix_time::pos_infin);
  number_of_retransmissions = 0;
  
  //Async receive might have ended because the socket was closed by the retransmission deadline
  if(!sock.is_open()) return;
  
  //Send the file data
  send_file_data(local_file);
}

//Private
//Gets called when we send an RQ message but didnt receive a response from the server
void TFTPSession::send_RQ_retransmission(std::string remote_file, bool is_write_request)
{
  //We can hit this while the timer just has been increase, so check if we really timed out
  if(retransmission_timer.expires_at() <= boost::asio::deadline_timer::traits_type::now()){
    if(number_of_retransmissions >= 8){
      sock.close();
      retransmission_timer.expires_at(boost::posix_time::pos_infin);
      return;
    }
    
    //Send retransmission
    send_RQ_packet(remote_file, is_write_request);
    number_of_retransmissions++;
    
    //Reset timer
    retransmission_timer.expires_from_now(boost::posix_time::millisec(300));
  }
  retransmission_timer.async_wait(boost::bind(
                                              &TFTPSession::send_RQ_retransmission,
                                              this,
                                              remote_file,
                                              is_write_request
                                         )
                                  );
}

//Private
//Gets called when we send an data packet (RWQ) but didnt receive a response from the server
void TFTPSession::send_RWQ_retransmission()
{
  if(retransmission_timer.expires_at() <= boost::asio::deadline_timer::traits_type::now()){
    if(number_of_retransmissions >= 8){
      sock.close();
      retransmission_timer.expires_at(boost::posix_time::pos_infin);
      return;
    }
    
    //Send retransmission
    sock.send_to(boost::asio::buffer(input_data_packet, TFTP_DATA_HEADER_SIZE + input_file.gcount()), server_endpoint);
    number_of_retransmissions++;
    
    //Reset timer
    retransmission_timer.expires_from_now(boost::posix_time::millisec(300));
  }
  retransmission_timer.async_wait(boost::bind(
                                              &TFTPSession::send_RWQ_retransmission,
                                              this)
                                  );
}



//Private
//Gets called after we send a data packet to the server
void TFTPSession::handle_RWQ_ACK_received(std::string local_file)
{
  //Check if we got a ACK, if not close connection
  if(get_opcode_from_rx_buffer() != TFTP_OPCODE_ACK){
    sock.close();
    return;
  }
  
  //Send next packet
  send_file_data(local_file);
}



//Private
std::uint16_t TFTPSession::get_opcode_from_rx_buffer()
{
  std::uint16_t opcode = 0;
  std::memcpy(&opcode, &rx_buffer[0], 2);
  return ntohs(opcode);
}

