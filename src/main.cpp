
#include <stdexcept>

#include "Parameters.hpp"
#include "worker.hpp"
#include "proxy.hpp"
#include "controller.hpp"
#include "unittest.hpp"

int main(int argc, char **argv) {
    // Parse command-line parameters.
    Parameters parameters;
    int status = parameters.parse_arguments(argc, argv);
    if (status != 0) {
        return status;
    }

    // Run the command.
    switch (parameters.m_command) {
        case CMD_WORKER:
            status = start_worker(parameters);
            break;

        case CMD_PROXY:
            status = start_proxy(parameters);
            break;

        case CMD_CONTROLLER:
            status = start_controller(parameters);
            break;

        case CMD_UNITTEST:
            status = start_unittests(parameters);
            break;

        case CMD_UNSPECIFIED:
            throw std::logic_error("can't get here");
    }

    return status;
}
