# Contributing to PSVR2 Linux Adapter

Thank you for your interest in contributing to the PSVR2 Linux Adapter project! This document outlines the process for contributing to the project and provides guidelines to help your contributions be accepted smoothly.

## Development Workflow

### Setting Up Development Environment

1. Fork the repository on GitHub
2. Clone your fork locally:
   ```bash
   git clone https://github.com/YOUR_USERNAME/psvr2-linux-adapter.git
   cd psvr2-linux-adapter
   ```
3. Set up the build system:
   ```bash
   chmod +x configure.sh
   ./configure.sh --debug
   ```
4. Build the project:
   ```bash
   cd build
   make
   ```

### Making Changes

1. Create a new branch for your feature or bugfix:
   ```bash
   git checkout -b feature/your-feature-name
   ```
   
2. Make your changes following the coding style guidelines below

3. Run the code analyzer to check for style issues:
   ```bash
   cd build
   make check-style
   ```

4. Test your changes:
   ```bash
   # If you've added new functionality, consider writing tests
   cd build
   make test  # if you enabled tests with --enable-tests
   ```

5. Commit your changes with meaningful commit messages:
   ```bash
   git add .
   git commit -m "Add feature: description of your changes"
   ```

6. Push to your fork:
   ```bash
   git push origin feature/your-feature-name
   ```

7. Create a Pull Request against the main repository

## Coding Style Guidelines

This project follows the Linux kernel coding style for kernel module code:

1. Indentation: Use tabs (8 characters wide) for indentation
2. Line Length: Keep lines to a maximum of 80 characters when possible
3. Naming Convention:
   - Use lowercase for function and variable names
   - Use underscores to separate words
   - Prefix functions and types with `psvr2_`
4. Comments:
   - Use `/* */` style comments, not `//`
   - Document function purpose, parameters, and return values
5. Error Handling:
   - Always check return values from functions
   - Use goto for error cleanup paths in kernel code

For userspace code, we follow a more modern style:

1. Indentation: 4 spaces (no tabs)
2. Line Length: 100 characters maximum
3. Naming Convention:
   - Use lowercase with underscores for function and variable names
   - Use camelCase for struct members
4. Comments:
   - Both `/* */` and `//` style comments are acceptable
   - Document all public API functions thoroughly

## Pull Request Guidelines

1. Keep PRs focused on a single issue or feature
2. Include a clear description of the changes and their purpose
3. Reference any related issues
4. Make sure all tests pass
5. Update documentation if necessary

## Reporting Issues

When reporting issues, please include:

1. Description of the issue
2. Steps to reproduce
3. Expected vs. actual behavior
4. Your Linux distribution and kernel version
5. Hardware details (especially GPU and PSVR2 adapter firmware version if known)

## Protocol Documentation

If you're contributing to protocol reverse engineering:

1. Document all findings in Markdown in the `docs/` directory
2. Include packet captures when possible
3. Clearly distinguish between confirmed and speculative information
4. Include references to any external resources used

## Code of Conduct

Please be respectful and constructive in all interactions. We aim to foster an inclusive and welcoming community where everyone feels comfortable contributing.

## License

By contributing to this project, you agree that your contributions will be licensed under the project's GNU GPL v2.0 license.
