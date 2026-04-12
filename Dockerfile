# Vitaki-Fork Development Environment
# Based on official VitaSDK with additional development tools for the entire project

FROM vitasdk/vitasdk:latest

# Install additional development tools
RUN apk add --no-cache \
    pngquant \
    imagemagick \
    optipng \
    cppcheck \
    clang-extra-tools \
    git \
    curl \
    wget \
    vim \
    nano \
    htop \
    bash \
    python3 \
    py3-pip \
    py3-protobuf \
    py3-pillow \
    openssl-dev \
    openssl-libs-static \
    protobuf \
    protobuf-dev \
    protobuf-c \
    protobuf-c-dev

# Python tooling for crash-dump analysis
RUN pip3 install --no-cache-dir --break-system-packages \
    "pyelftools==0.29"

# Install nanopb (Protocol Buffers for embedded C) - cross-compile for ARM
RUN cd /tmp && \
    wget https://github.com/nanopb/nanopb/archive/refs/tags/0.4.8.tar.gz && \
    tar -xzf 0.4.8.tar.gz && \
    cd nanopb-0.4.8 && \
    mkdir build && cd build && \
    cmake .. \
        -DCMAKE_INSTALL_PREFIX=/usr/local/vitasdk/arm-vita-eabi \
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/vitasdk/share/vita.toolchain.cmake \
        -Dnanopb_BUILD_GENERATOR=OFF \
        -DCMAKE_BUILD_TYPE=Release && \
    make && \
    make install && \
    rm -rf /tmp/nanopb-0.4.8*

# Install json-c for Vita (required by Chiaki holepunch path)
RUN cd /tmp && \
    wget https://github.com/json-c/json-c/archive/refs/tags/json-c-0.17-20230812.tar.gz && \
    tar -xzf json-c-0.17-20230812.tar.gz && \
    cd json-c-json-c-0.17-20230812 && \
    mkdir build && cd build && \
    cmake .. \
        -DCMAKE_INSTALL_PREFIX=/usr/local/vitasdk/arm-vita-eabi \
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/vitasdk/share/vita.toolchain.cmake \
        -DCMAKE_POSITION_INDEPENDENT_CODE=OFF \
        -DCMAKE_C_FLAGS=-fno-pic \
        -DDISABLE_STATIC_FPIC=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_TESTING=OFF \
        -DBUILD_APPS=OFF \
        -DHAVE_SNPRINTF=ON \
        -DHAVE_VSNPRINTF=ON \
        -DHAVE_VASPRINTF=ON \
        -DHAVE___THREAD=OFF \
        -DHAVE_DECL_ISINF=ON \
        -DHAVE_DECL_ISNAN=ON \
        -DHAVE_DECL_INFINITY=ON \
        -DHAVE_DECL_NAN=ON \
        -DDISABLE_WERROR=ON \
        -DCMAKE_BUILD_TYPE=Release && \
    make && \
    make install && \
    rm -rf /tmp/json-c-json-c-0.17-20230812*

# Copy Vita compatibility stub headers for miniupnpc cross-compilation
COPY third-party/vita-stubs/ /tmp/vita-stubs/

# Install miniupnpc for Vita (required by UPnP NAT traversal in holepunch path)
RUN cd /tmp && \
    wget https://github.com/miniupnp/miniupnp/archive/refs/tags/miniupnpc_2_3_3.tar.gz && \
    tar -xzf miniupnpc_2_3_3.tar.gz && \
    cd miniupnp-miniupnpc_2_3_3/miniupnpc && \
    sed -i '/set(CMAKE_POSITION_INDEPENDENT_CODE ON)/d' CMakeLists.txt && \
    mkdir build && cd build && \
    cmake .. \
        -DCMAKE_INSTALL_PREFIX=/usr/local/vitasdk/arm-vita-eabi \
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/vitasdk/share/vita.toolchain.cmake \
        -DUPNPC_BUILD_STATIC=ON \
        -DUPNPC_BUILD_SHARED=OFF \
        -DUPNPC_BUILD_TESTS=OFF \
        -DUPNPC_BUILD_SAMPLE=OFF \
        -DNO_GETADDRINFO=ON \
        -DCMAKE_POSITION_INDEPENDENT_CODE=OFF \
        -DCMAKE_C_FLAGS="-fno-pic -I/tmp/vita-stubs -Wno-error -Dgai_strerror=strerror -DNEED_STRUCT_IP_MREQN" \
        -DCMAKE_BUILD_TYPE=Release && \
    make && \
    make install && \
    rm -rf /tmp/miniupnpc_2_3_3.tar.gz /tmp/miniupnp-miniupnpc_2_3_3*

# Set working directory
WORKDIR /build/git

# Set environment variables
ENV VITASDK=/usr/local/vitasdk
ENV PATH=$VITASDK/bin:$PATH
ENV NANOPB_DIR=/usr/local

# Create non-root user for development
RUN adduser -D -s /bin/bash vitadev && \
    chown -R vitadev:vitadev /build

USER vitadev

# Set default command
CMD ["/bin/bash"]
