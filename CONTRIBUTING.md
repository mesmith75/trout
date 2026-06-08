# Contributing to trout

Thank you for your interest in contributing to trout! As part of the SHiP Collaboration, we follow a set of standards to ensure code quality and maintainability.

## Development Workflow

1. **Fork and Clone**: Create a fork of the repository and clone it locally.
2. **Environment**: Ensure you have the required dependencies (ROOT, Geant4, Pythia8, Phlex, etc.). Using the SHiP software stack container is recommended.
3. **Pre-commit Hooks**: We use `pre-commit` to enforce coding standards. Install the hooks before making changes:
   ```bash
   pre-commit install
   ```
4. **Branching**: Create a feature branch for your changes.
5. **Coding Standards**:
   - Follow the existing C++ style (enforced by `clang-format` and `cpplint`).
   - Use `ruff` for Python script formatting.
   - Ensure all files have the correct SPDX license headers (REUSE compliant).
6. **Commits**: We follow [Conventional Commits](https://www.conventionalcommits.org/). This helps in automated changelog generation.
   - `feat: ...` for new features
   - `fix: ...` for bug fixes
   - `docs: ...` for documentation changes
   - `style: ...` for formatting
   - `refactor: ...` for code refactoring
7. **Testing**:
   - Run existing smoke tests using `just`:
     ```bash
     just build
     phlex -c workflows/gun_only.jsonnet
     ```
   - Add new workflows or benchmarks if you introduce new features.
8. **Submission**: Open a Pull Request against the `main` branch. Ensure the CI passes.

## Coding Style

- **C++**: We use C++23. Style is defined in `.clang-format`.
- **Python**: Follow PEP 8 (enforced by `ruff`).
- **Configuration**: Workflows are defined using [Jsonnet](https://jsonnet.org/).

## Licensing

This project is licensed under the **LGPL-3.0-or-later**. All contributions must be compatible with this license. Each new file must include an SPDX identifier and copyright notice.
