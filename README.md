# mrcs (Modern Revision Control System)

> "Version one file with zero friction."

`mrcs` is a modern, developer-friendly revision control system designed for tracking individual files without the overhead of initializing a full version control system like Git. It wraps standard GNU RCS utilities behind a beautiful, clean, and intuitive Git-like interface.

---

## Why `mrcs`?

While Git is the industry standard for project-level version control, it is often overkill—or even risky—for tracking individual files. `mrcs` acts as a **secondary, local-only file-tracking system** that solves these specific problems:

- **Prevent Secret Leaks (No Accidental Pushes):** If you are tracking local scratchpads, drafts, configuration overrides, or files containing private keys, keeping them under Git runs the risk of accidentally staging and pushing them to a public remote repository. `mrcs` operates **entirely locally**—there is no `push` command and no remote connection, so your files are guaranteed to stay on your machine.
- **Micro-Versioning Without Commit Noise:** You can keep your main Git history clean by only committing meaningful, project-wide changes. In parallel, you can use `mrcs` to track every single minor edit, auto-save, or experimental rewrite on a single configuration or document file.
- **No Repository Overhead:** Git requires creating a repository, managing `.gitignore` files, and staging files before tracking them. `mrcs` lets you version control any single file instantly in any directory: `mrcs init file.txt` and you're good to go.

---

## Features

- **Zero-Friction Tracking**: Easily track individual files (e.g., `TODO.md`, `changelog.md`, `config.json`).
- **Familiar CLI**: Command syntax modeled after Git (`init`, `commit`, `log`, `diff`, `status`, `restore`, etc.).
- **Smart Auto-Resolution**: Omit the file argument if there's only one tracked file in the directory.
- **Unified Colorized Diffs**: Modern, readable, and colored diff output.
- **Android/Termux Friendly**: Built-in workaround (`finalize_transaction`) for Termux/Android shared storage filesystem limitations.
- **No Terminology Confusion**: Hides ancient Unix RCS terms like `check-in` (`ci`) and `check-out` (`co`).
- **Binary File Support**: Automatically detects binary files using a NUL-byte scanner and configures RCS binary mode (`-kb`) to preserve raw bytes and disable keyword expansion.

---

## Stage 1 Architecture

In **Stage 1**, `mrcs` acts as a clean wrapper around standard GNU RCS utilities:
- `mrcs init file` executes `rcs -i -t-...` and `rcs -U` to set up lock-free version control.
- `mrcs commit` executes `ci -u`.
- `mrcs restore` executes `co -f -u -r`.
- `mrcs log` parses and formats raw `rlog` output.
- `mrcs diff` executes `rcsdiff -u` and colorizes the output.

By using POSIX `fork()` and `execvp()` instead of standard shell wrappers (`system()`), `mrcs` is safe from shell injection and highly robust.

---

## Getting Started

### Prerequisites

Ensure you have GNU RCS installed on your system. In Termux or Debian/Ubuntu:

```bash
pkg install rcs
# or
apt install rcs
```

### Installation

#### Option 1: Quick Install (Recommended)
You can install `mrcs` with a single command:

```bash
curl -fsSL https://raw.githubusercontent.com/estiaksoyeb/mrcs/main/install.sh | bash
```

#### Option 2: Build from Source
Alternatively, clone this repository and compile the binary:

```bash
make
```

This compiles a standalone binary named `mrcs`. You can copy it to your `PATH` (e.g., `/usr/local/bin` or `~/bin`).

---

## Commands

### `mrcs init <file>`
Initialize tracking for a file. This creates a standard `RCS/` directory in the parent folder to keep the workspace clean.
```bash
mrcs init TODO.md
```

### `mrcs commit [file] [-m "message"]`
Commit changes. If `-m` is omitted, `mrcs` launches your preferred terminal text editor (defined by `$VISUAL` or `$EDITOR`, falling back to `nano`). Comments starting with `#` are ignored, and an empty message aborts the commit.
```bash
mrcs commit TODO.md -m "Add details on version 1.0"
```

### `mrcs log [file]`
Displays a pretty version of the revision history.
```bash
mrcs log
```

### `mrcs diff [args]`
Compare revisions or the working copy. Supports both positional and explicit `-r` formats:
- `mrcs diff`: Compares working file to the latest revision.
- `mrcs diff 1.1` (or `mrcs diff -r 1.1`): Compares working file to revision 1.1.
- `mrcs diff 1.1 1.2` (or `mrcs diff -r 1.1 -r 1.2`): Compares revision 1.1 with 1.2.
- `mrcs diff 1.1 TODO.md` (or `mrcs diff -r 1.1 TODO.md`): Compares revision 1.1 of `TODO.md`.

### `mrcs show [file]`
Show the diff introduced by the most recent commit (comparing the revision immediately before HEAD to HEAD).
```bash
mrcs show TODO.md
```

### `mrcs status [file]`
Check the status of tracked files. If no file is specified, it displays a summary of status and current revisions for all tracked files in the directory.
```bash
mrcs status
```

### `mrcs current [file]`
Display the current HEAD revision number of the file.
```bash
mrcs current
```

### `mrcs list`
List all tracked files in the current directory along with their status and current revision.
```bash
mrcs list
```

### `mrcs restore <rev> [file]`
Restore the working copy of a file to an older revision.
```bash
mrcs restore 1.1
```

### `mrcs delete <rev> [file]`
Permanently delete a revision from history.
```bash
mrcs delete 1.2
```

---

## Running Tests

Run the integration tests to verify correctness:

```bash
./test_mrcs.sh
```
