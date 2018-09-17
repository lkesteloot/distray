#ifndef OUTGOING_BUFFER_HPP
#define OUTGOING_BUFFER_HPP

#include <sys/socket.h>

// Represents data that needs to be sent asynchronously.
class OutgoingBuffer {
    // File descriptor we're sending on.
    int m_fd;

    // Buffer includes size header.
    uint8_t *m_buffer;

    // Size includes size header.
    uint32_t m_size;

    // How many bytes have been sent.
    uint32_t m_sent;

public:
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

#endif // OUTGOING_BUFFER_HPP
