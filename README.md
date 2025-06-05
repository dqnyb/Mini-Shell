# Mini-Shell

## Overview
Mini-Shell is a lightweight shell implementation written in C. It provides basic functionality for executing commands, handling pipes, redirection, and managing environment variables. The shell supports internal commands like `cd`, `pwd`, and `exit`, as well as external commands executed via `execvp`.

## Features
- **Internal Commands**: Includes `cd`, `pwd`, and `exit`.
- **External Commands**: Executes external programs using `execvp`.
- **Piping**: Supports command chaining with pipes (`|`).
- **Redirection**: Handles input (`<`) and output (`>`/`>>`) redirection.
- **Parallel Execution**: Executes commands simultaneously using `&`.
- **Conditional Execution**: Supports conditional operators (`&&` and `||`).

## File Structure
- **`main.c`**: Entry point of the shell. Handles user input and command parsing.
- **`cmd.c`**: Implements command execution logic, including internal and external commands.
- **`cmd.h`**: Header file for `cmd.c`.
- **`utils.c`**: Utility functions for string manipulation and argument preparation.
- **`utils.h`**: Header file for `utils.c`.
- **`Makefile`**: Build script for compiling the project.
- **`README.md`**: Documentation for the project.

## Build Instructions
To build the project, run the following command:
```sh
make# Mini-Shell