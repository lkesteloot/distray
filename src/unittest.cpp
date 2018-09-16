
#include <iostream>
#include <vector>

#include "unittest.hpp"
#include "util.hpp"

struct PathnameHasParameter {
    std::string m_str;
    bool m_expected;
};

static std::vector<PathnameHasParameter> m_has_parameter = {
    { "image.png", false },
    { "image-%d.png", true },
    { "image-%03d.png", true },
    { "image-%3d.png", false },
    { "image-%f.png", false },
    { "image-%d%d.png", true },
    { "image-% 3d.png", false },
};

static int test_has_parameter() {
    std::cerr << "test_has_parameter:\n";

    for (PathnameHasParameter &p : m_has_parameter) {
        std::cerr << "    " << p.m_str << ": ";

        bool actual = string_has_parameter(p.m_str);
        if (actual == p.m_expected) {
            std::cerr << "pass\n";
        } else {
            std::cerr << "FAIL\n";
            return 1;
        }
    }

    return 0;
}

struct SubstituteParameter {
    std::string m_str;
    int m_parameter;
    std::string m_expected;
};

static std::vector<SubstituteParameter> m_substitute_parameter {
    { "", 0, "" },
    { "no parameter", 0, "no parameter" },
    { "%d", 123, "123" },
    { "%d", -1, "%d" },
    { "%05d", 123, "00123" },
    { "abc%ddef", 123, "abc123def" },
    { "abc%05ddef", 123, "abc00123def" },
    { "abc%05ddef%dghi", 123, "abc00123def123ghi" },
    { "%5d", 123, "%5d" },
    { "%g", 123, "%g" },
    { "%%", 123, "%%" },
};

static int test_substitute_parameter() {
std::string substitute_parameter(const std::string &str, int value);
    std::cerr << "test_substitute_parameter:\n";

    for (SubstituteParameter &p : m_substitute_parameter) {
        std::cerr << "    " << p.m_str << ": ";

        std::string actual = substitute_parameter(p.m_str, p.m_parameter);
        if (actual == p.m_expected) {
            std::cerr << "pass\n";
        } else {
            std::cerr << "FAIL (" << actual << " instead of " << p.m_expected << ")\n";
            return 1;
        }
    }

    return 0;
}

struct IsPathnameLocal {
    std::string m_str;
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
        std::cerr << "    " << p.m_str << ": ";

        bool actual = is_pathname_local(p.m_str);
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

    status |= test_has_parameter();
    status |= test_substitute_parameter();
    status |= test_is_pathname_local();

    if (status == 0) {
        std::cout << "\nAll tests passed.\n";
    } else {
        std::cout << "\nTESTS FAILED.\n";
    }

    return status;
}
