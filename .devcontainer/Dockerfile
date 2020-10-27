# See here for image contents: https://github.com/microsoft/vscode-dev-containers/tree/v0.145.1/containers/alpine/.devcontainer/base.Dockerfile

# [Choice] Alpine version: 3.12, 3.11, 3.10
ARG VARIANT="3.12"
FROM mcr.microsoft.com/vscode/devcontainers/base:0-alpine-${VARIANT}

# ** [Optional] Uncomment this section to install additional packages. **
RUN apk update \
    && apk add --no-cache --virtual build-deps python3 python2 sqlite-dev build-base ninja py3-pip clang lld libarchive-dev util-linux linux-headers \ 
    && pip3 --no-cache-dir install meson \
    && pip3 --no-cache-dir install unicorn

RUN cd /tmp \
    && wget http://dl-cdn.alpinelinux.org/alpine/v3.12/releases/x86/alpine-minirootfs-3.12.1-x86.tar.gz