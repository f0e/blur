# the environment linux releases are built in. see build.sh
FROM ubuntu:22.04

COPY install-build-deps.sh /tmp/
RUN bash /tmp/install-build-deps.sh && rm -rf /var/lib/apt/lists/*
