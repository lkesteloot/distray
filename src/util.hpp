#ifndef UTIL_HPP
#define UTIL_HPP

#include <nng/nng.h>

#include "Drp.pb.h"

// Send a protobuf message over a socket, returning any nng error code.
int send_message(nng_socket sock, const google::protobuf::Message &request);

// Receive a protobuf message over a socket, returning any nng error code.
int receive_message(nng_socket sock, google::protobuf::Message &response);

// Whether a pathname includes a parameter ("%d" or "%0Nd").
bool pathname_has_parameter(const std::string &pathname);

// Substitute a parameter ("%d" or "%0Nd") into the pathname.
std::string substitute_pathname_parameter(const std::string &pathname, int value);

// Check whether a pathname is local (relative and can't escape the current directory).
bool is_pathname_local(const std::string &pathname);

// Read a file into a string. Throws an std::runtime_error exception if there's an I/O error.
std::string read_file(const std::string &pathname);

// Write a file from a string. Returns whether successful.
bool write_file(const std::string &pathname, const std::string &content);

#endif // UTIL_HPP
