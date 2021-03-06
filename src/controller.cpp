
#include <netinet/in.h>
#include <poll.h>
#include <set>

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

// Remove the remote worker from our list.
static void kill_worker(std::vector<RemoteWorker *> &remote_workers,
        std::deque<int> &frames, int index) {

    RemoteWorker *remote_worker = remote_workers[index];

    int frame = remote_worker->get_frame();
    if (frame == -1) {
        std::cout << "Warning: Pending connection disconnected. Proxy must have died.\n";
    } else {
        std::cout << "Worker from " << remote_worker->hostname() <<
            " working on frame " << frame << " is dead.\n";
        frames.push_front(frame);
    }

    remote_workers.erase(remote_workers.begin() + index);
    delete remote_worker;
}

// Start the controller. Returns program exit code.
int start_controller(Parameters &parameters) {
    // Resolve endpoints.
    bool success = parameters.m_endpoint.resolve(true, "", DEFAULT_WORKER_PORT);
    if (!success) {
        return -1;
    }
    for (Endpoint &endpoint : parameters.m_proxy_endpoints) {
        success = endpoint.resolve(false, "", DEFAULT_CONTROLLER_PORT);
        if (!success) {
            return -1;
        }
    }

    int sock_fd = create_server_socket(parameters.m_endpoint);
    if (sock_fd == -1) {
        perror("create_server_socket");
        return -1;
    }

    // Get all frames.
    std::deque<int> frames = parameters.m_frames.get_all();

    // Our list of remote workers.
    std::vector<RemoteWorker *> remote_workers;

    // Keep going as long as there are frames to be done or workers working on frames.
    while (!frames.empty() || any_worker_working(remote_workers)) {
        // Create blocking (non-connected) connections to proxies, if necessary.
        std::set<int> proxy_indices;

        // Fill with every index to start with.
        for (int proxy_index = 0; proxy_index < parameters.m_proxy_endpoints.size(); proxy_index++) {
            proxy_indices.insert(proxy_index);
        }

        // Remove the ones that we have a connection to.
        for (RemoteWorker *remote_worker : remote_workers) {
            int proxy_index = remote_worker->get_proxy_index();
            if (proxy_index != -1) {
                proxy_indices.erase(proxy_index);
            }
        }

        // Whatever's left, create a connection for.
        for (int proxy_index : proxy_indices) {
            // Start a worker connection for each proxy.
            int proxy_fd = create_client_socket(parameters.m_proxy_endpoints[proxy_index]);
            if (proxy_fd == -1) {
                return -1;
            }

            RemoteWorker *remote_worker = new RemoteWorker(proxy_fd, parameters);
            remote_worker->set_proxy_index(proxy_index);
            remote_worker->start();
            remote_workers.push_back(remote_worker);
        }

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
                    success = remote_workers[i - 1]->receive();
                    if (!success) {
                        if (errno == ECONNRESET) {
                            // Other side disconnected.
                            kill_worker(remote_workers, frames, i - 1);
                        } else {
                            perror("worker receive");
                            return -1;
                        }
                    }
                }
            }

            // See if we can write to this file descriptor.
            if ((revents & POLLOUT) != 0) {
                // Can't ever write to listening socket.
                if (i > 0) {
                    success = remote_workers[i - 1]->send();
                    if (!success) {
                        perror("worker send");
                        return -1;
                    }
                }
            }
            if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                // Socket is dead, kill the worker.
                if (i > 0) {
                    kill_worker(remote_workers, frames, i - 1);
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
