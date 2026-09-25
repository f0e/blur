# the environment linux releases are built in. see ci/build-linux-docker.sh
FROM ubuntu:22.04

COPY install-build-deps-linux.sh /tmp/
RUN bash /tmp/install-build-deps-linux.sh && rm -rf /var/lib/apt/lists/*
