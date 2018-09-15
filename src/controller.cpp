
#include <cstring>
#include <stdexcept>

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

// Returns whether successful.
static bool copy_in(nng_socket sock, const Parameters &parameters, int frame) {
    for (const FileCopy &fileCopy : parameters.m_in_copies) {
        if ((frame >= 0) == fileCopy.m_either_has_parameter) {
            Drp::Request request;
            Drp::Response response;

            request.set_request_type(Drp::COPY_IN);
            Drp::CopyInRequest *copy_in_request = request.mutable_copy_in_request();
            // XXX expand %d and %0Nd.
            copy_in_request->set_pathname(fileCopy.m_destination);
            try {
                // XXX expand %d and %0Nd.
                copy_in_request->set_content(read_file(fileCopy.m_source));
            } catch (std::runtime_error e) {
                std::cerr << "Error reading file " << fileCopy.m_source << "\n";
                return -1;
            }
            int rv = send_message(sock, request);
            if (rv != 0) {
                fatal("send_message", rv);
            }

            rv = receive_message(sock, response);
            if (rv != 0) {
                fatal("receive_message", rv);
            }

            if (request.request_type() == Drp::COPY_IN) {
                std::cout << "copy in success: " << response.copy_in_response().success() << "\n";
                if (!response.copy_in_response().success()) {
                    return false;
                }
            } else {
                std::cout << "Got unknown response type " << request.request_type() << "\n";
            }
        }
    }

    return true;
}

// Returns whether successful.
static bool copy_out(nng_socket sock, const Parameters &parameters, int frame) {
    for (const FileCopy &fileCopy : parameters.m_out_copies) {
        if ((frame >= 0) == fileCopy.m_either_has_parameter) {
            Drp::Request request;
            Drp::Response response;

            request.set_request_type(Drp::COPY_OUT);
            Drp::CopyOutRequest *copy_out_request = request.mutable_copy_out_request();
            // XXX expand %d and %0Nd.
            copy_out_request->set_pathname(fileCopy.m_source);
            int rv = send_message(sock, request);
            if (rv != 0) {
                fatal("send_message", rv);
            }

            rv = receive_message(sock, response);
            if (rv != 0) {
                fatal("receive_message", rv);
            }

            if (request.request_type() == Drp::COPY_OUT) {
                std::cout << "copy out success: " << response.copy_out_response().success() << "\n";
                if (!response.copy_out_response().success()) {
                    return false;
                }

                // XXX expand %d and %0Nd.
                bool success = write_file(fileCopy.m_destination, response.copy_out_response().content());
                if (!success) {
                    std::cout << "Could not write local file " << fileCopy.m_destination << "\n";
                    return false;
                }
            } else {
                std::cout << "Got unknown response type " << request.request_type() << "\n";
            }
        }
    }

    return true;
}

int start_controller(const Parameters &parameters) {
    nng_socket sock;
    int rv;
    const Frames &frames = parameters.m_frames;

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

    // Copy files to worker.
    // XXX Fail everything if this fails.
    copy_in(sock, parameters, -1);

    int frame = frames.m_first;

    while (!frames.is_done(frame)) {
        std::cout << "Frame " << frame << "\n";

        // Copy files to worker.
        // XXX Fail everything if this fails.
        copy_in(sock, parameters, frame);

        // Send execute program message.
        {
            Drp::Request request;
            Drp::Response response;

            request.set_request_type(Drp::EXECUTE);
            Drp::ExecuteRequest *execute_request = request.mutable_execute_request();
            execute_request->set_executable(parameters.m_executable);
            for (std::string argument : parameters.m_arguments) {
                // XXX expand %d and %0Nd.
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

        // Copy files from worker.
        // XXX Fail everything if this fails.
        copy_out(sock, parameters, frame);

        // Next frame.
        frame += frames.m_step;
    }

    // Copy files from worker.
    // XXX Fail everything if this fails.
    copy_out(sock, parameters, -1);

    nng_close(sock);

    return 0;
}
