
#include <iostream>
#include <vector>

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
struct Parameters {
    Command m_command;
    std::string m_password;
    std::vector<std::string> m_proxies;
    std::vector<FileCopy> m_in_copies;
    std::vector<FileCopy> m_out_copies;

    Parameters()
        : m_command(CMD_UNSPECIFIED) {

        // Nothing.
    }
};

// Prints program usage to standard error.
static void usage() {
    std::cerr << "Usage: distrend {worker,proxy,controller} [FLAGS] [ARGUMENTS]\n";
    std::cerr << "Commands:\n";
    std::cerr << "    worker [FLAGS] HOST[:PORT]\n";
    std::cerr << "        The HOST is the hostname of either a proxy or a controller.\n";
    std::cerr << "        The PORT defaults to 1120.\n";
    std::cerr << "        --password PASSWORD Password to pass to proxies or the controller.\n";
    std::cerr << "                   Defaults to an empty string.\n";
    std::cerr << "    proxy [FLAGS]\n";
    std::cerr << "        --password PASSWORD Password to expect from workers or the controller.\n";
    std::cerr << "                   Defaults to an empty string.\n";
    std::cerr << "    controller [FLAGS] FRAMES EXEC [PARAMETERS...]\n";
    std::cerr << "        FRAMES is a frame range specification: FIRST[,LAST[,STEP]],\n";
    std::cerr << "        where STEP defaults to 1 or -1 (depending on order of FIRST and LAST)\n";
    std::cerr << "        and LAST defaults to FIRST.\n";
    std::cerr << "        EXEC is the executable to run on each worker.\n";
    std::cerr << "        PARAMETERS are the parameters to pass to the executed binary.\n";
    std::cerr << "        Use %d or %0Nd for the frame number, where N is a positive\n";
    std::cerr << "        decimal integer that specifies field width.\n";
    std::cerr << "        --proxy HOST        Proxy to connect to. Can be repeated.\n";
    std::cerr << "        --in LOCAL REMOTE   Copy LOCAL file to REMOTE file. Can be repeated.\n";
    std::cerr << "        --out REMOTE LOCAL  Copy REMOTE file to LOCAL file. Can be repeated.\n";
    std::cerr << "        --password PASSWORD Password to expect from workers or to pass\n";
    std::cerr << "                            to proxies. Defaults to an empty string.\n";
}

// Fills parameters and returns 0 on success; otherwise returns program exit status.
static int parse_arguments(int &argc, char **&argv, Parameters &parameters) {
    // Skip program name.
    argc--;
    argv++;

    while (argc > 0) {
        std::string arg(argv[0]);

        // Can specify these anytime.
        if (arg == "--help" || arg == "-h" || arg == "help") {
            usage();
            return 2;
        }

        // Command must be the first parameter.
        if (parameters.m_command == CMD_UNSPECIFIED) {
            if (arg == "worker") {
                parameters.m_command = CMD_WORKER;
            } else if (arg == "proxy") {
                parameters.m_command = CMD_PROXY;
            } else if (arg == "controller") {
                parameters.m_command = CMD_CONTROLLER;
            } else {
                std::cerr << "Command must be the first parameter.\n";
                usage();
                return 1;
            }

            argc--;
            argv++;
        } else {
            if (!arg.empty() && arg[0] == '-') {
                if (arg == "--password") {
                    if (argc == 1) {
                        std::cerr << "Must specify password with --password flag.\n";
                        usage();
                        return 1;
                    }

                    parameters.m_password = argv[1];

                    argc -= 2;
                    argv += 2;
                } else if (arg == "--proxy") {
                    if (parameters.m_command != CMD_CONTROLLER) {
                        std::cerr << "The --proxy flag is only valid with the controller command.\n";
                        usage();
                        return 1;
                    }
                    if (argc == 1) {
                        std::cerr << "Must specify proxy with --proxy flag.\n";
                        usage();
                        return 1;
                    }

                    parameters.m_proxies.push_back(argv[1]);

                    argc -= 2;
                    argv += 2;
                } else if (arg == "--in") {
                    if (parameters.m_command != CMD_CONTROLLER) {
                        std::cerr << "The --in flag is only valid with the controller command.\n";
                        usage();
                        return 1;
                    }
                    if (argc <= 2) {
                        std::cerr << "Must specify two pathnames with --in flag.\n";
                        usage();
                        return 1;
                    }

                    parameters.m_in_copies.push_back(FileCopy(argv[1], argv[2]));

                    argc -= 3;
                    argv += 3;
                } else if (arg == "--out") {
                    if (parameters.m_command != CMD_CONTROLLER) {
                        std::cerr << "The --out flag is only valid with the controller command.\n";
                        usage();
                        return 1;
                    }
                    if (argc <= 2) {
                        std::cerr << "Must specify two pathnames with --out flag.\n";
                        usage();
                        return 1;
                    }

                    parameters.m_out_copies.push_back(FileCopy(argv[1], argv[2]));

                    argc -= 3;
                    argv += 3;
                } else {
                    std::cerr << "Unknown flag " << arg << "\n";
                    usage();
                    return 1;
                }
            }
        }
    }

    if (parameters.m_command == CMD_UNSPECIFIED) {
        std::cerr << "Must specify command.\n";
        usage();
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    // Parse command-line parameters.
    Parameters parameters;
    int status = parse_arguments(argc, argv, parameters);
    if (status != 0) {
        return status;
    }

    return 0;
}
