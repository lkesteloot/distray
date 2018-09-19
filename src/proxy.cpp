
#include <cstring>
#include <vector>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "proxy.hpp"

static const int TMP_BUFFER_SIZE = 128*1024;

// Global temporary buffer for receiving from sockets. We're single-threaded,
// so all Buffer objects can use this.
static uint8_t *g_tmp_buffer;

// Buffer for passing data from one file descriptor to another.
class Buffer {
    // A string is not an efficient way to store this because we erase from
    // the front. A deque doesn't work because it doesn't necessarily store
    // its array contiguously, and there's no bulk addition or data API.
    std::string m_buffer;

public:
    // Whether the buffer could send data right now (is not empty).
    bool can_send() const {
        return !m_buffer.empty();
    }

    // Sends as much as it can. Returns whether successful. If not, sets errno.
    bool send(int fd) {
        if (can_send()) {
            // Try to send the whole thing.
            int sent_here = ::send(fd, &m_buffer[0], m_buffer.size(), 0);
            if (sent_here == -1) {
                return false;
            }

            // Erase what we sent.
            m_buffer.erase(0, sent_here);
        }

        return true;
    }

    // Receive as much as we can, adding it to our buffer. Returns whether
    // successful. If not, sets errno.
    bool receive(int fd) {
        // Receive into the global temporary buffer.
        int received = recv(fd, g_tmp_buffer, TMP_BUFFER_SIZE, 0);
        if (received == -1) {
            return false;
        }

        if (received == 0) {
            // Other side closed connection.
            // XXX handle.
            // This isn't technically correct, but it'll be handled the same
            // way higher up the stack.
            errno = ECONNRESET;
            return false;
        }

        add(g_tmp_buffer, received);

        return true;
    }

private:
    // Add the received data to the buffer.
    void add(uint8_t *data, int size) {
        m_buffer.append((char *) data, size);
    }
};

// Connection between a worker and a controller.
class Connection {
    // File descriptor for the worker, or -1 if none has connected yet.
    int m_worker_fd;

    // File descriptor for the controller, or -1 if none has connected yet.
    int m_controller_fd;

    // Buffer for each direction.
    Buffer m_w2c;
    Buffer m_c2w;

public:
    Connection()
        : m_worker_fd(-1), m_controller_fd(-1) {

        // Nothing.
    }

    void set_worker_fd(int worker_fd) {
        m_worker_fd = worker_fd;
    }

    void set_controller_fd(int controller_fd) {
        m_controller_fd = controller_fd;
    }

    int get_worker_fd() const {
        return m_worker_fd;
    }

    int get_controller_fd() const {
        return m_controller_fd;
    }

    // Add entries to the list of pollfds for each file descriptor we're
    // interested in.
    void add_pollfds(std::vector<struct pollfd> &pollfds) const {
        add_pollfd(pollfds, m_worker_fd, m_c2w);
        add_pollfd(pollfds, m_controller_fd, m_w2c);
    }

    // Send as much as we can to this file descriptor. Returns whether
    // successful. If not, sets errno.
    bool send(int fd) {
        if (fd == m_worker_fd) {
            return m_c2w.send(fd);
        } else if (fd == m_controller_fd) {
            return m_w2c.send(fd);
        } else {
            // XXX handle.
            std::cerr << "Fatal: Was passed fd " << fd << " that we don't know about.\n";
            exit(-1);
        }
    }

    // Receive as much as we can from this file descriptor. Returns whether
    // successful. If not, sets errno.
    bool receive(int fd) {
        if (fd == m_worker_fd) {
            return m_w2c.receive(fd);
        } else if (fd == m_controller_fd) {
            return m_c2w.receive(fd);
        } else {
            // XXX handle.
            std::cerr << "Fatal: Was passed fd " << fd << " that we don't know about.\n";
            exit(-1);
        }
    }

    // Shut down.
    void die() {
        if (m_worker_fd != -1) {
            close(m_worker_fd);
            m_worker_fd = -1;
        }
        if (m_controller_fd != -1) {
            close(m_controller_fd);
            m_controller_fd = -1;
        }
    }

private:
    // Add entry to the list of pollfds if we're interested in this file
    // descriptor. The buffer is the outgoing buffer for this file descriptor.
    void add_pollfd(std::vector<struct pollfd> &pollfds, int fd, const Buffer &buffer) const {
        if (fd != -1) {
            struct pollfd pollfd;

            pollfd.fd = fd;
            pollfd.events = POLLIN | (buffer.can_send() ? POLLOUT : 0);
            pollfd.revents = 0;

            pollfds.push_back(pollfd);
        }
    }
};

// Return any connection that has either the specified worker fd or specified
// controller fd. Specify -2 to mean "don't care". Return null if not found.
static Connection *find(const std::vector<Connection *> &connections,
        int worker_fd, int controller_fd) {

    for (Connection *connection : connections) {
        if (connection->get_worker_fd() == worker_fd
                || connection->get_controller_fd() == controller_fd) {

            return connection;
        }
    }

    return nullptr;
}

// Write connection statistics to standard out.
static void log_connections(const std::vector<Connection *> &connections) {
    int only_worker = 0;
    int only_controller = 0;
    int both = 0;
    int total = 0;

    for (Connection *connection : connections) {
        total++;

        if (connection->get_worker_fd() != -1 && connection->get_controller_fd() != -1) {
            both++;
        } else if (connection->get_worker_fd() != -1) {
            only_worker++;
        } else {
            only_controller++;
        }
    }

    printf("%14d %14d %14d %14d\n", total, only_worker, only_controller, both);
}

