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
