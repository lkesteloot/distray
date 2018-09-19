#ifndef REMOTE_WORKER_HPP
#define REMOTE_WORKER_HPP

#include <poll.h>

#include "Drp.pb.h"
#include "Parameters.hpp"
#include "OutgoingBuffer.hpp"
#include "IncomingBuffer.hpp"

// Represents a remote worker. Stores our state for it.
class RemoteWorker {
public:
    enum State {
        // Initial states.
        SEND_WELCOME_REQUEST,
        RECEIVE_WELCOME_RESPONSE,

        // Copy in non-frame files.
        SEND_COPY_IN_NON_FRAME_FILE,
        RECEIVE_COPY_IN_NON_FRAME_FILE,

        // Waiting for assignment.
        IDLE,

        // Copy in frame files.
        SEND_COPY_IN_FRAME_FILE,
        RECEIVE_COPY_IN_FRAME_FILE,

        // Sending an execute command.
        SEND_EXECUTE_REQUEST,
        RECEIVE_EXECUTE_RESPONSE,

        // Copy out frame files.
        SEND_COPY_OUT_FRAME_FILE,
        RECEIVE_COPY_OUT_FRAME_FILE,

        // Copy in non-frame files.
        SEND_COPY_OUT_NON_FRAME_FILE,
        RECEIVE_COPY_OUT_NON_FRAME_FILE,

        // Finished with this worker.
        DONE,
    };

    // Networking file descriptor.
    int m_fd;

    // Our current state in the state machine.
    State m_state;

    // Part of our state: What file we're copying. This points to the
    // next index to do (e.g., the next index in m_in_copies).
    int m_state_index;

    // User parameters.
    const Parameters &m_parameters;

    // Whatever frame we're working on, or -1 for none.
    int m_frame;

    // Index of proxy we're blocked for, or -1 for none.
    int m_proxy_index;

    // Buffer for outgoing and incoming messages.
    OutgoingBuffer m_outgoing_buffer;
    IncomingBuffer m_incoming_buffer;

    // Hostname of this remote machine. Empty if no one has connected yet.
    std::string m_hostname;

    RemoteWorker(int fd, const Parameters &parameters)
        : m_fd(fd), m_state(SEND_WELCOME_REQUEST), m_state_index(0), m_parameters(parameters),
            m_frame(-1), m_proxy_index(-1), m_outgoing_buffer(fd), m_incoming_buffer(fd) {

        // Nothing.
    }

    // The frame we were assigned to work on.
    int get_frame() const {
        return m_frame;
    }

    // Get the hostname. Might be empty if we've not gotten a welcome response.
    const std::string &hostname() const {
        return m_hostname;
    }

    // Set the index of the proxy (in the m_proxy_urls list) we're blocked for.
    void set_proxy_index(int proxy_index) {
        m_proxy_index = proxy_index;
    }

    // Get the index of the proxy (in the m_proxy_urls list) we're blocked for.
    int get_proxy_index() const {
        return m_proxy_index;
    }

    // Fill the structure for poll().
    void fill_pollfd(struct pollfd &pollfd) const {
        pollfd.fd = m_fd;
        pollfd.events =
            (m_outgoing_buffer.need_send() ? POLLOUT : 0) |
            (m_incoming_buffer.need_receive() ? POLLIN : 0);
        pollfd.revents = 0;
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
        m_state = SEND_COPY_IN_FRAME_FILE;
        m_state_index = 0;
        dispatch();
    }

private:
    // Move the state machine forward.
    void dispatch();

    // Send a file. Frame is -1 for non-frame files.
    void copy_file_in(int frame, State receive_state, State next_state);
    void copy_file_out(int frame, State receive_state, State next_state);
    void handle_copy_file_out_response(const Drp::Response &response, int frame,
            const FileCopy &fileCopy);

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
            std::cout << "Got response type " << response.request_type() <<
                ", expected " << expected_request_type << "\n";
            exit(1);
        }

        // Reset for next time.
        m_incoming_buffer.reset();
    }
};

#endif // REMOTE_WORKER_HPP
