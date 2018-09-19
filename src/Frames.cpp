
#include <stdlib.h>
#include <iostream>
#include <stdexcept>

#include "Frames.hpp"

// Parse a number at s, moving it forward. Throws std::invalid_argument()
// if no number can be parsed.
static int parse_int(const char *&s) {
    char *e;

    int value = strtol(s, &e, 10);
    if (s == e) {
        throw std::invalid_argument(s);
    }

    // Move forward.
    s = e;

    return value;
}

bool Frames::parse(const std::string &spec) {
    const char *s = spec.c_str();

    try {
        m_first = parse_int(s);

        if (*s == ',') {
            s += 1;
            m_last = parse_int(s);

            if (*s == ',') {
                s += 1;
                m_step = parse_int(s);
            } else {
                // Auto-compute step.
                m_step = m_first <= m_last ? 1 : -1;
            }
        } else {
            // One-frame range.
            m_last = m_first;
            m_step = 1;
        }
    } catch (std::invalid_argument e) {
        std::cerr << "Invalid number in frame specification: " << e.what() << "\n";
        return false;
    }

    // Must have eaten up entire string.
    if (*s != '\0') {
        std::cerr << "Cannot parse frame specification: " << spec << "\n";
        return false;
    }

    return true;
}

std::deque<int> Frames::get_all() const {
    std::deque<int> v;

    for (int frame = m_first; !is_done(frame); frame += m_step) {
        v.push_back(frame);
    }

    return v;
}
