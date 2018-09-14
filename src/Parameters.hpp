#ifndef PARAMETERS_HPP
#define PARAMETERS_HPP

#include <iostream>
#include <vector>

#include "Frames.hpp"

// Default port we listen to or connect to.
static const int DEFAULT_PORT = 1120;

// Command that we're running.
enum Command {
    CMD_UNSPECIFIED,
    CMD_WORKER,
    CMD_PROXY,
    CMD_CONTROLLER,
};

// A file copy (in or out).
struct FileCopy {
    std::string m_source;
    std::string m_destination;

    FileCopy(const std::string &source, const std::string &destination)
        : m_source(source), m_destination(destination) {

        // Nothing.
    }
};

// All command-line parameters.
class Parameters {
public:
    // Command we're running.
    Command m_command;

    // For all commands.
    std::string m_password;

    // For CMD_WORKER.
    std::string m_hostname;
    int m_port;

    // For CMD_PROXY.
    // (None.)

    // For CMD_CONTROLLER.
    std::vector<std::string> m_proxies;
    std::vector<FileCopy> m_in_copies;
    std::vector<FileCopy> m_out_copies;
    Frames m_frames;
    std::string m_executable;
    std::vector<std::string> m_arguments;

    Parameters()
        : m_command(CMD_UNSPECIFIED), m_port(DEFAULT_PORT) {

        // Nothing.
    }

    void usage() const;
    int parse_arguments(int argc, char *argv[]);

private:
    // Parses the string into a "host:port" pair, storing the results
    // in the m_hostname and m_port fields. The host is optional. Returns
    // whether successful. Does not validate that a hostname has the valid
    // syntax.
    bool parse_host_and_port(const std::string &host_and_port);
};

#endif // PARAMETERS_HPP
