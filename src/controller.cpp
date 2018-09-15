
#include <cstring>

#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>

#include "controller.hpp"
#include "Drp.pb.h"
#include "util.hpp"

static void fatal(const char *func, int rv)
{
    fprintf(stderr, "%s: %s\n", func, nng_strerror(rv));
    exit(1);
}

int start_controller(const Parameters &parameters) {
    nng_socket sock;
    int rv;

    if ((rv = nng_req0_open(&sock)) != 0) {
        fatal("nng_socket", rv);
    }
    if ((rv = nng_listen(sock, parameters.m_url.c_str(), NULL, 0)) != 0) {
        fatal("nng_listen", rv);
    }

    // Send welcome message.
    {
        Drp::Request request;
        Drp::Response response;

        request.set_request_type(Drp::WELCOME);
        rv = send_message(sock, request);
        if (rv != 0) {
            fatal("send_message", rv);
        }

        rv = receive_message(sock, response);
        if (rv != 0) {
            fatal("receive_message", rv);
        }

        if (request.request_type() == Drp::WELCOME) {
            std::cout << "hostname: " << response.welcome_response().hostname() <<
                ", cores: " << response.welcome_response().core_count() << "\n";
        } else {
            std::cout << "Got unknown response type " << request.request_type() << "\n";
        }
    }

    // Send execute program message.
    {
        Drp::Request request;
        Drp::Response response;

        request.set_request_type(Drp::EXECUTE);
        Drp::ExecuteRequest *execute_request = request.mutable_execute_request();
        execute_request->set_executable(parameters.m_executable);
        for (std::string argument : parameters.m_arguments) {
            execute_request->add_argument(argument);
        }
        rv = send_message(sock, request);
        if (rv != 0) {
            fatal("send_message", rv);
        }

        rv = receive_message(sock, response);
        if (rv != 0) {
            fatal("receive_message", rv);
        }

        if (request.request_type() == Drp::EXECUTE) {
            std::cout << "status: " << response.execute_response().status() << "\n";
        } else {
            std::cout << "Got unknown response type " << request.request_type() << "\n";
        }
    }

    nng_close(sock);

    return 0;
}
