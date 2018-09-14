#ifndef UTIL_HPP
#define UTIL_HPP

#include <nng/nng.h>

#include "Drp.pb.h"

// Send a protobuf message over a socket, returning any nng error code.
int send_message(nng_socket sock, const google::protobuf::Message &request);

// Receive a protobuf message over a socket, returning any nng error code.
int receive_message(nng_socket sock, google::protobuf::Message &response);

#endif // UTIL_HPP
