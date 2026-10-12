# syntax=docker/dockerfile:1
#
# EmbedIDS EdgeML image: one image provides both embedids-agent and
# edgeml-simulator. Build context is the repository root:
#   docker build -f edgeml/deployment/docker/Dockerfile -t embedids-edgeml .

ARG BASE_IMAGE=ubuntu:24.04

FROM ${BASE_IMAGE} AS build
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build git ca-certificates libsqlite3-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY cmake ./cmake
COPY include ./include
COPY src ./src
COPY edgeml ./edgeml
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_EDGEML=ON -DBUILD_TESTS=OFF \
 && cmake --build build --target embedids-agent edgeml-simulator \
 && strip build/edgeml/embedids-agent build/edgeml/edgeml-simulator

FROM ${BASE_IMAGE} AS runtime
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libsqlite3-0 curl ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && groupadd --system embedids \
 && useradd --system --gid embedids --home-dir /var/lib/embedids \
      --shell /usr/sbin/nologin embedids \
 && mkdir -p /etc/embedids /var/lib/embedids /models \
 && chown embedids:embedids /var/lib/embedids /models

COPY --from=build /src/build/edgeml/embedids-agent /usr/local/bin/
COPY --from=build /src/build/edgeml/edgeml-simulator /usr/local/bin/
COPY edgeml/configs/ /usr/share/embedids/configs/
COPY edgeml/configs/demo.yaml /etc/embedids/config.yaml
COPY edgeml/dashboard/frontend/ /usr/share/embedids/dashboard/

# Least privilege: the agent needs no root access in simulation mode.
USER embedids
WORKDIR /var/lib/embedids
EXPOSE 8081 9100
HEALTHCHECK --interval=10s --timeout=3s --retries=3 \
  CMD curl -fsS http://127.0.0.1:8081/health || exit 1
ENTRYPOINT ["/usr/local/bin/embedids-agent"]
CMD ["--config", "/etc/embedids/config.yaml"]
