
#include <cstring>
#include <stdexcept>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#include "controller.hpp"
#include "Drp.pb.h"
#include "util.hpp"

// Represents data that needs to be sent asynchronously.
class OutgoingBuffer {
public:
    // File descriptor we're sending on.
    int m_fd;

    // Buffer includes size header.
    uint8_t *m_buffer;

    // Size includes size header.
    uint32_t m_size;

    // How many bytes have been sent.
    uint32_t m_sent;

    OutgoingBuffer(int fd)
        : m_fd(fd), m_buffer(nullptr), m_size(0), m_sent(0) {

        // Nothing.
    }

    virtual ~OutgoingBuffer() {
        delete[] m_buffer;
    }

    // Set the outgoing message. Does not send anything.
    void set_message(const google::protobuf::Message &message) {
        delete[] m_buffer;

        int data_size = message.ByteSize();
        m_size = data_size + sizeof(data_size);
        m_buffer = new uint8_t[m_size];
        *((uint32_t *) m_buffer) = htonl(data_size);
        message.SerializeToArray(m_buffer + sizeof(data_size), data_size);
        m_sent = 0;
    }

    // Whether we have something to write.
    bool need_send() const {
        return m_sent < m_size;
    }

    // Sends as much as it can. Returns whether successful. If not, sets errno.
    bool send() {
        if (need_send()) {
            uint32_t bytes_left = m_size - m_sent;

            int sent_here = ::send(m_fd, m_buffer + m_sent, bytes_left, 0);
            if (sent_here == -1) {
                return false;
            }

            m_sent += sent_here;
        }

        return true;
    }
};

// Buffer that accumulates bytes until enough are ready to receive a message.
class IncomingBuffer {
public:
    // File descriptor we're receiving on.
    int m_fd;

    // Buffer does not include size header.
    uint8_t *m_buffer;

    // Size does not include size header.
    uint32_t m_size;

    // Whether we've received the size.
    bool m_have_size;

    // How many bytes have been received. If m_have_size is false, it's
    // bytes of size; otherwise bytes of buffer.
    uint32_t m_received;

    // Buffer capacity.
    uint32_t m_capacity;

    IncomingBuffer(int fd)
        : m_fd(fd), m_buffer(nullptr), m_size(0), m_have_size(false), m_received(0), m_capacity(0) {

        // Nothing.
    }

    virtual ~IncomingBuffer() {
        delete[] m_buffer;
    }

    // Get the message. Assumes that need_receive() is false. Returns whether successful.
    bool get_message(google::protobuf::Message &message) {
        return message.ParseFromArray(m_buffer, m_size);
    }

    // Get ready for the next message.
    void reset() {
        m_size = 0;
        m_have_size = false;
        m_received = 0;
    }

    // Whether we want to receive more bytes for this message.
    bool need_receive() const {
        return !m_have_size || m_received < m_size;
    }

    // Receive as many bytes as we can. Returns whether successful. If not, sets errno.
    bool receive() {
        if (m_have_size) {
            int bytes_left = m_size - m_received;

            int received_here = recv(m_fd, m_buffer + m_received, bytes_left, 0);
            if (received_here == -1) {
                return false;
            }

            m_received += received_here;
        } else {
            // We don't yet have the size. Go for that.
            int bytes_left = sizeof(m_size) - m_received;

            int received_here = recv(m_fd, ((uint8_t *) &m_size) + m_received, bytes_left, 0);
            if (received_here == -1) {
                return false;
            }

            m_received += received_here;
            if (m_received == sizeof(m_size)) {
                m_size = ntohl(m_size);
                m_have_size = true;
                m_received = 0;

                // Grow buffer if necessary.
                if (m_capacity < m_size) {
                    delete[] m_buffer;
                    m_buffer = new uint8_t[m_size];
                    m_capacity = m_size;
                }
            }
        }

        return true;
    }
};

// Represents a remote worker. Stores our state for it.
class RemoteWorker {
public:
    enum State {
        // Initial states.
        SEND_WELCOME_REQUEST,
        RECEIVE_WELCOME_RESPONSE,

        // Waiting for assignment.
        IDLE,

        // Sending an execute command.
        SEND_EXECUTE_REQUEST,
        RECEIVE_EXECUTE_RESPONSE,
    };

    // Networking file descriptor.
    int m_fd;

    // Our current state in the state machine.
    State m_state;

    // User parameters.
    const Parameters &m_parameters;

    // Whatever frame we're working on, or -1 for none.
    int m_frame;

    // Buffer for outgoing and incoming messages.
    OutgoingBuffer m_outgoing_buffer;
    IncomingBuffer m_incoming_buffer;

    // Hostname of this remote machine. Empty if no one has connected yet.
    std::string m_hostname;

    RemoteWorker(int fd, const Parameters &parameters)
        : m_fd(fd), m_state(SEND_WELCOME_REQUEST), m_parameters(parameters), m_frame(-1),
            m_outgoing_buffer(fd), m_incoming_buffer(fd) {

        // Nothing.
    }

    // Get the structure for poll().
    struct pollfd get_pollfd() const {
        struct pollfd pollfd;

        pollfd.fd = m_fd;
        pollfd.events =
            (m_outgoing_buffer.need_send() ? POLLOUT : 0) |
            (m_incoming_buffer.need_receive() ? POLLIN : 0);
        pollfd.revents = 0;

        return pollfd;
    }

    // Send what we can.
    bool send() {
        return m_outgoing_buffer.send();
    }

    // Receive as many bytes as we can. Returns whether successful. If not, sets errno.
    bool receive() {
        bool success = m_incoming_buffer.receive();
        if (!success) {
            return success;
        }

        if (!m_incoming_buffer.need_receive()) {
            // We're done, decode it.
            dispatch();
        }

        return true;
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
            case SEND_WELCOME_REQUEST: {
                // Send welcome message.
                Drp::Request request;
                request.set_request_type(Drp::WELCOME);
                send_request(request, RECEIVE_WELCOME_RESPONSE);
                break;
            }

            case RECEIVE_WELCOME_RESPONSE: {
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
                send_request(request, RECEIVE_EXECUTE_RESPONSE);
                break;
            }

            case RECEIVE_EXECUTE_RESPONSE: {
                Drp::Response response;
                receive_response(response, Drp::EXECUTE);
                // std::cout << "status: " << response.execute_response().status() << "\n";
                m_state = IDLE;
                break;
            }
        }
    }

    void send_request(const Drp::Request &request, State next_state) {
        m_incoming_buffer.reset();
        m_outgoing_buffer.set_message(request);
        m_state = next_state;
    }

    void receive_response(Drp::Response &response, Drp::RequestType expected_request_type) {
        bool success = m_incoming_buffer.get_message(response);
        if (!success) {
            std::cout << "Can't decode buffer into message.\n";
            exit(1);
        }

        if (response.request_type() != expected_request_type) {
            std::cout << "Got unknown response type " << response.request_type() <<
                ", expected " << expected_request_type << "\n";
            exit(1);
        }

        // Reset for next time.
        m_incoming_buffer.reset();
    }
};

// Returns whether successful.
static bool copy_in(int sock, const Parameters &parameters, int frame) {
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
        if ((frame >= 0) == fileCopy.m_either_has_parameter) {
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
            if ((pollfds[i].revents & POLLIN) != 0) {
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
            if ((pollfds[i].revents & POLLOUT) != 0) {
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
