
#include <thread>
#include <unistd.h>

#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>

#include "worker.hpp"
#include "Drp.pb.h"

static void fatal(const char *func, int rv)
{
    fprintf(stderr, "%s: %s\n", func, nng_strerror(rv));
    exit(1);
}

int start_worker(const Parameters &parameters) {
    nng_socket sock;
    int rv;
    Drp::Request request;
    Drp::Response response;

    if ((rv = nng_rep0_open(&sock)) != 0) {
        fatal("nng_rep0_open", rv);
    }
    if ((rv = nng_dial(sock, parameters.m_url.c_str(), NULL, 0)) != 0) {
        fatal("nng_dial", rv);
    }
    for (;;) {
        char *buf = NULL;
        size_t size;
        if ((rv = nng_recv(sock, &buf, &size, NNG_FLAG_ALLOC)) != 0) {
            fatal("nng_recv", rv);
        }
        request.ParseFromArray(buf, size);
        nng_free(buf, size);

        std::cout << "Received " << request.request_type() << "\n";

        response.set_request_type(request.request_type());
        Drp::WelcomeResponse *welcome_response = response.mutable_welcome_response();
        char hostname[128];
        rv = gethostname(hostname, sizeof(hostname));
        if (rv == -1) {
            strcpy(hostname, "unknown");
        }
        welcome_response->set_hostname(hostname);
        welcome_response->set_core_count(std::thread::hardware_concurrency());
        size = response.ByteSize();
        buf = (char *) nng_alloc(size);
        response.SerializeToArray(buf, size);

        rv = nng_send(sock, buf, size, NNG_FLAG_ALLOC);
        if (rv != 0) {
            fatal("nng_send", rv);
        }
    }

    return 0;
}
