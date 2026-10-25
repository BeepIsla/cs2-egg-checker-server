FROM cm2network/steamcmd:steam-trixie AS steamcmd

RUN /home/steam/steamcmd/steamcmd.sh +quit

FROM debian:trixie AS builder

WORKDIR /app
RUN apt-get update && apt-get install -y --no-install-recommends --no-install-suggests \
	build-essential \
	clang \
	cmake \
	libstdc++-14-dev \
	ninja-build

COPY submodules/s2sdk/lib/linux64/release ./submodules/s2sdk/lib/linux64/release
COPY submodules/s2sdk/thirdparty/protobuf-3.21.8/src ./submodules/s2sdk/thirdparty/protobuf-3.21.8/src
COPY submodules/steamworks/redistributable_bin/linux64 ./submodules/steamworks/redistributable_bin/linux64
COPY submodules/steamworks/public ./submodules/steamworks/public
COPY submodules/sqlite ./submodules/sqlite
COPY CMakeLists.txt CMakePresets.json ./
COPY src ./src

RUN cmake --preset release && cmake --build --preset release

FROM debian:trixie

RUN apt-get update && apt-get install -y --no-install-recommends \
	ca-certificates \
	&& rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=steamcmd /home/steam/steamcmd/linux64/steamclient.so steamclient.so
COPY --from=builder /app/build/release/bin/libsteam_api.so libsteam_api.so
COPY --from=builder /app/build/release/bin/cs2-egg-checker-server cs2-egg-checker-server

RUN useradd -m -u 1000 server && mkdir -p /data && chown -R server:server /data /app
USER server

ENV LD_LIBRARY_PATH=/app
ENV LISTEN_ADDRESS=0.0.0.0:27015

EXPOSE 27015/udp

ENTRYPOINT ["/app/cs2-egg-checker-server"]
