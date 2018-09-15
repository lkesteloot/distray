
#include "util.hpp"

int send_message(nng_socket sock, const google::protobuf::Message &request) {
    // Serialize to bytes.
    size_t size = request.ByteSize();
    void *buf = nng_alloc(size);
    // XXX check result code (a bool, undocumented).
    request.SerializeToArray(buf, size);

    // Send.
    return nng_send(sock, buf, size, NNG_FLAG_ALLOC);
}

int receive_message(nng_socket sock, google::protobuf::Message &response) {
    void *buf;
    size_t size;

    // Receive.
    int status = nng_recv(sock, &buf, &size, NNG_FLAG_ALLOC);
    if (status != 0) {
        return status;
    }

    // Decode. XXX check result code (a bool, undocumented).
    response.ParseFromArray(buf, size);
    nng_free(buf, size);

    return 0;
}

// Finds a parameter of the form "%d" or "%0Nd" (where N is a positive integer)
// and returns the begin (inclusive) and end (exclusive) index into the string.
// The width is zero in the "%d" case or N in the "%0Nd" case. Returns whether
// a parameter was found. The begin, end, and width parameters may be destroyed
// even if the return value is false.
static bool find_pathname_parameter(const std::string &pathname, int &begin, int &end, int &width) {
    const char *s = pathname.c_str();
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

bool pathname_has_parameter(const std::string &pathname) {
    int begin, end, width;

    return find_pathname_parameter(pathname, begin, end, width);
}

// Substitute a parameter ("%d" or "%0Nd") into the pathname.
std::string substitute_pathname_parameter(const std::string &pathname, int value) {
    return "not_done";
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
