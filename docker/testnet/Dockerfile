# =============================================================================
# Stage 1: Build
# =============================================================================
FROM --platform=linux/amd64 ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    clang \
    cmake \
    nasm \
    libc++-dev \
    libc++abi-dev \
    libjsoncpp-dev \
    uuid-dev \
    zlib1g-dev \
    libssl-dev \
    libstdc++-12-dev \
    libfmt-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build/qubic-core-lite
COPY . .

RUN mkdir -p build && cd build \
    && cmake .. \
        -D CMAKE_C_COMPILER=clang \
        -D CMAKE_CXX_COMPILER=clang++ \
        -D BUILD_TESTS:BOOL=OFF \
        -D BUILD_BINARY:BOOL=ON \
        -D CMAKE_BUILD_TYPE=Release \
        -D ENABLE_AVX512=OFF \
        -D TESTNET=ON \
    && cmake --build . --target Qubic -- -j$(nproc)

# =============================================================================
# Stage 2: Runtime
# =============================================================================
FROM --platform=linux/amd64 ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    libc++1 \
    libc++abi1 \
    libjsoncpp25 \
    libssl3 \
    && rm -rf /var/lib/apt/lists/*

# Binary lives outside the data volume
COPY --from=builder /build/qubic-core-lite/build/src/Qubic /usr/local/bin/Qubic
RUN chmod +x /usr/local/bin/Qubic

# Data directory (state files, snapshots) - safe to mount as volume
WORKDIR /qubic

# Testnet P2P port
EXPOSE 31841
# RPC/HTTP API
EXPOSE 41841

ENTRYPOINT ["/usr/local/bin/Qubic"]
