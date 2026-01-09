# Copilot / AI Agent Instructions for Pacmanist

This file contains targeted, actionable guidance for AI coding agents working on the `operative-systems-pacmanist` repository. Keep suggestions focused on the project's current codebase (C, Makefile, ncurses) and the project requirements in `project_statement.md`.

**Big Picture:**
- **Project type:** C terminal game (Pacman-like) using `ncurses` for display.
- **Core components:** `src/game.c` (main loop), `src/board.c` (game state & rules), `src/display.c` (ncurses UI), headers in `include/` (`board.h`, `display.h`).
- **Build / outputs:** `Makefile` drives compilation; binary produced at `bin/Pacmanist`, object files in `obj/`.

**Build / Run / Debug (explicit):**
- **Install deps:** `libncurses-dev` (Debian/Ubuntu) or `ncurses-devel` (RHEL/Fedora). See `README.md`.
- **Compile:** `make` or `make pacmanist` (or `make all`).
- **Run:** `./bin/Pacmanist` or `make run`.
- **Create build folders:** `make folders` (creates `obj/` and `bin/`).
- **Clean:** `make clean` removes objects and executable.
- **Valgrind:** pass the suppression file included in repo: `valgrind --suppressions=ncurses.suppression ./bin/Pacmanist` to ignore known ncurses leaks.
- **GDB:** ncurses captures the terminal; prefer `gdb -p <Pacmanist_PID>` from a second terminal (or add an artificial delay at startup to attach early).

**Project-specific conventions & constraints (discovered):**
- **C standard:** C17. Compiler flags found in `Makefile`: `-g -Wall -Wextra -Werror -std=c17 -D_POSIX_C_SOURCE=200809L` and linking with `-lncurses`.
- **Single-thread UI rule:** `ncurses` is not thread-safe. Any concurrency solution must ensure only one thread interacts with `ncurses` (the display thread). Other threads may update game state but must synchronize before the display reads it.
- **POSIX file IO requirement (exercise scope):** When extending the code to read level/behavior files, the project requires using POSIX file-descriptor APIs (open/read/close) and NOT `stdio` (`FILE *`) — see `project_statement.md` for the exercise rules. AI agents should follow that when adding file parsing code.
- **Save/restore and processes:** The exercises require using `fork()` to implement save-backup semantics (one saved state allowed). If implementing, prefer explicit comments documenting which process writes the backup and which continues running.

**Code patterns & where to look:**
- `src/game.c`: main loop, input handling (W/A/S/D), game lifecycle. Good place to add high-level orchestration changes.
- `src/board.c` and `include/board.h`: data model for the board, agents and rules such as collisions and point collection. Modify here for core game logic.
- `src/display.c` and `include/display.h`: wrapper around `ncurses` calls and screen updates. Keep ncurses-only code confined here to maintain single-threaded UI.
- `debug.log`: runtime log produced by the program. Useful to inspect after running to understand state transitions and key events.

**Integration points / external dependencies:**
- `ncurses` for terminal rendering; leaks expected and suppressed by `ncurses.suppression`.
- Standard POSIX APIs for file operations (open/read/write/close), process control (`fork`, signals), and synchronization primitives (pthread APIs if threads are used).

**Safe change recommendations for AI patches:**
- Keep `ncurses` calls isolated to `display.c`. When adding threads, add a single display thread and use mutexes/condition variables for state snapshots.
- When introducing file parsing, create small helper functions in a new `src/io.c` (and `include/io.h`) that use POSIX `open/read/close`. Add unit-style manual tests (small helper program or a test target in Makefile) rather than changing `game.c` directly.
- Preserve existing logging to `debug.log`. If adding more logging, follow the existing format (keys, REFRESH, board dumps) to keep `debug.log` parsable.
- Do not change project-wide compiler flags in `Makefile` without a clear reason — many course tests assume `-D_POSIX_C_SOURCE` and the strict flags.

**Examples (copyable):**
- Compile and run with valgrind suppression:
```
make
valgrind --suppressions=ncurses.suppression ./bin/Pacmanist
```
- Attach gdb from another terminal:
```
./bin/Pacmanist &
gdb -p <PID>
```

**Files to reference when making changes:**
- `Makefile` — build flags and targets
- `src/game.c`, `src/board.c`, `src/display.c` — main logic and display
- `include/board.h`, `include/display.h` — data structures and interfaces
- `project_statement.md` — explicit assignment constraints (POSIX IO, `fork`, thread/ui responsibilities)
- `ncurses.suppression` — use with Valgrind

If any section is unclear or you'd like me to merge these guidelines into an existing team style, tell me which parts to expand or examples you want added (parsing example, threading skeleton, or sample Makefile target). 
