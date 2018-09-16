
#include "RemoteWorker.hpp"

void RemoteWorker::dispatch() {
    do {
        std::cout << "RemoteWorker: state = " << m_state << "\n";

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
                m_state_index = 0;
                m_state = SEND_COPY_IN_NON_FRAME_FILE;
                break;
            }

            case SEND_COPY_IN_NON_FRAME_FILE: {
                copy_file_in(-1, RECEIVE_COPY_IN_NON_FRAME_FILE, IDLE);
                break;
            }

            case RECEIVE_COPY_IN_NON_FRAME_FILE: {
                Drp::Response response;
                receive_response(response, Drp::COPY_IN);
                // XXX check copy status.
                m_state_index++;
                m_state = SEND_COPY_IN_NON_FRAME_FILE;
                break;
            }

            case IDLE: {
                // We're never dispatched in idle mode.
                std::cout << "Should not be in IDLE.\n";
                exit(1);
            }

            case SEND_COPY_IN_FRAME_FILE: {
                copy_file_in(m_frame, RECEIVE_COPY_IN_FRAME_FILE, SEND_EXECUTE_REQUEST);
                break;
            }

            case RECEIVE_COPY_IN_FRAME_FILE: {
                Drp::Response response;
                receive_response(response, Drp::COPY_IN);
                // XXX check copy status.
                m_state_index++;
                m_state = SEND_COPY_IN_FRAME_FILE;
                break;
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
                // XXX check status.
                // std::cout << "status: " << response.execute_response().status() << "\n";
                m_state = SEND_COPY_OUT_FRAME_FILE;
                m_state_index = 0;
                break;
            }

            case SEND_COPY_OUT_FRAME_FILE: {
                copy_file_out(m_frame, RECEIVE_COPY_OUT_FRAME_FILE, IDLE);
                break;
            }

            case RECEIVE_COPY_OUT_FRAME_FILE: {
                Drp::Response response;
                receive_response(response, Drp::COPY_OUT);
                handle_copy_file_out_response(response, m_frame,
                        m_parameters.m_out_copies[m_state_index]);
                m_state_index++;
                m_state = SEND_COPY_OUT_FRAME_FILE;
                break;
            }

            case SEND_COPY_OUT_NON_FRAME_FILE: {
                break;
            }

            case RECEIVE_COPY_OUT_NON_FRAME_FILE: {
                break;
            }

            case DONE: {
                break;
            }
        }
    } while (m_state == SEND_WELCOME_REQUEST
            || m_state == SEND_COPY_IN_NON_FRAME_FILE
            || m_state == SEND_COPY_IN_FRAME_FILE
            || m_state == SEND_EXECUTE_REQUEST
            || m_state == SEND_COPY_OUT_FRAME_FILE
            || m_state == SEND_COPY_OUT_NON_FRAME_FILE);
}

void RemoteWorker::copy_file_in(int frame, State receive_state, State next_state) {
    if (m_state_index < m_parameters.m_in_copies.size()) {
        const FileCopy &fileCopy = m_parameters.m_in_copies[m_state_index];
        if ((frame >= 0) == fileCopy.has_parameter()) {
            // Send file.
            Drp::Request request;
            request.set_request_type(Drp::COPY_IN);
            Drp::CopyInRequest *copy_in_request = request.mutable_copy_in_request();
            std::string source_pathname = substitute_parameter(fileCopy.m_source, frame);
            std::string destination_pathname = substitute_parameter(fileCopy.m_destination, frame);
            std::cout << "Copying in " << source_pathname << " to " << destination_pathname << "\n";
            copy_in_request->set_pathname(destination_pathname);
            try {
                copy_in_request->set_content(read_file(source_pathname));
            } catch (std::runtime_error e) {
                std::cerr << "Error reading file " << source_pathname << "\n";
                exit(-1);
            }
            send_request(request, receive_state);
        } else {
            m_state_index++;
        }
    } else {
        m_state = next_state;
    }
}

void RemoteWorker::copy_file_out(int frame, State receive_state, State next_state) {
    if (m_state_index < m_parameters.m_out_copies.size()) {
        const FileCopy &fileCopy = m_parameters.m_out_copies[m_state_index];
        if ((frame >= 0) == fileCopy.has_parameter()) {
            // Ask for file.
            Drp::Request request;
            request.set_request_type(Drp::COPY_OUT);
            Drp::CopyOutRequest *copy_out_request = request.mutable_copy_out_request();
            std::string source_pathname = substitute_parameter(fileCopy.m_source, frame);
            std::string destination_pathname = substitute_parameter(fileCopy.m_destination, frame);
            std::cout << "Copying out " << source_pathname << " to " << destination_pathname << "\n";
            copy_out_request->set_pathname(destination_pathname);
            send_request(request, receive_state);
        } else {
            m_state_index++;
        }
    } else {
        m_state = next_state;
    }
}

void RemoteWorker::handle_copy_file_out_response(const Drp::Response &response, int frame,
        const FileCopy &fileCopy) {

    if (!response.copy_out_response().success()) {
        // XXX handle.
        std::cout << "copy out success: " << response.copy_out_response().success() << "\n";
        exit(-1);
    }

    bool success = write_file(substitute_parameter(fileCopy.m_destination, frame),
            response.copy_out_response().content());
    if (!success) {
        // XXX handle.
        std::cout << "Could not write local file " << fileCopy.m_destination << "\n";
        exit(-1);
    }
}
