# Contributing to GSST

Thank you for your interest in contributing! This guide will help you get started.

## Development Environment

### Option A: Dev Container (Recommended)

The easiest way to get a fully configured environment:

1. Install [Docker](https://www.docker.com/) and the [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html)
2. Install the [VS Code Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)
3. Open the project in VS Code and click **"Reopen in Container"**

Everything (CUDA toolkit, build tools, linters, formatters) is pre-installed.

### Option B: Local Setup

Requirements:

- **CUDA Toolkit** 12.0+
- **CMake** 3.25+
- **C++20** compiler (GCC 11+, Clang 15+, MSVC 2022+)
- **Ninja** (recommended) or Make
- **clang-format** 18+ and **clang-tidy** 18+ (for linting)
- **pre-commit** (`pip install pre-commit`)

## Build

```bash
# Configure (default preset: Release with tests, benchmarks, examples)
cmake --preset default

# Build
cmake --build --preset default

# Run tests
ctest --preset default

# Other presets: debug, release, ci, sanitize
cmake --preset debug
cmake --build --preset debug
```

## Pre-commit Hooks

We use [pre-commit](https://pre-commit.com/) to enforce code quality before each commit:

```bash
# Install hooks (one-time)
pre-commit install

# Run all hooks manually
pre-commit run --all-files
```

This checks:

- **clang-format** — C++/CUDA code style
- **cmake-format** — CMake file style
- **trailing whitespace**, **end-of-file**, **merge conflicts**
- **yamllint**, **markdownlint**, **shellcheck**
- **typos** — catches common spelling errors

## Code Style

- Follow the project `.clang-format` configuration (LLVM-based, 120-column limit)
- Use C++20 features where appropriate
- CUDA kernels: mark with `__global__`, use `__device__` / `__host__` explicitly
- Naming conventions:
  - Classes/structs: `CamelCase`
  - Functions/methods: `snake_case`
  - Variables: `snake_case`
  - Constants/macros: `UPPER_CASE` (prefix macros with `GSST_`)
  - Namespaces: `snake_case`

## Testing

```bash
# Run all tests
ctest --test-dir build --output-on-failure

# Run a specific test
./build/test/gsst_roundtrip_test

# Run with a filter
./build/test/gsst_roundtrip_test --gtest_filter="*Blocks*"
```

When adding new functionality:

1. Add unit tests in `test/`
2. Ensure existing tests still pass
3. Add benchmarks in `bench/` for performance-sensitive code

## Project Structure

```text
├── .github/workflows/    # CI/CD pipelines
├── .devcontainer/        # Dev container setup
├── cmake/                # CMake modules
├── include/gsst/         # Public headers
│   ├── gsst.hpp          # C++ API
│   ├── gsst.h            # C API
│   └── detail/           # Internal headers
├── src/                  # Implementation
├── test/                 # Unit tests (Google Test)
├── bench/                # Benchmarks (Google Benchmark)
└── examples/             # Usage examples
```

## Pull Request Process

1. Fork the repository and create a feature branch from `main`
2. Make your changes, following the code style guidelines
3. Ensure all tests pass and pre-commit hooks are clean
4. Write clear commit messages (conventional commits encouraged)
5. Open a PR against `main` with a description of your changes

## CI Pipeline

Every push and PR triggers:

- **Lint & Format** — clang-format check
- **CUDA Build & Test** — Release and Debug builds
- **Host Compilation Check** — ensures code compiles
- **Install Check** — validates `cmake --install` and `find_package(gsst)` flow

PRs also trigger **Static Analysis** (clang-tidy, cppcheck).

## License

By contributing, you agree that your contributions will be licensed under the project's MIT License.
