# wolfTrust host CI image.
#
# Bakes in the host toolchains every PR job used to apt-get at job time, so CI
# just pulls this image instead of installing packages on every run. Built on
# the m33mu emulator/cross base so one lineage covers host and target. Built and
# pushed to ghcr.io/wolfssl/wolftrust-ci:latest by publish-ci-image.yml.

FROM ghcr.io/wolfssl/wolfboot-ci-m33mu:v1.15

ENV DEBIAN_FRONTEND=noninteractive

# One apt-get install. Anything added here ships in the image; do not add
# per-workflow installs. gcc-15 and automake already ship in the base.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        gcc-12 gcc-13 gcc-14 \
        clang-19 clang-21 \
        valgrind \
        libtool libtool-bin pkg-config \
    && rm -rf /var/lib/apt/lists/*
