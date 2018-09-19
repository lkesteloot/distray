#ifndef UTIL_HPP
#define UTIL_HPP

#include <netdb.h>
#include <google/protobuf/message.h>

// Represents both an endpoint string (like "example.com:1120") and its
// parsed and looked-up address.
class Endpoint {
public:
    std::string m_endpoint;
    struct sockaddr_in m_sockaddr;

    Endpoint() {
        // Nothing.
    }

    // Returns whether successful.
    bool set(const std::string &endpoint, bool is_server,
            const std::string &default_hostname, int default_port);
};

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
int create_server_socket(const Endpoint &endpoint);

// Create a client socket. Returns -1 (and sets errno) on failure, otherwise returns 0.
int create_client_socket(const Endpoint &endpoint);

// Parse a "hostname:port" string into a hostname and port. Also accepts
// ":port" (blank hostname), "port" (default hostname), "hostname:" (default
// port), and "hostname" (default port). Returns whether successful.
bool parse_endpoint(const std::string &endpoint,
        const std::string &default_hostname, int default_port,
        std::string &hostname, int &port);

// Do a DNS lookup on hostname/port combo. The hostname can be empty to
// mean "any address" (is_server) or "localhost" (not is_server). Returns
// whether successful.
bool do_dns_lookup(const std::string &hostname, int port, bool is_server,
        struct sockaddr_in &sockaddr);

// Combination of parse_endpoint() and do_dns_lookup(). Returns whether
// successful.
bool parse_and_lookup_endpoint(const std::string &endpoint,
        bool is_server,
        const std::string &default_hostname,
        int default_port,
        struct sockaddr_in &sockaddr);

#endif // UTIL_HPP
