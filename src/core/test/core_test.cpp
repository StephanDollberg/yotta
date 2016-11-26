#include <vector>
#include <iostream>
#include <string>
#include <cstring>

#include "../yta_process.h"

struct create_listen_fds_env_case {
    std::vector<int> fds;
    std::string result;
};

int test_create_listen_fds_env() {
    std::vector<create_listen_fds_env_case> tests{
                                                    { {1}, "listen_fds=1" },
                                                    { {100}, "listen_fds=100" },
                                                    { {1,2,3,4}, "listen_fds=1 2 3 4" },
                                                    { {100,200,300,400}, "listen_fds=100 200 300 400" },
                                                 };

    for (auto&& test : tests) {
        auto result = create_listen_fds_env(test.fds.data(), test.fds.size());
        if (strcmp(result, test.result.data()) != 0) {
            std::cerr << "create_listen_fd_env test failed: "
                      << " expected: " << test.result
                      << " got: " << result << std::endl;

            free(result);
            return 1;
        }

        free(result);
    }

    return 0;
}

struct parse_listen_fds_env_case {
    std::string test;
    std::vector<int> fds;
};

int test_parse_listen_fds_env() {
    std::vector<parse_listen_fds_env_case> tests{
                                                    { "1", {1} },
                                                    { "100", {100} },
                                                    { "1 2 3 4", {1,2,3,4} },
                                                    { "100 200 300 400", {100,200,300,400} },
                                                };

    for (auto&& test : tests) {
        auto result = parse_listen_fds_env(&test.test[0], test.fds.size());

        for (std::size_t i = 0; i < test.fds.size(); ++i) {
            if (result[i] != test.fds[i]) {
                std::cerr << "parse_listen_fd_env test failed: "
                          << " expected: " << test.fds[i]
                          << " got: " << result[i] << " at " << i << std::endl;

                free(result);
                return 1;
            }

        }

        free(result);
    }

    return 0;
}

int main() {

    if (test_create_listen_fds_env() != 0) {
        return 1;
    }

    if (test_parse_listen_fds_env() != 0) {
        return 1;
    }

    return 0;
}
