FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    make \
    gcc-arm-none-eabi=15:10.3-2021.07-4 \
    binutils-arm-none-eabi=2.38-3ubuntu1+15build1 \
    libnewlib-arm-none-eabi=3.3.0-1.3 \
    libstdc++-arm-none-eabi-newlib=15:10.3-2021.07-4+17 \
    python3 git ca-certificates srecord \
    gcc libc6-dev gcovr \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