// Close both sides of a connection and remove it from the collection.
static void close_connection(std::vector<Connection *> &connections, Connection *connection) {
    // Shut down.
    connection->die();

    // Remove from the list.
    for (std::vector<Connection *>::iterator itr = connections.begin();
            itr != connections.end(); ++itr) {

        if (connection == *itr) {
            connections.erase(itr);
            break;
        }
    }

    delete connection;
    log_connections(connections);
}

// Serve up our proxy. Returns program exit code.
int start_proxy(Parameters &parameters) {
    g_tmp_buffer = new uint8_t[TMP_BUFFER_SIZE];

    // Resolve endpoints.
    bool success = parameters.m_worker_endpoint.resolve(true, "", DEFAULT_WORKER_PORT);
    if (!success) {
        // XXX Handle.
        return -1;
    }
    success = parameters.m_controller_endpoint.resolve(true, "", DEFAULT_CONTROLLER_PORT);
    if (!success) {
        // XXX Handle.
        return -1;
    }

    // Listen for workers.
    int worker_server_fd = create_server_socket(parameters.m_worker_endpoint);
    if (worker_server_fd == -1) {
        perror("create_server_socket (worker)");
        return -1;
    }

    // Listen for controllers.
    int controller_server_fd = create_server_socket(parameters.m_controller_endpoint);
    if (controller_server_fd == -1) {
        perror("create_server_socket (controller)");
        return -1;
    }

    // Match up pairs as they come in.
    std::vector<Connection *> connections;

    printf("%14s %14s %14s %14s\n", "total", "worker", "controller", "both");
    log_connections(connections);

    while (true) {
        // Poll entry for each server socket.
        std::vector<struct pollfd> pollfds(2);

        // Listening sockets.
        pollfds[0].fd = worker_server_fd;
        pollfds[0].events = POLLIN;
        pollfds[0].revents = 0;
        pollfds[1].fd = controller_server_fd;
        pollfds[1].events = POLLIN;
        pollfds[1].revents = 0;

        // Every connection we have.
        for (Connection *connection : connections) {
            connection->add_pollfds(pollfds);
        }

        // Wait for event on any file descriptor.
        int result = poll(pollfds.data(), pollfds.size(), -1);
        if (result == -1) {
            perror("poll");
            return -1;
        }

        for (int i = 0; i < pollfds.size(); i++) {
            int fd = pollfds[i].fd;
            short revents = pollfds[i].revents;
            bool skip_rest = false;

            // See if we can read from this file descriptor.
            if ((revents & POLLIN) != 0) {
                if (i <= 1) {
                    // New connection from controller or worker.
                    struct sockaddr_in remote_addr;
                    socklen_t remote_addr_len = sizeof(remote_addr);
                    int conn_fd = accept(pollfds[i].fd,
                            (struct sockaddr *) &remote_addr, &remote_addr_len);
                    if (result == -1) {
                        perror("accept");
                        return -1;
                    }

                    // Find an existing connection if possible.
                    Connection *connection;

                    if (i == 0) {
                        // Connection from worker. Look for existing connection
                        // with only a controller.
                        connection = find(connections, -1, -2);
                        if (connection != nullptr) {
                            connection->set_worker_fd(conn_fd);
                        }
                    } else {
                        // Connection from controller. Look for existing connection
                        // with only a worker.
                        connection = find(connections, -2, -1);
                        if (connection != nullptr) {
                            connection->set_controller_fd(conn_fd);
                        }
                    }

                    if (connection == nullptr) {
                        // Didn't find existing connection. Create a new one.
                        connection = new Connection();
                        if (i == 0) {
                            connection->set_worker_fd(conn_fd);
                        } else {
                            connection->set_controller_fd(conn_fd);
                        }
                        connections.push_back(connection);
                    }

                    log_connections(connections);
                } else {
                    // Find the connection for this file descriptor.
                    Connection *connection = find(connections, fd, fd);
                    if (connection == nullptr) {
                        // XXX handle.
                        std::cerr << "Did not find incoming fd " << fd << "\n";
                        exit(-1);
                    } else {
                        success = connection->receive(fd);
                        if (!success) {
                            if (errno == ECONNRESET) {
                                // Other side disconnected.
                                close_connection(connections, connection);
                                skip_rest = true;
                            } else {
                                perror("connection receive");
                                return -1;
                            }
                        }
                    }
                }
            }

            // See if we can write to this file descriptor.
            if ((revents & POLLOUT) != 0 && !skip_rest) {
                // Can't ever write to listening socket.
                if (i >= 2) {
                    // Find the connection for this file descriptor.
                    Connection *connection = find(connections, fd, fd);
                    if (connection == nullptr) {
                        // XXX handle.
                        std::cerr << "Did not find outgoing fd " << fd << "\n";
                        exit(-1);
                    } else {
                        success = connection->send(fd);
                        if (!success) {
                            perror("connection send");
                            return -1;
                        }
                    }
                }
            }

            if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 && !skip_rest) {
                // Socket is dead, kill the worker.
                if (i >= 2) {
                    Connection *connection = find(connections, fd, fd);
                    if (connection == nullptr) {
                        // XXX handle.
                        std::cerr << "Did not find dead fd " << fd << "\n";
                        exit(-1);
                    } else {
                        close_connection(connections, connection);
                    }
                }
            }
        }
    }

    delete[] g_tmp_buffer;

    return 0;
}
