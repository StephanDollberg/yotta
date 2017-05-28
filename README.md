## Yotta

Yotta is basic http file server. It serves my personal blog [dollberg.xyz](https://dollberg.xyz).

It's main purpose is though to build an epoll based event loop with all kinds of gimmicks. In addition, it's a performant file server that doesn't need 50 lines of config.

Current HTTP Features:
 - If-Modified-Since
 - (Essential) Range Support

Event loop:
 - Edge triggered `epoll` loop
 - Connections loadbalanced via `REUSEPORT` to worker processes
 - Timers via `timerfd`
 - Files are served directly via `sendfile`
 - Zero down time upgrades (same principle as nginx)

### Building

CMake is being use as a build system.

```
mkdir build
cd build 
cmake ../src
make
```

Will build you the `yotta` binary.

### Usage

Run the binary in the directory you want to serve, you shall pass the address and port to listen on:

    yotta -h :: -p 10000
    
To listen on all interfaces and port `10000`.

See `upgrade_yotta.sh` and `yotta.unit` for usage of the pid file (`-i`) option, daemonizing (`-g`) option and how to upgrade.

### Tests

There are a couple of unit tests which can be run via `make test` in the build folder in addition to integration tests in `src/test`.

