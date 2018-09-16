
#include <cstring>
#include <stdexcept>

#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>
#include <nng/supplemental/util/platform.h>

#include "controller.hpp"
#include "Drp.pb.h"
#include "util.hpp"

static void fatal(const char *func, int rv)
{
    fprintf(stderr, "%s: %s\n", func, nng_strerror(rv));
    exit(1);
}

// Represents a remote worker. Stores our state for it.
class RemoteWorker {
public:
    enum State {
        // Initial states.
        INIT,
        SENT_WELCOME_REQUEST,
        WAIT_WELCOME_RESPONSE,

        // Waiting for assignment.
        IDLE,

        // Sending an execute command.
        SEND_EXECUTE_REQUEST,
        SENT_EXECUTE_REQUEST,
        WAIT_EXECUTE_RESPONSE,
    };

    // Our current state in the state machine.
    State m_state;

    // User parameters.
    const Parameters &m_parameters;

    // Whatever frame we're working on, or -1 for none.
    int m_frame;

    // Hostname of this remote machine. Empty if no one has connected yet.
    std::string m_hostname;

    // NNG's async I/O state for this socket.
    nng_aio *m_aio;

    // Context for the NNG req/res protocol.
    nng_ctx m_ctx;

    RemoteWorker(nng_socket sock, const Parameters &parameters)
        : m_state(INIT), m_parameters(parameters), m_frame(-1) {

        int rv;

        if ((rv = nng_aio_alloc(&m_aio, callback, this)) != 0) {
            fatal("nng_aio_alloc", rv);
        }
        if ((rv = nng_ctx_open(&m_ctx, sock)) != 0) {
            fatal("nng_ctx_open", rv);
        }
    }

    // Kick off the process.
    void start() {
        dispatch();
    }

    bool is_idle() {
        return m_state == IDLE;
    }

    void run_frame(int frame) {
        if (!is_idle()) {
            std::cerr << "Error: Gave a frame to a non-idle worker.\n";
            exit(1);
        }

        std::cout << "Starting frame " << frame << " on " << m_hostname << "\n";

        m_frame = frame;
        m_state = SEND_EXECUTE_REQUEST;
        dispatch();
    }

private:
    // C-style static callback.
    static void callback(void *arg) {
        RemoteWorker *remoteWorker = (RemoteWorker *) arg;

        // Delegate to dispatcher.
        remoteWorker->dispatch();
    }

    // Move the state machine forward.
    void dispatch() {
        // std::cout << "RemoteWorker: state = " << m_state << "\n";

        switch (m_state) {
            case INIT: {
                // Send welcome message.
                Drp::Request request;
                request.set_request_type(Drp::WELCOME);
                send_request(request, SENT_WELCOME_REQUEST);
                break;
            }

            case SENT_WELCOME_REQUEST: {
                check_result();
                m_state = WAIT_WELCOME_RESPONSE;
                // This might call us synchronously, do it last:
                nng_ctx_recv(m_ctx, m_aio);
                break;
            }

            case WAIT_WELCOME_RESPONSE: {
                check_result();
                Drp::Response response;
                receive_response(response, Drp::WELCOME);
                m_hostname = response.welcome_response().hostname();
                std::cout << "hostname: " << m_hostname <<
                    ", cores: " << response.welcome_response().core_count() << "\n";
                m_state = IDLE;
                break;
            }

            case IDLE: {
                // We're never dispatched in idle mode.
                std::cout << "Should not be in IDLE.\n";
                exit(1);
            }

            case SEND_EXECUTE_REQUEST: {
                Drp::Request request;
                request.set_request_type(Drp::EXECUTE);
                Drp::ExecuteRequest *execute_request = request.mutable_execute_request();
                execute_request->set_executable(m_parameters.m_executable);
                for (std::string argument : m_parameters.m_arguments) {
                    execute_request->add_argument(substitute_parameter(argument, m_frame));
                }
                send_request(request, SENT_EXECUTE_REQUEST);
                break;
            }

            case SENT_EXECUTE_REQUEST: {
                check_result();
                m_state = WAIT_EXECUTE_RESPONSE;
                // This might call us synchronously, do it last:
                nng_ctx_recv(m_ctx, m_aio);
                break;
            }

            case WAIT_EXECUTE_RESPONSE: {
                check_result();
                Drp::Response response;
                receive_response(response, Drp::EXECUTE);
                // std::cout << "status: " << response.execute_response().status() << "\n";
                m_state = IDLE;
                break;
            }
        }
    }

