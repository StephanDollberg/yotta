#include <getopt.h>
#include <iostream>

// Maybe switch to boost program_options in the future but that just requires linkage


struct argument_options {
    bool daemonize = false;
    std::string pid_file = "/tmp/yotta.pid";
    std::string port = "0";
    std::string host = "::";
    int workers = 4;
};


inline argument_options get_program_opts(int argc, char** argv) {
    argument_options options;
    int opt = 0;
    while ((opt = getopt(argc, argv, "gi:p:h:w:")) != -1) {
        switch (opt) {
            case 'g': {
                std::cout << "Daemonizing turned on" << std::endl;
                options.daemonize = true;
                break;
            }
            case 'i': {
                std::cout << "Using pid file: " << optarg << std::endl;
                options.pid_file = optarg;
                break;
            }
            case 'p': {
                std::cout << "Listening on port: " << optarg << std::endl;
                options.port = optarg;
                break;
            }
            case 'h': {
                std::cout << "Listening on host: " << optarg << std::endl;
                options.host = optarg;
                break;
            }
            case 'w': {
                std::cout << "Spawning " << optarg << " workers" << std::endl;
                options.workers = std::atoi(optarg);
                break;
            }
            case '?':  { // unknown option... {
                std::cerr << "Unknown option: '" << optopt << std::endl;
                break;
            }
        }
    }

    return options;
}
