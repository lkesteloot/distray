
#include <cstring>

#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>

#include "controller.hpp"

static void fatal(const char *func, int rv)
{
    fprintf(stderr, "%s: %s\n", func, nng_strerror(rv));
    exit(1);
}

int start_controller(const Parameters &parameters) {
    nng_socket sock;
    int rv;
    size_t sz;
    char *buf = NULL;
    uint8_t cmd[6];

    strcpy((char *) cmd, "hello");

    if ((rv = nng_req0_open(&sock)) != 0) {
        fatal("nng_socket", rv);
    }
    if ((rv = nng_listen(sock, "tcp://127.0.0.1:1120", NULL, 0)) != 0) {
        fatal("nng_listen", rv);
    }
    if ((rv = nng_send(sock, cmd, sizeof(cmd), 0)) != 0) {
        fatal("nng_send", rv);
    }
    if ((rv = nng_recv(sock, &buf, &sz, NNG_FLAG_ALLOC)) != 0) {
        fatal("nng_recv", rv);
    }

    printf("Reply: <%s>\n", buf);

    // XXX This assumes that buf is ASCIIZ (zero terminated).
    nng_free(buf, sz);
    nng_close(sock);

    return 0;
}
