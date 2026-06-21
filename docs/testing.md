# mrcs Testing Specifications

To ensure the wrapper layer and system workarounds are rock-solid, `mrcs` uses an automated integration test suite located at [test_mrcs.sh](file:///root/Projects/mrcs/test_mrcs.sh).

## How to Run Tests

Ensure `rcs` is installed, compile the C executable, and run the test script:

```bash
make
./test_mrcs.sh
```

---

## Test Mapping & Coverage

The test suite spins up an isolated sandbox environment (`test_sandbox/`) and validates the following mapping of commands to functional assertions:

| Tested Command | Sandbox Check / Assertion | Purpose / Validation |
| :--- | :--- | :--- |
| **`mrcs help`** | Verify exit status is `0`. | Basic CLI availability and command dispatch check. |
| **`mrcs init`** | Check for `test_file.txt` and `RCS/test_file.txt,v` creation. | Verifies automatic directory creation, file creation, and RCS tracking initialization. |
| **`mrcs status`** | Match text output containing `uncommitted`. | Validates state reporting on a newly initialized but empty repository. |
| **`mrcs commit`** | Verify output matches `[test_file.txt 1.1]`. | Tests initial check-in process, revision matching, and output formatting. |
| **`mrcs status`** | Match text output containing `clean`. | Verifies repository state updates to `clean` after commit. |
| **`mrcs current`** | Verify output matches `1.1`. | Tests head revision retrieval and output. |
| **`mrcs diff`** | Verify unified diff shows `+Adding new line of text`. | Validates unified diff extraction and color formatting. |
| **`mrcs commit`** | Omit the file name parameter. Verify auto-resolution selects `test_file.txt`. | Assures that single tracked files are automatically resolved when file args are omitted. |
| **`mrcs current`** | Match `1.2`. | Verifies revision incremental logic is correctly tracked. |
| **`mrcs log`** | Match both `Initial commit message` and `Second commit message`. | Validates nested line splitter and `rlog` parsing logic. |
| **`mrcs restore`** | Restore to `1.1`. Verify file content reverts to initial empty string. | Validates checkout, revision extraction, and non-strict (writable) restoration. |
| **`mrcs list`** | Verify `test_file.txt` (revision 1.2) [clean] appears. | Validates directory scanning, status computation, and tabulation formatting. |
| **`mrcs delete`** | Delete version `1.2` with `--force`. Verify `mrcs current` reverts to `1.1`. | Confirms history pruning, prompt bypassing, and file update safety. |

---

## Sandbox Cleanup

The script automatically executes:
```bash
rm -rf test_sandbox
```
upon exit to prevent polluting the repository.
