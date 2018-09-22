
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "util.hpp"

bool Endpoint::resolve(bool is_server, const std::string &default_hostname, int default_port) {
    return parse_and_lookup_endpoint(m_endpoint, is_server,
            default_hostname, default_port, m_sockaddr);
}

int send_message(int sock_fd, const google::protobuf::Message &request) {
    // Serialize to bytes.
    uint32_t size = request.ByteSize();
    uint32_t full_size = size + sizeof(size);
    uint8_t *buf = new uint8_t[full_size];
    *((uint32_t *) buf) = htonl(size);
    request.SerializeToArray(buf + sizeof(size), size);

    // Send.
    int result = send(sock_fd, buf, full_size, 0);

    delete[] buf;

    return result;
}

int receive_message(int sock_fd, google::protobuf::Message &response) {
    uint32_t size;

    // Receive length.
    int status = recv(sock_fd, &size, sizeof(size), MSG_WAITALL);
    if (status == -1) {
        perror("recv1");
        return status;
    } else if (status == 0) {
        // Other side closed connection. This error isn't technically
        // correct, but it'll be handled the right way higher up the stack.
        errno = ECONNRESET;
        return -1;
    } else if (status != sizeof(size)) {
        // Shouldn't happen, we specify MSG_WAITALL.
        std::cout << "Received " << status << " bytes instead of " << sizeof(size) << "\n";
        exit(-1);
    }

    size = ntohl(size);

    uint8_t *buf = new uint8_t[size];

    // Receive.
    status = recv(sock_fd, buf, size, MSG_WAITALL);
    if (status == -1) {
        perror("recv2");
    } else if (status == 0) {
        // Other side closed connection. This error isn't technically
        // correct, but it'll be handled the right way higher up the stack.
        errno = ECONNRESET;
        return -1;
    } else if (status != size) {
        // Shouldn't happen, we specify MSG_WAITALL.
        std::cout << "Received " << status << " bytes instead of " << size << "\n";
        exit(-1);
    } else {
        // Decode. The result code is undocumented, we're guessing here.
        bool success = response.ParseFromArray(buf, size);
        if (!success) {
            std::cerr << "Can't decode protobuf.\n";
            exit(-1);
        }
    }

    delete[] buf;

    return status;
}

// Finds a parameter of the form "%d" or "%0Nd" (where N is a positive integer)
// and returns the begin (inclusive) and end (exclusive) index into the string.
// The width is zero in the "%d" case or N in the "%0Nd" case. Returns whether
// a parameter was found. The begin, end, and width parameters may be destroyed
// even if the return value is false.
static bool find_parameter(const std::string &str, int &begin, int &end, int &width) {
    const char *s = str.c_str();
    const char *p = s;

    while (true) {
        // Find the first %.
        p = strchr(p, '%');
        if (p == nullptr) {
            return false;
        }

        // Record start.
        begin = p - s;

        // Skip %.
        p++;
        if (*p == '0' || *p == 'd') {
            // Parse optional numeric value.
            width = 0;
            while (*p >= '0' && *p <= '9') {
                width = width*10 + (*p - '0');
                p++;
            }
            if (*p == 'd') {
                // Found parameter.
                end = p - s + 1;
                return true;
            }
        }
    }
}

bool string_has_parameter(const std::string &str) {
    int begin, end, width;

    return find_parameter(str, begin, end, width);
}

// Substitute a parameter ("%d" or "%0Nd") into the string.
std::string substitute_parameter(const std::string &str, int value) {
    int begin, end, width;

    // See if we have any parameters.
    if (value >= 0 && find_parameter(str, begin, end, width)) {
        // Convert value to a string, the hard C++ way.
        std::stringstream value_stream;
        if (width == 0) {
            value_stream << value;
        } else {
            value_stream << std::setfill('0') << std::setw(width) << value;
        }
        std::string value_str = value_stream.str();

        // Recurse to do the rest of the string.
        return str.substr(0, begin) + value_str + substitute_parameter(str.substr(end), value);
    } else {
        // No parameters or negative value, return string unchanged.
        return str;
    }
}

bool is_pathname_local(const std::string &pathname) {
    // Can't be absolute.
    if (pathname.length() > 0 && pathname[0] == '/') {
        return false;
    }

    // Can't have parent directories.
    if (pathname.find("..") != std::string::npos) {
        return false;
    }

    return true;
}

std::string read_file(const std::string &pathname) {
    // This is reasonably efficient.
    std::ifstream f(pathname);
    if (!f) {
        throw std::runtime_error("cannot open file: " + pathname);
    }

    std::stringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
}

bool write_file(const std::string &pathname, const std::string &content) {
    // This is reasonably efficient.
    std::ofstream f(pathname);
    if (!f) {
        return false;
    }

    f << content;

    return true;
}

