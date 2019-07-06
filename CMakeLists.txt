cmake_minimum_required(VERSION 3.0)

project(tftpclient)

add_executable(tftpclient main.cpp TFTPSession.cpp)

TARGET_LINK_LIBRARIES( tftpclient
pthread
boost_filesystem
)

install(TARGETS tftpclient RUNTIME DESTINATION bin)
