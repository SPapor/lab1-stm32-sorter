FROM ubuntu:26.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    make \
    gcc-arm-none-eabi=15:14.2.rel1-1 \
    binutils-arm-none-eabi=2.45.50.20251209-1ubuntu1+23build1 \
    libnewlib-arm-none-eabi=4.6.0.20260123-1 \
    libstdc++-arm-none-eabi-newlib=15:14.2.rel1-1+29 \
    python3 git ca-certificates srecord \
    gcc libc6-dev gcovr \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
