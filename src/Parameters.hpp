#ifndef PARAMETERS_HPP
#define PARAMETERS_HPP

#include <iostream>
#include <vector>

#include "Frames.hpp"
#include "util.hpp"

// Default ports we listen to or connect to.
static const int DEFAULT_WORKER_PORT = 1120;
static const int DEFAULT_CONTROLLER_PORT = 1121;

// Command that we're running.
enum Command {
    CMD_UNSPECIFIED,
    CMD_WORKER,
    CMD_PROXY,
    CMD_CONTROLLER,
    CMD_UNITTEST,
};

// A file copy (in or out).
struct FileCopy {
    std::string m_source;
    std::string m_destination;
    bool m_source_has_parameter;
    bool m_destination_has_parameter;

    FileCopy(const std::string &source, const std::string &destination)
        : m_source(source), m_destination(destination) {

        m_source_has_parameter = string_has_parameter(m_source);
        m_destination_has_parameter = string_has_parameter(m_destination);
    }

    // If either source or destination has a parameter.
    bool has_parameter() const {
        return m_source_has_parameter || m_destination_has_parameter;
    }
};

// Represents both an endpoint string (like "example.com:1120") and its
// parsed and looked-up address.
class Endpoint {
public:
    std::string m_endpoint;
    struct sockaddr_in m_sockaddr;

    Endpoint() {
        // Nothing.
    }

    // Returns whether successful.
    bool set(const std::string &endpoint, bool is_server,
            const std::string &default_hostname, int default_port) {

        m_endpoint = endpoint;

        return parse_and_lookup_endpoint(m_endpoint, is_server,
                default_hostname, default_port, m_sockaddr);
    }
};

// All command-line parameters.
class Parameters {
public:
    // Command we're running.
    Command m_command;

    // For all commands.
    std::string m_password;

    // For CMD_WORKER (outgoing) and CMD_CONTROLLER (incoming).
    Endpoint m_endpoint;

    // For CMD_PROXY.
    Endpoint m_worker_endpoint;
    Endpoint m_controller_endpoint;

    // For CMD_CONTROLLER.
    std::vector<Endpoint> m_proxy_endpoints;
    std::vector<FileCopy> m_in_copies;
    std::vector<FileCopy> m_out_copies;
    Frames m_frames;
    std::string m_executable;
    std::vector<std::string> m_arguments;

    Parameters()
        : m_command(CMD_UNSPECIFIED) {

        // Nothing.
    }

    void usage() const;
    int parse_arguments(int argc, char *argv[]);
};

#endif // PARAMETERS_HPP
