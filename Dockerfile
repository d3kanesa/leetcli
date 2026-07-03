FROM ubuntu:22.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git clang pkg-config curl zip unzip tar \
    libssl-dev libcurl4-openssl-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Bootstrap vcpkg
RUN git clone https://github.com/Microsoft/vcpkg.git /vcpkg \
    && /vcpkg/bootstrap-vcpkg.sh

ENV VCPKG_ROOT=/vcpkg

# Copy project files
COPY . .

# Install vcpkg dependencies
RUN /vcpkg/vcpkg install cpr nlohmann-json ftxui

# Build
RUN cmake -S . -B build \
    -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -- -j$(nproc)

FROM debian:12-slim AS runtime
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 libcurl4 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Copy built binary
COPY --from=builder /src/build/leetcli /usr/local/bin/leetcli
RUN chmod +x /usr/local/bin/leetcli

# Copy entrypoint
COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

# Set HOME so the C++ binary finds config at $HOME/.leetcli/config.json,
# which maps to the bind-mounted /workspace on the host.
ENV HOME=/workspace
VOLUME ["/workspace"]
WORKDIR /workspace

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["help"]
