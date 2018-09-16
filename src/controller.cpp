
#include <cstring>
#include <stdexcept>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#include "controller.hpp"
#include "Drp.pb.h"
#include "util.hpp"
#include "RemoteWorker.hpp"

// Returns whether successful.
static bool copy_in(int sock, const Parameters &parameters, int frame) {
    for (const FileCopy &fileCopy : parameters.m_in_copies) {
        if ((frame >= 0) == fileCopy.has_parameter()) {
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
            int rv = send_message_sock(sock, request);
            if (rv == -1) {
                perror("send_message_sock");
                return false;
            }

            rv = receive_message_sock(sock, response);
            if (rv == -1) {
                perror("receive_message_sock");
                return false;
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
static bool copy_out(int sock, const Parameters &parameters, int frame) {
    for (const FileCopy &fileCopy : parameters.m_out_copies) {
        if ((frame >= 0) == fileCopy.has_parameter()) {
            Drp::Request request;
            Drp::Response response;

            request.set_request_type(Drp::COPY_OUT);
            Drp::CopyOutRequest *copy_out_request = request.mutable_copy_out_request();
            copy_out_request->set_pathname(substitute_parameter(fileCopy.m_source, frame));
            int rv = send_message_sock(sock, request);
            if (rv == -1) {
                perror("send_message_sock");
                return -1;
            }

            rv = receive_message_sock(sock, response);
            if (rv == -1) {
                perror("receive_message_sock");
                return -1;
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
    // Create socket.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    int result = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (result == -1) {
        perror("setsockopt");
        return -1;
    }
    result = setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    if (result == -1) {
        perror("setsockopt");
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(1120);

    result = bind(sockfd, (struct sockaddr *) &addr, sizeof(addr));
    if (result == -1) {
        perror("bind");
        return -1;
    }

    result = listen(sockfd, 10);
    if (result == -1) {
        perror("listen");
        return -1;
    }

    // Get all frames.
    std::deque<int> frames = parameters.m_frames.get_all();

    std::vector<RemoteWorker *> remote_workers;

    while (!frames.empty()) {
        std::vector<struct pollfd> pollfds;

        // Listening socket.
        pollfds.resize(1);
        pollfds[0].fd = sockfd;
        pollfds[0].events = POLLIN;
        pollfds[0].revents = 0;

        for (RemoteWorker *remote_worker : remote_workers) {
            pollfds.push_back(remote_worker->get_pollfd());
        }

        result = poll(pollfds.data(), pollfds.size(), -1);
        if (result == -1) {
            perror("poll");
            return -1;
        }

        for (int i = 0; i < pollfds.size(); i++) {
            // printf("%d: %x\n", i, pollfds[i].revents);
            short revents = pollfds[i].revents;

            if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                // Socket is dead, kill the worker.
                std::cout << "Worker " << (i - 1) << " is dead.\n";
            }
            if ((revents & POLLIN) != 0) {
                if (i == 0) {
                    struct sockaddr_in remote_addr;
                    socklen_t remote_addr_len = sizeof(remote_addr);
                    int connfd = accept(sockfd, (struct sockaddr *) &remote_addr, &remote_addr_len);
                    if (result == -1) {
                        perror("accept");
                        return -1;
                    }

                    RemoteWorker *remote_worker = new RemoteWorker(connfd, parameters);
                    remote_workers.push_back(remote_worker);
                    remote_worker->start();
                } else {
                    bool success = remote_workers[i - 1]->receive();
                    if (!success) {
                        perror("worker receive");
                        return -1;
                    }
                }
            }
            if ((revents & POLLOUT) != 0) {
                if (i > 0) {
                    bool success = remote_workers[i - 1]->send();
                    if (!success) {
                        perror("worker send");
                        return -1;
                    }
                }
            }
        }

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
    // Create socket.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    int result = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (result == -1) {
        perror("setsockopt");
        return -1;
    }
    result = setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    if (result == -1) {
        perror("setsockopt");
        return -1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(1120);

    result = bind(sockfd, (struct sockaddr *) &addr, sizeof(addr));
    if (result == -1) {
        perror("bind");
        return -1;
    }

    result = listen(sockfd, 10);
    if (result == -1) {
        perror("listen");
        return -1;
    }

    struct sockaddr_in remote_addr;
    socklen_t remote_addr_len = sizeof(remote_addr);
    int connfd = accept(sockfd, (struct sockaddr *) &remote_addr, &remote_addr_len);
    if (result == -1) {
        perror("accept");
        return -1;
    }

    // Send welcome message.
    {
        Drp::Request request;
        Drp::Response response;

        request.set_request_type(Drp::WELCOME);
        result = send_message_sock(connfd, request);
        if (result == -1) {
            perror("send_message_sock");
            return -1;
        }

        result = receive_message_sock(connfd, response);
        if (result == -1) {
            perror("receive_message_sock");
            return -1;
        }

        if (response.request_type() == Drp::WELCOME) {
            std::cout << "hostname: " << response.welcome_response().hostname() <<
                ", cores: " << response.welcome_response().core_count() << "\n";
        } else {
            std::cout << "Got unknown response type " << response.request_type() << "\n";
        }
    }

    // Copy files to worker.
    // XXX Fail everything if this fails.
    copy_in(connfd, parameters, -1);

    const Frames &frames = parameters.m_frames;
    int frame = frames.m_first;

    while (!frames.is_done(frame)) {
        std::cout << "Frame " << frame << "\n";

        // Copy files to worker.
        // XXX Fail everything if this fails.
        copy_in(connfd, parameters, frame);

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
            result = send_message_sock(connfd, request);
            if (result == -1) {
                perror("send_message_sock");
                return -1;
            }

            result = receive_message_sock(connfd, response);
            if (result == -1) {
                perror("receive_message_sock");
                return -1;
            }

            if (request.request_type() == Drp::EXECUTE) {
                std::cout << "status: " << response.execute_response().status() << "\n";
            } else {
                std::cout << "Got unknown response type " << request.request_type() << "\n";
            }
        }

        // Copy files from worker.
        // XXX Fail everything if this fails.
        copy_out(connfd, parameters, frame);

        // Next frame.
        frame += frames.m_step;
    }

    // Copy files from worker.
    // XXX Fail everything if this fails.
    copy_out(connfd, parameters, -1);

    // close(connfd);
    // close(sockfd);

    return 0;
}
