
#include <netinet/in.h>
#include <poll.h>

#include "controller.hpp"
#include "RemoteWorker.hpp"
#include "Parameters.hpp"

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
