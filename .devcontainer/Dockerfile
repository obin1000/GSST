# =============================================================================
# GSST — Development Container
# =============================================================================
# CUDA development environment with all build tools pre-installed.
# =============================================================================

ARG CUDA_VERSION=13.2
ARG UBUNTU_VERSION=24.04

FROM nvidia/cuda:${CUDA_VERSION}-devel-ubuntu${UBUNTU_VERSION}

ENV DEBIAN_FRONTEND=noninteractive

# ---------------------------------------------------------------------------
# System packages
# ---------------------------------------------------------------------------
RUN apt-get update -qq && apt-get install -y --no-install-recommends \
    # Build tools
    cmake \
    ninja-build \
    g++ \
    git \
    ca-certificates \
    pkg-config \
    # Analysis tools
    clang-format-18 \
    clang-tidy-18 \
    cppcheck \
    # OpenMP support
    libomp-dev \
    # Debug tools
    gdb \
    cuda-gdb-13-2 \
    valgrind \
    # Performance tools
    linux-tools-generic \
    # Ccache for faster rebuilds
    ccache \
    # Other utilities
    curl \
    wget \
    unzip \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# ---------------------------------------------------------------------------
# Python tools (cmake-format, pre-commit)
# ---------------------------------------------------------------------------
RUN pip3 install --break-system-packages \
    cmake-format \
    pre-commit

# ---------------------------------------------------------------------------
# Ccache configuration
# ---------------------------------------------------------------------------
ENV CCACHE_DIR=/root/.cache/ccache
ENV CCACHE_MAXSIZE=2G
ENV CMAKE_CXX_COMPILER_LAUNCHER=ccache
ENV CMAKE_CUDA_COMPILER_LAUNCHER=ccache

# Create ccache symlinks
RUN mkdir -p /usr/local/lib/ccache && \
    ln -s /usr/bin/ccache /usr/local/lib/ccache/g++ && \
    ln -s /usr/bin/ccache /usr/local/lib/ccache/gcc && \
    ln -s /usr/bin/ccache /usr/local/lib/ccache/nvcc

ENV PATH="/usr/local/lib/ccache:${PATH}"

# ---------------------------------------------------------------------------
# Clang-tidy / clang-format aliases
# ---------------------------------------------------------------------------
RUN update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-18 100 && \
    update-alternatives --install /usr/bin/clang-tidy  clang-tidy  /usr/bin/clang-tidy-18  100

# ---------------------------------------------------------------------------
# Working directory
# ---------------------------------------------------------------------------
WORKDIR /workspaces/GSST

# Default to bash
CMD ["/bin/bash"]
