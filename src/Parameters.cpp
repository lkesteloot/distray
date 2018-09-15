
#include <deque>
#include <stdexcept>
#include <cstring>

#include "Parameters.hpp"

// Helper class to deal with list of arguments.
class Arguments {
    std::deque<std::string> m_args;

public:
    Arguments(int argc, char *argv[]) {
        for (int i = 0; i < argc; i++) {
            m_args.push_back(argv[i]);
        }
    }

    // Whether there are any arguments left.
    bool no_more() const {
        return m_args.empty();
    }

    // Return the next argument.
    std::string next() {
        if (m_args.empty()) {
            throw std::logic_error("should have checked empty first");
        }

        std::string arg = m_args.front();
        m_args.pop_front();
        return arg;
    }

    // Whether we have at least this many left.
    bool has_at_least(int count) const {
        return m_args.size() >= count;
    }

    // Whether we have exactly this many left.
    bool has_exactly(int count) const {
        return m_args.size() == count;
    }

    // Whether the next argument is a flag.
    bool next_is_flag() const {
        return has_at_least(1) && !m_args[0].empty() && m_args[0][0] == '-';
    }

    // Move the rest of the arguments to the vector.
    void fill_from_rest(std::vector<std::string> &v) {
        while (!no_more()) {
            v.push_back(next());
        }
    }
};

// Prints program usage to standard error.
void Parameters::usage() const {
    std::cerr << "Usage: distrend {worker,proxy,controller} [FLAGS] [ARGUMENTS]\n";
    std::cerr << "\n";
    std::cerr << "Commands:\n";
    std::cerr << "\n";
    std::cerr << "    worker [FLAGS] URL\n";
    std::cerr << "        The URL of either a proxy or a controller (tcp://HOSTNAME:PORT).\n";
    std::cerr << "        --password PASSWORD Password to pass to proxies or the controller.\n";
    std::cerr << "                   Defaults to an empty string.\n";
    std::cerr << "\n";
    std::cerr << "    proxy [FLAGS]\n";
    std::cerr << "        --password     PASSWORD Password to expect from workers or the\n";
    std::cerr << "                       controller. Defaults to an empty string.\n";
    std::cerr << "        --listen URL   URL to listen on [tcp://0.0.0.0:1120].\n";
    std::cerr << "\n";
    std::cerr << "    controller [FLAGS] FRAMES EXEC [PARAMETERS...]\n";
    std::cerr << "        FRAMES is a frame range specification: FIRST[,LAST[,STEP]],\n";
    std::cerr << "        where STEP defaults to 1 or -1 (depending on order of FIRST and LAST)\n";
    std::cerr << "        and LAST defaults to FIRST.\n";
    std::cerr << "        EXEC is the executable to run on each worker.\n";
    std::cerr << "        PARAMETERS are the parameters to pass to the executed binary.\n";
    std::cerr << "        Use %d or %0Nd for the frame number, where N is a positive\n";
    std::cerr << "        decimal integer that specifies field width.\n";
    std::cerr << "        --proxy URL         Proxy URL to connect to. Can be repeated.\n";
    std::cerr << "        --in LOCAL REMOTE   Copy LOCAL file to REMOTE file. Can be repeated.\n";
    std::cerr << "        --out REMOTE LOCAL  Copy REMOTE file to LOCAL file. Can be repeated.\n";
    std::cerr << "        --password PASSWORD Password to expect from workers or to pass\n";
    std::cerr << "                            to proxies. Defaults to an empty string.\n";
    std::cerr << "        --listen URL        URL to listen on [tcp://0.0.0.0:1120].\n";
}

// Fills parameters and returns 0 on success; otherwise returns program exit status.
int Parameters::parse_arguments(int argc, char *argv[]) {
    Arguments args(argc, argv);

    // Skip program name.
    args.next();
    if (args.no_more()) {
        usage();
        return 2;
    }

    // Parse command.
    std::string arg = args.next();
    if (arg == "--help" || arg == "-h" || arg == "help") {
        usage();
        return 2;
    }

    if (arg == "worker") {
        m_command = CMD_WORKER;
    } else if (arg == "proxy") {
        m_command = CMD_PROXY;
    } else if (arg == "controller") {
        m_command = CMD_CONTROLLER;
    } else if (arg == "unittest") {
        m_command = CMD_UNITTEST;
    } else {
        std::cerr << "Command must be the first parameter.\n";
        return 1;
    }

    // Default value for incoming URL.
    if (m_command == CMD_PROXY || m_command == CMD_CONTROLLER) {
        // Listen on all IPv4 interfaces.
        m_url = "tcp://0.0.0.0:1120";
    }

    while (args.next_is_flag()) {
        arg = args.next();

        if (arg == "--password") {
            if (args.has_at_least(1)) {
                m_password = args.next();
            } else {
                std::cerr << "Must specify password with --password flag.\n";
                return 1;
            }
        } else if (arg == "--proxy") {
            if (m_command != CMD_CONTROLLER) {
                std::cerr << "The --proxy flag is only valid with the controller command.\n";
                return 1;
            }
            if (args.has_at_least(1)) {
                m_proxy_urls.push_back(args.next());
            } else {
                std::cerr << "Must specify proxy URL with --proxy flag.\n";
                return 1;
            }
        } else if (arg == "--in" || arg == "--out") {
            if (m_command != CMD_CONTROLLER) {
                std::cerr << "The " << arg <<
                    " flag is only valid with the controller command.\n";
                return 1;
            }
            if (args.has_at_least(2)) {
                std::string source = args.next();
                std::string destination = args.next();

                std::vector<FileCopy> &copies = arg == "--in" ? m_in_copies : m_out_copies;
                copies.push_back(FileCopy(source, destination));
            } else {
                std::cerr << "Must specify two pathnames with " << arg << " flag.\n";
                return 1;
            }
        } else if (arg == "--listen") {
            if (m_command == CMD_WORKER) {
                std::cerr << "The --listen flag is not valid for the worker command.\n";
                return 1;
            }
            if (args.has_at_least(1)) {
                m_url = args.next();
            } else {
                std::cerr << "Must specify listen URL with --listen flag.\n";
                return 1;
            }
        } else {
            std::cerr << "Unknown flag " << arg << "\n";
            return 1;
        }
    }

    // Parse non-flag parameters.
    if (m_command == CMD_WORKER) {
        if (args.has_exactly(1)) {
            m_url = args.next();
        } else {
            std::cerr << "The worker command must specify the URL to connect to.\n";
            return 1;
        }
    } else if (m_command == CMD_PROXY) {
        if (!args.no_more()) {
            std::cerr << "The proxy command takes no parameters.\n";
            return 1;
        }
    } else if (m_command == CMD_CONTROLLER) {
        if (!args.has_at_least(2)) {
            std::cerr << "The controller command must specify the frames and the program to run.\n";
            return 1;
        }

        // Parse frame range.
        bool success = m_frames.parse(args.next());
        if (!success) {
            return 1;
        }

        // Main executable name.
        m_executable = args.next();

        // Eat up the rest.
        args.fill_from_rest(m_arguments);
    } else if (m_command == CMD_UNITTEST) {
        if (!args.no_more()) {
            std::cerr << "The unittest command takes no parameters.\n";
            return 1;
        }
    } else {
        throw std::logic_error("can't get here");
    }

    return 0;
}

