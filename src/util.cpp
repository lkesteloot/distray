
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <sys/socket.h>

#include "util.hpp"

int send_message(int sockfd, const google::protobuf::Message &request) {
    // Serialize to bytes.
    uint32_t size = request.ByteSize();
    uint32_t full_size = size + sizeof(size);
    uint8_t *buf = new uint8_t[full_size];
    *((uint32_t *) buf) = htonl(size);
    request.SerializeToArray(buf + sizeof(size), size);

    // Send.
    int result = send(sockfd, buf, full_size, 0);

    delete[] buf;

    return result;
}

int receive_message(int sockfd, google::protobuf::Message &response) {
    uint32_t size;

    // Receive length.
    int status = recv(sockfd, &size, sizeof(size), MSG_WAITALL);
    if (status == -1) {
        perror("recv1");
        return status;
    } else if (status != sizeof(size)) {
        // XXX handle 0, which means other side closed connection.
        std::cout << "Received " << status << " bytes instead of " << sizeof(size) << "\n";
        exit(-1);
    }

    size = ntohl(size);

    uint8_t *buf = new uint8_t[size];

    // Receive.
    status = recv(sockfd, buf, size, MSG_WAITALL);
    if (status == -1) {
        perror("recv2");
    } else if (status != size) {
        // XXX handle 0, which means other side closed connection.
        std::cout << "Received " << status << " bytes instead of " << size << "\n";
        exit(-1);
    } else {
        // Decode. XXX check result code (a bool, undocumented).
        response.ParseFromArray(buf, size);
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
