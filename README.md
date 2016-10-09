## Yotta

Yotta is basic http file server. It serves my personal blog [dollberg.xyz](http://dollberg.xyz).

It's main purpose is though to build an epoll based event loop with all kinds of gimmicks.

Current HTTP Features:
 - If-Modified-Since
 - (Essential) Range Support

Event loop:
 - Edge triggered `epoll`
 - Worker Processes (connections loadbalanced via `REUSEPORT`)
 - timers via `timerfd`
 - `sendfile`

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

    yotta :: 10000
    
To listen on all interfaces and port `10000`.

### Tests

There are a couple of unit tests which can be run via `make test` in the build folder in addition to integration tests in `src/test`.

