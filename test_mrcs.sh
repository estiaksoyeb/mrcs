#!/bin/bash
set -e

# Setup a clean test workspace inside the directory
TEST_DIR="test_sandbox"
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"
cd "$TEST_DIR"

# Path to the compiled mrcs binary
MRCS="../mrcs"

echo "=== Running mrcs Integration Tests ==="

# 1. Test mrcs help
echo "Testing help..."
$MRCS help > /dev/null

# 2. Test mrcs init
echo "Testing init..."
$MRCS init test_file.txt
if [ ! -f test_file.txt ]; then
    echo "FAILED: test_file.txt was not created"
    exit 1
fi
if [ ! -f RCS/test_file.txt,v ]; then
    echo "FAILED: RCS/test_file.txt,v was not created"
    exit 1
fi

# 3. Test mrcs status before first commit
echo "Testing status before first commit..."
status_out=$($MRCS status test_file.txt)
if [[ ! "$status_out" =~ "uncommitted" ]]; then
    echo "FAILED: Expected status 'uncommitted', got '$status_out'"
    exit 1
fi

# 4. Test mrcs commit (initial)
echo "Testing initial commit..."
$MRCS commit test_file.txt -m "Initial commit message"
status_out=$($MRCS status test_file.txt)
if [[ ! "$status_out" =~ "clean" ]]; then
    echo "FAILED: Expected status 'clean' after initial commit, got '$status_out'"
    exit 1
fi

# 5. Test mrcs current
echo "Testing current revision..."
current_rev=$($MRCS current test_file.txt)
if [ "$current_rev" != "1.1" ]; then
    echo "FAILED: Expected current revision '1.1', got '$current_rev'"
    exit 1
fi

# 5b. Test mrcs show after initial commit (only one revision exists)
echo "Testing show after initial commit..."
show_out=$($MRCS show test_file.txt)
if [[ ! "$show_out" =~ "Only one revision (1.1) exists for test_file.txt. Nothing to show." ]]; then
    echo "FAILED: Expected 'Only one revision' message, got '$show_out'"
    exit 1
fi

# 6. Test file modification and status check
echo "Testing modification status..."
echo "Adding new line of text" > test_file.txt
status_out=$($MRCS status test_file.txt)
if [[ ! "$status_out" =~ "modified" ]]; then
    echo "FAILED: Expected status 'modified', got '$status_out'"
    exit 1
fi

# 7. Test mrcs diff
echo "Testing diff output..."
diff_out=$($MRCS diff test_file.txt)
if [[ ! "$diff_out" =~ "+Adding new line of text" ]]; then
    echo "FAILED: Diff output does not contain expected added line: $diff_out"
    exit 1
fi

# 8. Test mrcs commit using file auto-resolution (no file arg)
echo "Testing commit with auto-resolution..."
$MRCS commit -m "Second commit message"
status_out=$($MRCS status)
if [[ ! "$status_out" =~ "clean" ]]; then
    echo "FAILED: Expected status 'clean' after second commit, got '$status_out'"
    exit 1
fi

# 9. Test mrcs current (should be 1.2)
current_rev=$($MRCS current)
if [ "$current_rev" != "1.2" ]; then
    echo "FAILED: Expected current revision '1.2', got '$current_rev'"
    exit 1
fi

# 9b. Test mrcs show after second commit
echo "Testing show after second commit..."
show_out=$($MRCS show)
if [[ ! "$show_out" =~ "Showing changes from 1.1 to 1.2 (last commit):" ]]; then
    echo "FAILED: Expected show header, got '$show_out'"
    exit 1
fi
if [[ ! "$show_out" =~ "+Adding new line of text" ]]; then
    echo "FAILED: Expected diff in show output, got '$show_out'"
    exit 1
fi

# 10. Test mrcs log
echo "Testing log parsing..."
log_out=$($MRCS log)
if [[ ! "$log_out" =~ "Initial commit message" ]] || [[ ! "$log_out" =~ "Second commit message" ]]; then
    echo "FAILED: Log does not contain all commit messages: $log_out"
    exit 1
fi

# 11. Test mrcs restore
echo "Testing restore to 1.1..."
$MRCS restore 1.1
content=$(cat test_file.txt)
if [ "$content" != "" ]; then
    echo "FAILED: Expected restored content to be empty (initial touch), got '$content'"
    exit 1
fi

# Restore back to 1.2 for testing deletion
$MRCS restore 1.2

# 12. Test mrcs list
echo "Testing list..."
list_out=$($MRCS list)
if [[ ! "$list_out" =~ "test_file.txt" ]] || [[ ! "$list_out" =~ "revision 1.2" ]]; then
    echo "FAILED: List output unexpected: $list_out"
    exit 1
fi

# 13. Test mrcs delete revision
echo "Testing delete revision 1.2..."
$MRCS delete 1.2 --force
current_rev=$($MRCS current)
if [ "$current_rev" != "1.1" ]; then
    echo "FAILED: Expected current revision '1.1' after deleting 1.2, got '$current_rev'"
    exit 1
fi

# 14. Test binary file tracking (no keyword expansions, exact bytes preserved)
echo "Testing binary file tracking..."
# Create a binary file containing NUL bytes and keywords like $Id$
printf "BinaryData\0\$Id\$\0MoreData" > test_binary.bin
$MRCS init test_binary.bin
$MRCS commit test_binary.bin -m "Commit binary file"

# Check status
status_out=$($MRCS status test_binary.bin)
if [[ ! "$status_out" =~ "clean" ]]; then
    echo "FAILED: Expected status 'clean' for binary file, got '$status_out'"
    exit 1
fi

# Modify binary file
printf "BinaryData\0\$Id\$\0ModifiedData" > test_binary.bin
$MRCS commit test_binary.bin -m "Modify binary file"

# Restore to 1.1 and check if NUL bytes and exact keywords are preserved (no expansion of $Id$)
$MRCS restore 1.1 test_binary.bin
printf "BinaryData\0\$Id\$\0MoreData" > expected_binary.bin
if ! cmp -s test_binary.bin expected_binary.bin; then
    echo "FAILED: Binary content or keyword expansion check failed for restored 1.1"
    rm -f expected_binary.bin
    exit 1
fi
rm -f expected_binary.bin

# Cleanup sandbox
cd ..
rm -rf "$TEST_DIR"

echo "=== All Tests Passed Successfully ==="
