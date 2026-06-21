# mrcs Roadmap

This document outlines the architectural roadmap for the transition from a wrapper utility to a fully independent native revision control system.

---

## Stage 2: Replace the Reader (Target)

**Goal:** Remove dependencies on `rlog` and `rcsdiff`.

### Implementation Plan
- **Native RCS Parser**: Write a C parser in `mrcs` that directly opens the `,v` archive file. The RCS archive uses a standard syntax resembling S-expressions or key-value structures.
  - Parse the header (`head`, `branch`, `locks`).
  - Parse the metadata block (revision number, date, author, state, branches).
  - Parse log messages and text deltas.
- **Natively Generate Log Output**: Reformat parsed logs to match our current beautiful colorized Git-like structure without calling the `rlog` binary.
- **Native Diff Implementation**: Instead of calling `rcsdiff`, compile a basic diff algorithm (like the Myers Diff Algorithm) inside `mrcs` to generate unified diff outputs.

---

## Stage 3: Replace the Writer (Independence)

**Goal:** Remove dependencies on `rcs` and `ci`. At this stage, `mrcs` becomes completely independent from external RCS binaries.

### Implementation Plan
- **Native Revision Creation**: When committing, `mrcs` will directly open the `,v` file, insert the metadata, and write the text.
- **Snapshot Storage**: To maintain simplicity and correctness, the initial writer implementation can store full snapshots of revisions inside the archive file instead of complex reverse/forward deltas.
- **Compatibility Preservation**: The native writer must write archive files in the standard GNU RCS format, allowing existing RCS tools to continue reading them.

---

## Stage 4: Native Archive Format (The Milestone)

**Goal:** Shift away from the GNU RCS `,v` file format to a modern archive format designed for single-file versioning.

### Design Options Comparison

We will choose between three candidate architectures:

| Option | Layout | Advantages | Disadvantages |
| :--- | :--- | :--- | :--- |
| **Option A: File-System Folders** | `~/.mrcs/project/file.txt/revisions/`, `metadata.json` | - Humans can view historical snapshots directly.<br>- Simplest to debug.<br>- Easy to extend metadata. | - Higher disk footprint (no compression/deltas).<br>- Creating many small files is slower on mechanical drives. |
| **Option B: SQLite Backend** | `~/.mrcs/mrcs.db` | - Atomic commits (transactional safety by default).<br>- Fast querying of log and metadata.<br>- Single database file for all tracked files. | - Dependency on the SQLite library.<br>- Database corruption risks if file handles crash.<br>- Hard to edit metadata manually. |
| **Option C: Content-Addressable Storage (Git-like)** | `~/.mrcs/objects/[hash]` | - Dedupes files naturally.<br>- Cryptographic integrity (corruption resistant).<br>- Future-proof against malware/tampering. | - Highly complex to implement in C.<br>- History lookup requires traversal of hash trees. |

### Architectural Recommendation
**Option B (SQLite)** is recommended if we prioritize transaction safety, database speeds, and query simplicity. **Option A (Folders + JSON)** is recommended if we prioritize zero external dependencies and extreme transparency.

---

## Future Features List
After Stage 4, these features can be progressively introduced:
- **Named Tags & Snapshots**: Tag specific revisions (e.g. `release-v1.0`).
- **Markdown-Aware Diffing**: Richer console coloring for Markdown files.
- **Cron Auto-Snapshots**: Automatically commit files every X minutes if they have changes.
- **JSON Output Mode**: Output logs or diffs in JSON for scripting integration.
