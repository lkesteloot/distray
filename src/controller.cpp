
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

// Start the controller. Returns program exit code.
int start_controller(const Parameters &parameters) {
    int sock_fd = create_server_socket(1120); // XXX Make port configurable.
    if (sock_fd == -1) {
        perror("create_server_socket");
        return -1;
    }

    // Get all frames.
    std::deque<int> frames = parameters.m_frames.get_all();

    // Our list of remote workers.
    std::vector<RemoteWorker *> remote_workers;

    // Start a worker connection for each proxy.
    int proxy_fd = create_client_socket(1121); // XXX use port from parameters, default to 1121.
    if (proxy_fd == -1) {
        return -1;
    }

    {
        RemoteWorker *remote_worker = new RemoteWorker(proxy_fd, parameters);
        remote_workers.push_back(remote_worker);
        remote_worker->start();
    }

    // Keep going as long as there are frames to be done or workers working on frames.
    while (!frames.empty() || any_worker_working(remote_workers)) {
        // Poll entry for every worker, plus our listening socket.
        std::vector<struct pollfd> pollfds(1 + remote_workers.size());

        // Listening socket.
        pollfds[0].fd = sock_fd;
        pollfds[0].events = POLLIN;
        pollfds[0].revents = 0;

        // Worker sockets.
        for (int i = 0; i < remote_workers.size(); i++) {
            remote_workers[i]->fill_pollfd(pollfds[i + 1]);
        }

        // Wait for event on any file descriptor.
        int result = poll(pollfds.data(), pollfds.size(), -1);
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
                    int connfd = accept(sock_fd, (struct sockaddr *) &remote_addr, &remote_addr_len);
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
