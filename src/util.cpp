
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
