FROM ghcr.io/ps2dev/ps2dev:latest

USER root

RUN apk add --no-cache \
    make \
    bash \
    git \
    build-base

WORKDIR /workspace

CMD ["make"]
