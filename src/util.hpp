#ifndef UTIL_HPP
#define UTIL_HPP

#include <google/protobuf/message.h>

int send_message(int sock_fd, const google::protobuf::Message &request);
int receive_message(int sock_fd, google::protobuf::Message &response);

// Whether a string includes a parameter ("%d" or "%0Nd").
bool string_has_parameter(const std::string &str);

// Substitute a parameter ("%d" or "%0Nd") into the string. Does no
// expansion if the value is negative.
std::string substitute_parameter(const std::string &str, int value);

// Check whether a pathname is local (relative and can't escape the current directory).
bool is_pathname_local(const std::string &pathname);

// Read a file into a string. Throws an std::runtime_error exception if there's an I/O error.
std::string read_file(const std::string &pathname);

// Write a file from a string. Returns whether successful.
bool write_file(const std::string &pathname, const std::string &content);

// Create a server socket. Returns -1 (and sets errno) on failure, otherwise returns 0.
int create_server_socket(int port);

// Create a client socket. Returns -1 (and sets errno) on failure, otherwise returns 0.
int create_client_socket(int port);

#endif // UTIL_HPP
