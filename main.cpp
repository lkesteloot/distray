
#include "Parameters.hpp"

int main(int argc, char **argv) {
    // Parse command-line parameters.
    Parameters parameters;
    int status = parameters.parse_arguments(argc, argv);
    if (status != 0) {
        return status;
    }

    return 0;
}
