# =============================================================================
# Stage 1: Build the Qubic binary from source
# =============================================================================
FROM ubuntu:24.04 AS builder

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
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

ARG QUBIC_REPO_URL=https://github.com/qubic/core-lite.git
ARG QUBIC_BRANCH=main
ARG QUBIC_COMMIT=""

RUN git clone --depth 1 --branch ${QUBIC_BRANCH} ${QUBIC_REPO_URL} qubic-core-lite \
    && if [ -n "${QUBIC_COMMIT}" ]; then \
         cd qubic-core-lite && git fetch --depth 1 origin ${QUBIC_COMMIT} && git checkout ${QUBIC_COMMIT}; \
       fi

WORKDIR /build/qubic-core-lite/build

RUN cmake .. \
    -D CMAKE_C_COMPILER=clang \
    -D CMAKE_CXX_COMPILER=clang++ \
    -D BUILD_TESTS:BOOL=OFF \
    -D BUILD_BINARY:BOOL=ON \
    -D CMAKE_BUILD_TYPE=Release \
    -D ENABLE_AVX512=OFF \
    && cmake --build . --target Qubic -- -j$(nproc)

# Extract compiled epoch and version for metadata
RUN grep -oP '#define EPOCH \K[0-9]+' /build/qubic-core-lite/src/public_settings.h > /build/epoch.txt
RUN printf "%s.%s" \
    "$(grep -oP '#define VERSION_A \K[0-9]+' /build/qubic-core-lite/src/public_settings.h)" \
    "$(grep -oP '#define VERSION_B \K[0-9]+' /build/qubic-core-lite/src/public_settings.h)" \
    > /build/version.txt

# =============================================================================
# Stage 2: Runtime image with Python orchestrator
# =============================================================================
FROM ubuntu:24.04

# Runtime C++ dependencies for Qubic binary
RUN apt-get update && apt-get install -y \
    libc++1 \
    libc++abi1 \
    libjsoncpp25 \
    libssl3 \
    openssh-client \
    rsync \
    curl \
    unzip \
    zip \
    python3 \
    python3-pip \
    python3-venv \
    && rm -rf /var/lib/apt/lists/*

# Create Python venv and install dependencies
RUN python3 -m venv /opt/venv
ENV PATH="/opt/venv/bin:$PATH"

# Install Python package
COPY pyproject.toml /opt/orchestrator/pyproject.toml
COPY app/ /opt/orchestrator/app/
COPY config/ /opt/orchestrator/config/
WORKDIR /opt/orchestrator
RUN pip install --no-cache-dir .

# Copy Qubic binary and epoch metadata
WORKDIR /qubic
COPY --from=builder /build/qubic-core-lite/build/src/Qubic /qubic/Qubic
COPY --from=builder /build/epoch.txt /qubic/epoch.txt
COPY --from=builder /build/version.txt /qubic/version.txt
RUN chmod +x /qubic/Qubic

# Ports: P2P mainnet (21841), P2P testnet (31841), HTTP API (41841), Management (8080)
EXPOSE 21841 31841 41841 8080

# Environment defaults
ENV QUBIC_MODE="normal"
ENV QUBIC_DATA_DIR="/qubic"
ENV QUBIC_BINARY_PATH="/qubic/Qubic"
ENV QUBIC_LOG_LEVEL="INFO"
ENV QUBIC_LOG_FORMAT="json"

# Docker health check via the Qubic HTTP API
HEALTHCHECK --interval=30s --timeout=10s --start-period=300s --retries=3 \
    CMD curl -sf http://127.0.0.1:41841/tick-info || exit 1

# PID 1: Python orchestrator
ENTRYPOINT ["python", "-m", "app"]
