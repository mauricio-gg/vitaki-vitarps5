# Vitaki-Fork Development Environment
# Based on official VitaSDK with additional development tools for the entire project

# Pinned deliberately: the floating `latest` tag silently changed base distro
# from Alpine to Ubuntu 24.04 on 2026-08-15, which broke release CI (`apk: not
# found`). Re-pin only after verifying the new tag's package set explicitly.
FROM vitasdk/vitasdk:2026.08-20260815

# Install additional development tools.
# Note: clang-extra-tools is intentionally excluded; clang-format is installed
# below via pip at a pinned version to guarantee identical output between this
# image and CI (see the clang-format pip install step).
# git, curl, wget, bash, and python3 already ship in the base image and are
# not re-listed here. openssl-libs-static has no separate package on Debian —
# libssl-dev already ships the static libssl.a/libcrypto.a archives.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    pngquant \
    imagemagick \
    optipng \
    cppcheck \
    vim \
    nano \
    htop \
    python3-pip \
    python3-protobuf \
    python3-pil \
    libssl-dev \
    protobuf-compiler \
    libprotobuf-dev \
    protobuf-c-compiler \
    libprotobuf-c-dev \
    && rm -rf /var/lib/apt/lists/*

# Python tooling: crash-dump analysis + pinned clang-format.
# clang-format==19.1.5 is the single version pin shared with CI
# (.github/workflows/lint-format.yml).  Both install from the same PyPI wheel
# so the binary is byte-identical — this image and the GitHub Actions runners
# now share the same glibc/Ubuntu libc family, so no ABI mismatch is possible.
# Rebuilding the Docker image is required when this version changes.
RUN pip3 install --no-cache-dir --break-system-packages \
    "pyelftools==0.29" \
    "clang-format==19.1.5"

# Install nanopb (Protocol Buffers for embedded C) - cross-compile for ARM
RUN cd /tmp && \
    wget https://github.com/nanopb/nanopb/archive/refs/tags/0.4.8.tar.gz && \
    echo "3f78bf63722a810edb6da5ab5f0e76c7db13a961c2aad4ab49296e3095d0d830  0.4.8.tar.gz" | sha256sum -c --status && \
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
    echo "024d302a3aadcbf9f78735320a6d5aedf8b77876c8ac8bbb95081ca55054c7eb  json-c-0.17-20230812.tar.gz" | sha256sum -c --status && \
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
    echo "8cf2c833b3e76fc4893ff29c2a376e3394962449e5970e373c0a91421724d222  miniupnpc_2_3_3.tar.gz" | sha256sum -c --status && \
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
        -DCMAKE_C_FLAGS="-fno-pic -I/tmp/vita-stubs -Wno-error -DNEED_STRUCT_IP_MREQN" \
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

# The base image already provides a non-root `vitasdk` user at uid 1000 (the
# same uid the old Alpine `vitadev` user held), so reuse it instead of
# creating a new one. /build does not exist in the base image; the WORKDIR
# instruction above creates it as root, so it must be re-owned by vitasdk
# here before we switch to that user below.
RUN mkdir -p /build/git && chown -R vitasdk:vitasdk /build

USER vitasdk

# Set default command
CMD ["/bin/bash"]
