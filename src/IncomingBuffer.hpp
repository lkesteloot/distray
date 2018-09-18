#ifndef INCOMING_BUFFER_HPP
#define INCOMING_BUFFER_HPP

#include <sys/socket.h>
#include <google/protobuf/message.h>

// Buffer that accumulates bytes until enough are ready to receive a message.
class IncomingBuffer {
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

public:
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

    // Whether we want to receive more bytes for this message. If this returns
    // false then the message is ready to be decoded with get_message().
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
            } else if (received_here == 0) {
                // XXX handle.
                std::cerr << "IncomingBuffer got " << received_here << " instead of " << bytes_left << "\n";
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
                // XXX Fail if size is too large (> 10 MB?).
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

#endif // INCOMING_BUFFER_HPP
