
#include <thread>
#include <unistd.h>
#include <cstring>
#include <errno.h>
#include <netinet/in.h>

#include "worker.hpp"
#include "Drp.pb.h"
#include "util.hpp"

static void handle_welcome(const Drp::WelcomeRequest &request, Drp::WelcomeResponse &response) {
    char hostname[128];
    int rv = gethostname(hostname, sizeof(hostname));
    if (rv == -1) {
        strcpy(hostname, "unknown");
    }
    response.set_hostname(hostname);
    response.set_core_count(std::thread::hardware_concurrency());
}

static void handle_copy_in(const Drp::CopyInRequest &request, Drp::CopyInResponse &response) {
    std::string pathname = request.pathname();

    if (!is_pathname_local(pathname)) {
        // Shouldn't happen, we check this on the controller.
        std::cerr << "Asked to write to non-local pathname: " << pathname << "\n";
        response.set_success(false);
        return;
    }

    // XXX fail if file exists and is executable.

    bool success = write_file(pathname, request.content());
    if (!success) {
        std::cerr << "Failed to write to file: " << pathname << "\n";
    }

    response.set_success(success);
}

static void handle_execute(const Drp::ExecuteRequest &request, Drp::ExecuteResponse &response) {
    std::string executable = request.executable();

    if (!is_pathname_local(executable)) {
        // Shouldn't happen, we check this on the controller.
        std::cerr << "Asked to run non-local executable: " << executable << "\n";
        response.set_status(-1);
        return;
    }

    // Set up arguments.
    int count = request.argument_size();
    const char **args = new const char *[count + 2];

    args[0] = executable.c_str();
    for (int i = 0; i < count; i++) {
        args[i + 1] = request.argument(i).c_str();
    }
    args[count + 1] = nullptr;

    // Fork a child process.
    pid_t pid = fork();
    if (pid == 0) {
        // Child process.

        // Do not search the path and don't change the environment.
        int result = execv(args[0], (char **) args);
        if (result == 0) {
            // Shouldn't get here.
            std::cerr << "Could not execute " << executable << ": No error\n";
        } else {
            std::cerr << "Could not execute " << executable << ": " << strerror(errno) << "\n";
        }
        exit(-1);
    }

    // Parent process.
    int status;
    wait4(pid, &status, 0, nullptr);

    // Free up our arguments.
    delete[] args;
    args = nullptr;

    response.set_status(WEXITSTATUS(status));
}

static void handle_copy_out(const Drp::CopyOutRequest &request, Drp::CopyOutResponse &response) {
    std::string pathname = request.pathname();

    if (!is_pathname_local(pathname)) {
        // Shouldn't happen, we check this on the controller.
        std::cerr << "Asked to read from non-local pathname: " << pathname << "\n";
        response.set_success(false);
        return;
    }

    try {
        response.set_content(read_file(pathname));
        response.set_success(true);
    } catch (std::runtime_error e) {
        std::cerr << "Failed to read from file: " << pathname << "\n";
        response.set_success(false);
    }
}

// Start a worker. Returns program exit code.
int start_worker(const Parameters &parameters) {
    int sockfd = create_client_socket(parameters.m_endpoint);
    if (sockfd == -1) {
        return -1;
    }

    for (;;) {
        Drp::Request request;
        Drp::Response response;

        int result = receive_message(sockfd, request);
        if (result == -1) {
            perror("receive_message_sock");
            return -1;
        }

        // std::cout << "Received message " << request.request_type() << "\n";
        response.set_request_type(request.request_type());

        switch (request.request_type()) {
            case Drp::WELCOME:
                handle_welcome(request.welcome_request(),
                        *response.mutable_welcome_response());
                break;

            case Drp::COPY_IN:
                handle_copy_in(request.copy_in_request(),
                        *response.mutable_copy_in_response());
                break;

            case Drp::EXECUTE:
                handle_execute(request.execute_request(),
                        *response.mutable_execute_response());
                break;

            case Drp::COPY_OUT:
                handle_copy_out(request.copy_out_request(),
                        *response.mutable_copy_out_response());
                break;

            default:
                std::cerr << "Unhandled message type " << request.request_type() << "\n";
                break;
        }

        result = send_message(sockfd, response);
        if (result == -1) {
            perror("send_message_sock");
            return -1;
        }
    }

    return 0;
}
