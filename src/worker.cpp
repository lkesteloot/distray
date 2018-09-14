
#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>

#include "worker.hpp"

static void fatal(const char *func, int rv)
{
    fprintf(stderr, "%s: %s\n", func, nng_strerror(rv));
    exit(1);
}

int start_worker(const Parameters &parameters) {
    nng_socket sock;
    int rv;

    if ((rv = nng_rep0_open(&sock)) != 0) {
        fatal("nng_rep0_open", rv);
    }
    if ((rv = nng_dial(sock, parameters.m_url.c_str(), NULL, 0)) != 0) {
        fatal("nng_dial", rv);
    }
    for (;;) {
        char *buf = NULL;
        size_t sz;
        uint64_t val;
        if ((rv = nng_recv(sock, &buf, &sz, NNG_FLAG_ALLOC)) != 0) {
            fatal("nng_recv", rv);
        }
        printf("Received <%s>\n", buf);

        strcpy(buf, "world");
        rv = nng_send(sock, buf, sz, NNG_FLAG_ALLOC);
        if (rv != 0) {
            fatal("nng_send", rv);
        }

        // Unrecognized command, so toss the buffer.
        // XXX nng_free(buf, sz);
    }

    return 0;
}
