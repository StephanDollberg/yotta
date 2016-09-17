FROM debian:testing

RUN apt-get update -y && apt-get install -y git build-essential cmake libjemalloc-dev libcap2-bin

COPY /src /src

RUN mkdir build && cd build && cmake ../src/ && make && setcap CAP_NET_BIND_SERVICE=+eip /build/yotta

WORKDIR /data

USER nobody

ENTRYPOINT ["/build/yotta"]
