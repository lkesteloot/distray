
#include "unittest.hpp"
#include "util.hpp"

struct PathnameHasParameter {
    std::string m_pathname;
    bool m_expected;
};

static std::vector<PathnameHasParameter> m_pathname_has_parameter = {
    { "image.png", false },
    { "image-%d.png", true },
    { "image-%03d.png", true },
    { "image-%3d.png", false },
    { "image-%f.png", false },
    { "image-%d%d.png", true },
    { "image-% 3d.png", false },
};

static int test_pathname_has_parameter() {
    std::cerr << "test_pathname_has_parameter:\n";

    for (PathnameHasParameter &p : m_pathname_has_parameter) {
        std::cerr << "    " << p.m_pathname << ": ";

        bool actual = pathname_has_parameter(p.m_pathname);
        if (actual == p.m_expected) {
            std::cerr << "pass\n";
        } else {
            std::cerr << "FAIL\n";
            return 1;
        }
    }

    return 0;
}

struct IsPathnameLocal {
    std::string m_pathname;
    bool m_expected;
};

static std::vector<IsPathnameLocal> m_is_pathname_local = {
    { "image.png", true },
    { "./image.png", true },
    { "foo/bar/./image.png", true },
    { "../image.png", false },
    { "foo/bar/../image.png", false },
    { "/image.png", false },
};

static int test_is_pathname_local() {
    std::cerr << "test_is_pathname_local:\n";

    for (IsPathnameLocal &p : m_is_pathname_local) {
        std::cerr << "    " << p.m_pathname << ": ";

        bool actual = is_pathname_local(p.m_pathname);
        if (actual == p.m_expected) {
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

    status |= test_pathname_has_parameter();
    status |= test_is_pathname_local();

    if (status == 0) {
        std::cout << "\nAll tests passed.\n";
    } else {
        std::cout << "\nTESTS FAILED.\n";
    }

    return status;
}
