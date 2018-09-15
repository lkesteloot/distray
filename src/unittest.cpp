
#include "unittest.hpp"
#include "util.hpp"

struct HasPathname {
    std::string m_pathname;
    bool m_expected;
};

static HasPathname m_has_pathname[] = {
    { "image.png", false },
    { "image-%d.png", true },
    { "image-%03d.png", true },
    { "image-%3d.png", false },
    { "image-%f.png", false },
    { "image-%d%d.png", true },
    { "image-% 3d.png", false },
};

static int test_pathnames() {
    for (int i = 0; i < sizeof(m_has_pathname)/sizeof(m_has_pathname[0]); i++) {
        const HasPathname &hp = m_has_pathname[i];

        std::cerr << hp.m_pathname << ": ";

        bool actual = pathname_has_parameter(hp.m_pathname);
        if (actual == hp.m_expected) {
            std::cerr << "pass\n";
        } else {
            std::cerr << "FAIL\n";
            return 1;
        }
    }

    return 0;
}

int start_unittests(const Parameters &parameters) {
    int status = 0;

    status |= test_pathnames();

    if (status == 0) {
        std::cout << "\nAll tests passed.\n";
    } else {
        std::cout << "\nTESTS FAILED.\n";
    }

    return status;
}
