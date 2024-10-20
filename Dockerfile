FROM debian:bookworm AS builder
WORKDIR /opt/src

RUN apt-get update && apt-get install -y \
  cmake \
  g++ \
  git \
  libssl-dev \
  zlib1g-dev

COPY . .

RUN mkdir build && \
  cd build && \
  cmake .. && \
  make -j 4 && \
  make -j 4 test

FROM debian:bookworm AS final
WORKDIR /opt/app

RUN apt-get update && apt-get install -y \
  libssl3 \
  zlib1g

COPY --from=builder /opt/src/build/shelly_plug_metrics_exporter /opt/app/

EXPOSE 9100
ENTRYPOINT ["./shelly_plug_metrics_exporter", "--targets_config_file", "/opt/config/targets.json"]
