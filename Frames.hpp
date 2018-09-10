#ifndef FRAMES_HPP
#define FRAMES_HPP

// Frame sequence specification.
class Frames {
public:
    int m_first;
    int m_last;
    int m_step;

    // Format is "first[,last[,step]]". Returns whether successful. If not,
    // also prints error to standard error.
    bool parse(const std::string &spec);
};

#endif // FRAMES_HPP
