
#include <cstring>

#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>

#include "controller.hpp"
#include "Drp.pb.h"

static void fatal(const char *func, int rv)
{
    fprintf(stderr, "%s: %s\n", func, nng_strerror(rv));
    exit(1);
}

int start_controller(const Parameters &parameters) {
    nng_socket sock;
    int rv;
    char *buf = NULL;
    Drp::Request request;
    Drp::Response response;

    if ((rv = nng_req0_open(&sock)) != 0) {
        fatal("nng_socket", rv);
    }
    if ((rv = nng_listen(sock, "tcp://127.0.0.1:1120", NULL, 0)) != 0) {
        fatal("nng_listen", rv);
    }

    request.set_request_type(Drp::WELCOME);

    size_t size = request.ByteSize();
    buf = (char *) nng_alloc(size);
    request.SerializeToArray(buf, size);
    if ((rv = nng_send(sock, &request, sizeof(request), 0)) != 0) {
        fatal("nng_send", rv);
    }
    nng_free(buf, size);

    if ((rv = nng_recv(sock, &buf, &size, NNG_FLAG_ALLOC)) != 0) {
        fatal("nng_recv", rv);
    }
    response.ParseFromArray(buf, size);
    nng_free(buf, size);

    if (request.request_type() == Drp::WELCOME) {
        std::cout << "hostname: " << response.welcome_response().hostname() <<
            ", cores: " << response.welcome_response().core_count() << "\n";
    } else {
        std::cout << "Got unknown response type " << request.request_type() << "\n";
    }

    nng_close(sock);

    return 0;
}
