#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../yta_http.hpp"

struct clean_path_test_case {
    std::string input;
    std::string output;
};

int test_clean_path() {
    std::vector<clean_path_test_case> tests{ { "", "." },
                                             { "abc", "abc" },
                                             { "abc/def", "abc/def" },
                                             { "a/b/c", "a/b/c" },
                                             { ".", "." },
                                             { "..", ".." },
                                             { "../..", "../.." },
                                             { "../../abc", "../../abc" },
                                             { "/abc", "/abc" },
                                             { "/", "/" },

                                             // Remove trailing slash
                                             { "abc/", "abc" },
                                             { "abc/def/", "abc/def" },
                                             { "a/b/c/", "a/b/c" },
                                             { "./", "." },
                                             { "../", ".." },
                                             { "../../", "../.." },
                                             { "/abc/", "/abc" },

                                             // Remove doubled slash
                                             { "abc//def//ghi", "abc/def/ghi" },
                                             { "//abc", "/abc" },
                                             { "///abc", "/abc" },
                                             { "//abc//", "/abc" },
                                             { "abc//", "abc" },

                                             // Remove . elements
                                             { "abc/./def", "abc/def" },
                                             { "/./abc/def", "/abc/def" },
                                             { "abc/.", "abc" },

                                             // Remove .. elements
                                             { "abc/def/ghi/../jkl", "abc/def/jkl" },
                                             { "abc/def/../ghi/../jkl", "abc/jkl" },
                                             { "abc/def/..", "abc" },
                                             { "abc/def/../..", "." },
                                             { "/abc/def/../..", "/" },
                                             { "abc/def/../../..", ".." },
                                             { "/abc/def/../../..", "/" },
                                             { "abc/def/../../../ghi/jkl/../../../mno",
                                               "../../mno" },

                                             // Combinations
                                             { "abc/./../def", "def" },
                                             { "abc//./../def", "def" },
                                             { "abc/../../././../def", "../../def" } };

    for (auto&& test : tests) {
        char buf[512] = { 0 };
        auto res_length = yta::http::clean_path(&test.input[0], test.input.size(), buf);
        if (res_length != test.output.size() ||
            !std::equal(buf, buf + res_length, test.output.begin())) {
            std::cerr << "Clean path test failed: input after: " << test.input
                      << " expected: " << test.output << " result length: " << res_length
                      << std::endl;
            return 1;
        }
    }

    return 0;
}

int main() {

    if (test_clean_path() != 0) {
        return 1;
    }

    return 0;
}
