
#include <iostream>
#include <vector>

#include "unittest.hpp"
#include "util.hpp"

// Color escapes.
static const char *PASS = "\033[32m";
static const char *FAIL = "\033[31m";
static const char *NEUTRAL = "\033[0m";

// ------------------------------------------------------------------------------------------

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

static bool test_has_parameter() {
    std::cerr << "test_has_parameter:\n";

    for (PathnameHasParameter &p : m_has_parameter) {
        std::cerr << "    " << p.m_str << ": ";

        bool actual = string_has_parameter(p.m_str);
        if (actual == p.m_expected) {
            std::cerr << PASS << "pass" << NEUTRAL << "\n";
        } else {
            std::cerr << FAIL << "FAIL" << NEUTRAL << "\n";
            return false;
        }
    }

    return true;
}

// ------------------------------------------------------------------------------------------

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

static bool test_substitute_parameter() {
std::string substitute_parameter(const std::string &str, int value);
    std::cerr << "test_substitute_parameter:\n";

    for (SubstituteParameter &p : m_substitute_parameter) {
        std::cerr << "    " << p.m_str << ": ";

        std::string actual = substitute_parameter(p.m_str, p.m_parameter);
        if (actual == p.m_expected) {
            std::cerr << PASS << "pass" << NEUTRAL << "\n";
        } else {
            std::cerr << FAIL << "FAIL (" << actual << " instead of "
                << p.m_expected << ")" << NEUTRAL << "\n";
            return false;
        }
    }

    return true;
}

// ------------------------------------------------------------------------------------------

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

static bool test_is_pathname_local() {
    std::cerr << "test_is_pathname_local:\n";

    for (IsPathnameLocal &p : m_is_pathname_local) {
        std::cerr << "    " << p.m_str << ": ";

        bool actual = is_pathname_local(p.m_str);
        if (actual == p.m_expected) {
            std::cerr << PASS << "pass" << NEUTRAL << "\n";
        } else {
            std::cerr << FAIL << "FAIL" << NEUTRAL << "\n";
            return false;
        }
    }

    return true;
}

// ------------------------------------------------------------------------------------------

struct ParseEndpoint {
    std::string m_endpoint;
    std::string m_default_hostname;
    int m_default_port;
    bool m_success;
    std::string m_hostname;
    int m_port;
};

static std::vector<ParseEndpoint> m_parse_endpoint = {
    // Empty string is just defaults.
    { "", "foo", 1120, true, "foo", 1120 },
    // Can override hostname.
    { "bar", "foo", 1120, true, "bar", 1120 },
    // Can override port.
    { "9999", "foo", 1120, true, "foo", 9999 },
    // Can override both.
    { "bar:9999", "foo", 1120, true, "bar", 9999 },
    { ":9999", "foo", 1120, true, "", 9999 },
    // Bad port.
    { "bar:xyz", "foo", 1120, false, "", 0 },
};

static bool test_parse_endpoint() {
    std::cerr << "test_parse_endpoint:\n";

    for (ParseEndpoint &p : m_parse_endpoint) {
        std::string hostname;
        int port;

        std::cerr << "    " << p.m_endpoint << ": ";

        bool success = parse_endpoint(p.m_endpoint,
                p.m_default_hostname, p.m_default_port,
                hostname, port);
        if (success != p.m_success ||
                (success && (
                             hostname != p.m_hostname ||
                             port != p.m_port))) {

            std::cerr << FAIL << "FAIL: " << success << " " << hostname << " "
                << port << NEUTRAL << "\n";
            return false;
        } else {
            std::cerr << PASS << "pass" << NEUTRAL << "\n";
        }
    }

    return true;
}

// ------------------------------------------------------------------------------------------

struct DoDnsLookup {
    std::string m_hostname;
    int m_port;
    bool m_is_server;
    bool m_success;
    uint32_t m_address;
};

static std::vector<DoDnsLookup> m_do_dns_lookup = {
    { "localhost", 1120, false, true, 0x7F000001 },
    { "teamten.com", 80, false, true, 0x17EF04EB },
    { "", 80, false, true, 0x7F000001 },
    { "", 80, true, true, INADDR_ANY },
};

static bool test_do_dns_lookup() {
    std::cerr << "test_do_dns_lookup:\n";

    for (DoDnsLookup &p : m_do_dns_lookup) {
        std::cerr << "    " << p.m_hostname << ":" << p.m_port << ": ";

        struct sockaddr_in sockaddr;
        bool success = do_dns_lookup(p.m_hostname, p.m_port, p.m_is_server, sockaddr);

        if (success != p.m_success ||
                (success && (
                             sockaddr.sin_port != htons(p.m_port) ||
                             sockaddr.sin_addr.s_addr != htonl(p.m_address)))) {

            std::cerr << FAIL << "FAIL: success:" << success <<
                " " << std::hex << ntohl(sockaddr.sin_addr.s_addr) << std::dec <<
                ":" << ntohs(sockaddr.sin_port) << NEUTRAL << "\n";

            return false;
        } else {
            std::cerr << PASS << "pass" << NEUTRAL << "\n";
        }
    }

    return true;
}

// ------------------------------------------------------------------------------------------

int start_unittests(const Parameters &parameters) {
    bool pass = true;

    pass &= test_has_parameter();
    pass &= test_substitute_parameter();
    pass &= test_is_pathname_local();
    pass &= test_parse_endpoint();
    pass &= test_do_dns_lookup();

    if (pass) {
        std::cout << "\n" << PASS << "All tests passed." << NEUTRAL << "\n";
    } else {
        std::cout << "\n" << FAIL << "TESTS FAILED." << NEUTRAL << "\n";
    }

    return pass ? 0 : -1;
}