int create_server_socket(const Endpoint &endpoint) {
    // Create socket.
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        return -1;
    }

    // Make sure we can re-bind to this socket immediately shutting down last time.
    int opt = 1;
    int result = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (result == -1) {
        perror("setsockopt (SO_REUSEADDR)");
        return -1;
    }
    result = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    if (result == -1) {
        perror("setsockopt (SO_REUSEPORT)");
        return -1;
    }

    // Bind to our socket.
    result = bind(sock_fd, (struct sockaddr *) &endpoint.m_sockaddr, sizeof(endpoint.m_sockaddr));
    if (result == -1) {
        perror("bind");
        return -1;
    }

    // Listen for new connections.
    result = listen(sock_fd, 10);
    if (result == -1) {
        perror("listen");
        return -1;
    }

    return sock_fd;
}

int create_client_socket(const Endpoint &endpoint) {
    // Create socket.
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("socket");
        return -1;
    }

    int result = connect(sock_fd, (struct sockaddr *) &endpoint.m_sockaddr,
            sizeof(endpoint.m_sockaddr));
    if (result == -1) {
        perror("connect");
        return -1;
    }

    return sock_fd;
}

// Parses a non-negative decimal integer. Returns -1 if the string is not
// entirely made of a non-negative integer.
static int parse_integer(const char *s) {
    // Quick check if the start is okay. Also eliminates empty strings and
    // negative numbers.
    if (*s < '0' || *s > '9') {
        return -1;
    }

    char *end;
    int value = strtol(s, &end, 10);

    // See if we reached the end of the string.
    return *end == '\0' ? value : -1;
}

bool parse_endpoint(const std::string &endpoint,
        const std::string &default_hostname, int default_port,
        std::string &hostname, int &port) {

    // Set defaults.
    hostname = default_hostname;
    port = default_port;

    // Split the endpoint.
    const char *endpoint_s = endpoint.c_str();
    const char *c = strchr(endpoint_s, ':');
    if (c == nullptr) {
        // No colon. Could be a hostname or a port. First try to parse it as an
        // integer.
        int possible_port = parse_integer(endpoint_s);
        if (possible_port >= 0) {
            // Valid integer.
            port = possible_port;
        } else {
            // Invalid integer. Assume it's a hostname.
            if (!endpoint.empty()) {
                hostname = endpoint;
            }
        }
    } else {
        // Split at the colon.
        hostname = std::string(endpoint_s, c - endpoint_s);

        // Parse the port.
        if (c[1] != '\0') {
            // Only set the port if it's non-empty. Otherwise keep default.
            int possible_port = parse_integer(c + 1);
            if (possible_port >= 0) {
                // Valid port.
                port = possible_port;
            } else {
                // Bad port, fail.
                std::cerr << "Bad port " << (c + 1) << " in endpoint " << endpoint << "\n";
                return false;
            }
        }
    }

    return true;
}

bool do_dns_lookup(const std::string &hostname, int port, bool is_server,
        struct sockaddr_in &sockaddr) {

    // Convert port to string the dumb C++ way.
    std::stringstream port_stream;
    port_stream << port;
    std::string port_str = port_stream.str();

    // Configure the hints for the DNS lookup.
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = (is_server ? AI_PASSIVE : 0);

    // Pass in null for an empty string, which either means "any address" when
    // serving or "loopback" when connecting.
    const char *hostname_s = hostname.empty() ? nullptr : hostname.c_str();

    // Do the actual lookup.
    struct addrinfo *res;
    int error = getaddrinfo(hostname_s, port_str.c_str(), &hints, &res);
    if (error != 0) {
        std::cerr << hostname_s << ": " << gai_strerror(error) << "\n";
        return false;
    }

    if (res == nullptr) {
        // Failed to look up hostname. I don't know if this ever happens.
        std::cerr << "do_dns_lookup: Unexpected error in getaddrinfo() response for " <<
            hostname_s << "\n";
        return false;
    }

    if (res->ai_addrlen != sizeof(sockaddr_in)) {
        std::cerr << "do_dns_lookup: address structure is wrong size (" << res->ai_addrlen << ")\n";
        return false;
    }

    // Just take the first option.
    sockaddr = *((struct sockaddr_in *) res->ai_addr);

    freeaddrinfo(res);

    return true;
}

bool parse_and_lookup_endpoint(const std::string &endpoint,
        bool is_server,
        const std::string &default_hostname,
        int default_port,
        struct sockaddr_in &sockaddr) {

    std::string hostname;
    int port;

    return
        parse_endpoint(endpoint, default_hostname, default_port, hostname, port) &&
        do_dns_lookup(hostname, port, is_server, sockaddr);
}
