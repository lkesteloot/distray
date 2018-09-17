
#include <netinet/in.h>
#include <poll.h>

#include "controller.hpp"
#include "Drp.pb.h"
#include "RemoteWorker.hpp"
#include "Parameters.hpp"

// Returns any idle worker, or null if none is idle.
static RemoteWorker *get_idle_worker(const std::vector<RemoteWorker *> &remote_workers) {
    for (RemoteWorker *remote_worker : remote_workers) {
        if (remote_worker->is_idle()) {
            return remote_worker;
        }
    }

    return nullptr;
}

// Returns true iff any worker is non-idle. Returns false if all workers are idle.
static bool any_worker_working(const std::vector<RemoteWorker *> &remote_workers) {
    for (RemoteWorker *remote_worker : remote_workers) {
        if (!remote_worker->is_idle()) {
            return true;
        }
    }

    return false;
}

int start_controller(const Parameters &parameters) {
    // Create socket.
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return -1;
    }

    // Make sure we can re-bind to this socket immediately shutting down last time.
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

    // Bind to our socket.
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // XXX Make address configurable.
    addr.sin_port = htons(1120); // XXX Make port configurable.

    result = bind(sockfd, (struct sockaddr *) &addr, sizeof(addr));
    if (result == -1) {
        perror("bind");
        return -1;
    }

    // Listen for new connections.
    result = listen(sockfd, 10);
    if (result == -1) {
        perror("listen");
        return -1;
    }

    // Get all frames.
    std::deque<int> frames = parameters.m_frames.get_all();

    // Our list of remote workers.
    std::vector<RemoteWorker *> remote_workers;

    // Keep going as long as there are frames to be done or workers working on frames.
    while (!frames.empty() || any_worker_working(remote_workers)) {
        // Poll entry for every worker, plus our listening socket.
        std::vector<struct pollfd> pollfds(1 + remote_workers.size());

        // Listening socket.
        pollfds[0].fd = sockfd;
        pollfds[0].events = POLLIN;
        pollfds[0].revents = 0;

        // Worker sockets.
        for (int i = 0; i < remote_workers.size(); i++) {
            remote_workers[i]->fill_pollfd(pollfds[i + 1]);
        }

        // Wait for event on any file descriptor.
        result = poll(pollfds.data(), pollfds.size(), -1);
        if (result == -1) {
            perror("poll");
            return -1;
        }

        // Go backward so we can delete dead workers.
        for (int i = pollfds.size() - 1; i >= 0; i--) {
            // printf("%d: %x\n", i, pollfds[i].revents);
            short revents = pollfds[i].revents;

            // See if we can read from this file descriptor.
            if ((revents & POLLIN) != 0) {
                if (i == 0) {
                    // New connection from worker.
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

            // See if we can write to this file descriptor.
            if ((revents & POLLOUT) != 0) {
                // Can't ever write to listening socket.
                if (i > 0) {
                    bool success = remote_workers[i - 1]->send();
                    if (!success) {
                        perror("worker send");
                        return -1;
                    }
                }
            }
            if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                // Socket is dead, kill the worker.
                if (i > 0) {
                    int index = i - 1;
                    RemoteWorker *remote_worker = remote_workers[index];
                    int frame = remote_worker->get_frame();
                    std::cout << "Worker from " << remote_worker->hostname() <<
                        " working on frame " << frame << " is dead.\n";
                    frames.push_front(frame);
                    delete remote_workers[index];
                    remote_workers.erase(remote_workers.begin() + index);
                }
            }
        }

        // Hand out frames to any available worker.
        RemoteWorker *remote_worker;
        while (!frames.empty() && (remote_worker = get_idle_worker(remote_workers)) != nullptr) {
            int frame = frames.front();
            frames.pop_front();
            remote_worker->run_frame(frame);
        }
    }

    return 0;
}
