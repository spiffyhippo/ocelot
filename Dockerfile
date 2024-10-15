FROM debian:bookworm-slim

LABEL org.opencontainers.image.source=https://github.com/OPSnet/Ocelot

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        build-essential \
        cmake \
        default-libmysqlclient-dev \
        libboost-iostreams-dev \
        libboost-system-dev \
        libboost-beast-dev \  # Added Boost.Beast for HTTP parsing
        libev-dev \
        libjemalloc-dev \
        libmysql++-dev \
        netcat-traditional \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

COPY . /srv
WORKDIR /srv

# Set C++17 standard for Boost.Beast
RUN cmake -Wno-dev -S /srv -B /srv/build -DCMAKE_CXX_STANDARD=17 \
    && make -C build \
    && apt-get purge -y \
        build-essential \
        cmake \
        pkg-config \
    && apt-get autoremove -y \
    && apt-get clean -y \
    && mkdir -p /tmp/ocelot \
    && mv /srv/build/ocelot /srv/ocelot \
    && mv /srv/ocelot.conf.dist /srv/ocelot.conf

# Default listen_port value in ocelot.conf
EXPOSE 34000

CMD ["/srv/ocelot"]
