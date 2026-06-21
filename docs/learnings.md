# Engineering Learnings & Workarounds

During the implementation and testing of the `mrcs` Stage 1 compiler wrapper, we encountered two significant system-level bugs. This document details the issues and their architectural workarounds.

---

## 1. Android/Termux Shared Storage Transaction Bug

### The Issue
On Android devices running Termux or AndroidIDE, when working on shared storage (such as `/sdcard` or custom FUSE mounts), the underlying GNU RCS commands (`rcs`, `ci`, `co`) would run successfully but fail to write back revisions. Specifically, they would leave a temporary lock file behind (e.g. `,doc.txt,` or `RCS/,doc.txt,`) without updating the main archive file (`doc.txt,v` or `RCS/doc.txt,v`). 

### The Root Cause
Traditionally, GNU RCS implements atomic file replacement by creating a temp file, applying changes, and using the POSIX `link()` followed by `unlink()` system calls to overwrite the target. On Android's FUSE or FAT-based shared storage layers, hard links (`link()`) are unsupported. Because RCS does not handle this failure cleanly (it prints success but fails to rename), transaction files get stuck in-flight, leaving the repository locked or out of sync.

### The Workaround
We implemented a transaction repair mechanism (`finalize_transaction` in [mrcs.c](file:///root/Projects/mrcs/mrcs.c)):
- Immediately after executing any RCS binary that modifies the archive, `mrcs` checks for the presence of the temporary lock files `,filename,` or `RCS/,filename,`.
- If a temporary lock file is detected, `mrcs` manually opens write permissions on the target (`chmod 0644`), performs a standard filesystem rename/move (`rename()`), and resets the target to read-only (`chmod 0444`) to preserve RCS file integrity.

This ensures `mrcs` operates seamlessly on Termux and AndroidIDE shared storage.

---

## 2. GCC vs Clang Compiler Portability (macOS build)

### The Issue
During automated builds in the GitHub Release Actions runner for macOS (`macos-latest`), the compiler crashed with the error:
`error: function definition is not allowed here`

### The Root Cause
The initial translation of the python prototype used **nested helper functions** (e.g. `add_file` declared inside `find_tracked_files`, `print_revision` inside `cmd_log`, and `is_revision` inside `main`). 
- **GCC** supports nested functions as a compiler extension.
- **Clang** (macOS host compiler) does not support nested functions, adhering strictly to ISO C99 standards.

### The Workaround
We refactored all nested functions into standard file-scoped helper functions (`static`):
- Extracted local parameters into pointers or standard arguments.
- Added necessary forward prototype declarations (`LineList split_lines(const char *buf)`) at the top of [mrcs.c](file:///root/Projects/mrcs/mrcs.c).

This refactoring ensures complete ANSI/ISO C99 compatibility, allowing warning-free compilations on both Clang and GCC.

---

## 3. Command Injection Mitigation

### The Issue
To wrap utilities like `ci -u -m"message" file`, developers often use `system()`. However, if the user commits a message containing shell metacharacters (e.g., `mrcs commit -m 'Initial commit; rm -rf /'`), it would trigger a shell command injection.

### The Workaround
We avoided `system()` entirely. All commands are executed via a custom `run_command` helper using POSIX `fork()` and `execvp()`. Arguments are passed as an array of pointers directly to the OS kernel, bypassing the shell processor and completely neutralizing command injection vectors.