    void check_result() {
        int rv;

        if ((rv = nng_aio_result(m_aio)) != 0) {
            fatal("nng_aio_result", rv);
        }
    }

    void send_request(const Drp::Request &request, State next_state) {
        // Serialize to bytes.
        size_t size = request.ByteSize();
        nng_msg *msg;
        int rv;
        if ((rv = nng_msg_alloc(&msg, size)) != 0) {
            fatal("nng_msg_alloc", rv);
        }
        void *buf = nng_msg_body(msg);
        request.SerializeToArray(buf, size);
        nng_aio_set_msg(m_aio, msg);
        m_state = next_state;
        // This might call us synchronously, do it last:
        nng_ctx_send(m_ctx, m_aio);
    }

    void receive_response(Drp::Response &response, Drp::RequestType expected_request_type) {
        nng_msg *msg = nng_aio_get_msg(m_aio);
        // Decode. XXX check result code (a bool, undocumented).
        response.ParseFromArray(nng_msg_body(msg), nng_msg_len(msg));
        nng_msg_free(msg);

        if (response.request_type() != expected_request_type) {
            std::cout << "Got unknown response type " << response.request_type() <<
                ", expected " << expected_request_type << "\n";
            exit(1);
        }
    }
};

// Returns whether successful.
static bool copy_in(nng_socket sock, const Parameters &parameters, int frame) {
    for (const FileCopy &fileCopy : parameters.m_in_copies) {
        if ((frame >= 0) == fileCopy.m_either_has_parameter) {
            Drp::Request request;
            Drp::Response response;

            request.set_request_type(Drp::COPY_IN);
            Drp::CopyInRequest *copy_in_request = request.mutable_copy_in_request();
            copy_in_request->set_pathname(substitute_parameter(fileCopy.m_destination, frame));
            try {
                copy_in_request->set_content(read_file(substitute_parameter(fileCopy.m_source, frame)));
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
            copy_out_request->set_pathname(substitute_parameter(fileCopy.m_source, frame));
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

                bool success = write_file(substitute_parameter(fileCopy.m_destination, frame), response.copy_out_response().content());
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

    if ((rv = nng_req0_open(&sock)) != 0) {
        fatal("nng_socket", rv);
    }
    // Raise limit on received message size. We trust senders.
    if ((rv = nng_setopt_size(sock, NNG_OPT_RECVMAXSZ, 20*1024*1024)) != 0) {
        fatal("nng_setopt_size", rv);
    }
    if ((rv = nng_listen(sock, parameters.m_url.c_str(), NULL, 0)) != 0) {
        fatal("nng_listen", rv);
    }

    // Get all frames.
    std::deque<int> frames = parameters.m_frames.get_all();

    std::vector<RemoteWorker *> remote_workers;

    remote_workers.push_back(new RemoteWorker(sock, parameters));
    remote_workers[0]->start();

    while (!frames.empty()) {
        nng_msleep(100);

        for (RemoteWorker *remote_worker : remote_workers) {
            if (remote_worker->is_idle()) {
                int frame = frames.front();
                frames.pop_front();
                remote_worker->run_frame(frame);
            }
        }
    }

    return 0;
}

int start_controller_sync(const Parameters &parameters) {
    nng_socket sock;
    int rv;
    const Frames &frames = parameters.m_frames;

    if ((rv = nng_req0_open(&sock)) != 0) {
        fatal("nng_socket", rv);
    }
    // Raise limit on received message size. We trust senders.
    if ((rv = nng_setopt_size(sock, NNG_OPT_RECVMAXSZ, 20*1024*1024)) != 0) {
        fatal("nng_setopt_size", rv);
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
                execute_request->add_argument(substitute_parameter(argument, frame));
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
