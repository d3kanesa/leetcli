FROM gcc:latest

WORKDIR /app

# Install required tools
RUN apt-get update && \
    apt-get install -y git cmake build-essential curl

# Clone and bootstrap vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg && \
    /opt/vcpkg/bootstrap-vcpkg.sh

# Install dependencies
RUN /opt/vcpkg/vcpkg install cpr nlohmann-json

# Copy source files
COPY src/ ./src/
COPY CMakeLists.txt ./
copy include ./include/

# Build the project using vcpkg toolchain
RUN cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake && \
    cmake --build build --target install

# Optionally set a prefix (uncomment if needed)
# RUN cmake --install build --prefix ~/.local

# ...existing code...
ENTRYPOINT ["/bin/bash", "-c", "exec /usr/local/bin/leetcli \"$@\"", "--"]