#ifndef FRAMES_HPP
#define FRAMES_HPP

#include <deque>

// Frame sequence specification.
class Frames {
public:
    int m_first;
    int m_last;
    int m_step;

    // Format is "first[,last[,step]]". Returns whether successful. If not,
    // also prints error to standard error.
    bool parse(const std::string &spec);

    // Whether this frame is past the end (taking step into account).
    bool is_done(int frame) const {
        return m_step > 0 ? frame > m_last : frame < m_last;
    }

    // Get all frames, in order from first to last.
    std::deque<int> get_all() const;
};

#endif // FRAMES_HPP
