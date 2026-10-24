FROM ubuntu:latest

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    clang \
    ninja-build \
    cmake \
    git \
    curl \
    ca-certificates \
    lib32gcc-s1 \
    && rm -rf /var/lib/apt/lists/*

# Install SteamCMD to obtain steamclient.so
RUN mkdir -p /root/.steam/sdk64 && \
    curl -sqL "https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz" | tar zxvf - -C /root/.steam/sdk64 && \
    /root/.steam/sdk64/steamcmd.sh +quit || true && \
    cp /root/.steam/sdk64/linux64/steamclient.so /root/.steam/sdk64/steamclient.so

WORKDIR /app

ENV LD_LIBRARY_PATH=/app/build/release/bin

COPY . .

RUN chmod +x update_protobufs.sh entrypoint.sh && \
    ./update_protobufs.sh && \
    cmake --preset release && \
    cmake --build --preset release

EXPOSE 27015/udp

ENTRYPOINT ["/app/entrypoint.sh"]
CMD ["-listen", "0.0.0.0:27015"]
