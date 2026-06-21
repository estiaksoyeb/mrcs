#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#define BUFFER_SIZE 4096

/* Color definitions */
#define ANSI_GREEN  "\033[32m"
#define ANSI_YELLOW "\033[33m"
#define ANSI_RED    "\033[31m"
#define ANSI_BLUE   "\033[34m"
#define ANSI_CYAN   "\033[36m"
#define ANSI_BOLD   "\033[1m"
#define ANSI_RESET  "\033[0m"

/* Structs for path and file list management */
typedef struct {
    const char *start;
    size_t length;
} LineView;

typedef struct {
    LineView *lines;
    int count;
} LineList;

LineList split_lines(const char *buf);

typedef struct {
    char **names;
    int count;
} TrackedFiles;

static void add_file(TrackedFiles *tf, const char *name) {
    size_t len = strlen(name);
    if (len <= 2 || strcmp(name + len - 2, ",v") != 0) return;
    
    char *base = strndup(name, len - 2);
    
    /* Check for duplicates */
    for (int i = 0; i < tf->count; i++) {
        if (strcmp(tf->names[i], base) == 0) {
            free(base);
            return;
        }
    }
    
    char **new_names = realloc(tf->names, sizeof(char*) * (tf->count + 1));
    if (new_names) {
        tf->names = new_names;
        tf->names[tf->count++] = base;
    } else {
        free(base);
    }
}

static void print_revision(int *in_revision, int *first_print, const char *rev_num, const char *author, const char *date, char **msg_buf, size_t *msg_len) {
    if (!*in_revision) return;
    if (!*first_print) printf("\n");
    *first_print = 0;
    
    if (isatty(STDOUT_FILENO)) {
        printf("%srevision %s%s\n", ANSI_YELLOW, rev_num, ANSI_RESET);
    } else {
        printf("revision %s\n", rev_num);
    }
    printf("Author: %s\n", author);
    printf("Date:   %s\n", date);
    printf("\n");
    
    if (*msg_buf) {
        LineList msg_ll = split_lines(*msg_buf);
        for (int k = 0; k < msg_ll.count; k++) {
            printf("    %.*s\n", (int)msg_ll.lines[k].length, msg_ll.lines[k].start);
        }
        free(msg_ll.lines);
        free(*msg_buf);
        *msg_buf = NULL;
        *msg_len = 0;
    }
}

static int is_revision(const char *s) {
    if (!s || *s == '\0') return 0;
    if (!(*s >= '0' && *s <= '9')) return 0;
    while (*s) {
        if (!((*s >= '0' && *s <= '9') || *s == '.')) return 0;
        s++;
    }
    return 1;
}

/* Core helper to run commands and capture stdout/stderr safely (prevents shell injection) */
int run_command(const char *const argv[], char **out_stdout, char **out_stderr) {
    int pipe_out[2];
    int pipe_err[2];
    
    if (pipe(pipe_out) == -1) return -1;
    if (pipe(pipe_err) == -1) {
        close(pipe_out[0]);
        close(pipe_out[1]);
        return -1;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        close(pipe_out[0]);
        close(pipe_out[1]);
        close(pipe_err[0]);
        close(pipe_err[1]);
        return -1;
    }
    
    if (pid == 0) {
        /* Child process */
        dup2(pipe_out[1], STDOUT_FILENO);
        dup2(pipe_err[1], STDERR_FILENO);
        
        close(pipe_out[0]);
        close(pipe_out[1]);
        close(pipe_err[0]);
        close(pipe_err[1]);
        
        execvp(argv[0], (char *const *)argv);
        /* If execvp returns, it failed */
        perror("execvp");
        exit(127);
    } else {
        /* Parent process */
        close(pipe_out[1]);
        close(pipe_err[1]);
        
        char *stdout_buf = NULL;
        size_t stdout_size = 0;
        char *stderr_buf = NULL;
        size_t stderr_size = 0;
        
        char temp[BUFFER_SIZE];
        ssize_t bytes_read;
        
        /* Read stdout */
        while ((bytes_read = read(pipe_out[0], temp, sizeof(temp) - 1)) > 0) {
            char *new_buf = realloc(stdout_buf, stdout_size + bytes_read + 1);
            if (!new_buf) {
                free(stdout_buf);
                stdout_buf = NULL;
                break;
            }
            stdout_buf = new_buf;
            memcpy(stdout_buf + stdout_size, temp, bytes_read);
            stdout_size += bytes_read;
            stdout_buf[stdout_size] = '\0';
        }
        close(pipe_out[0]);
        
        /* Read stderr */
        while ((bytes_read = read(pipe_err[0], temp, sizeof(temp) - 1)) > 0) {
            char *new_buf = realloc(stderr_buf, stderr_size + bytes_read + 1);
            if (!new_buf) {
                free(stderr_buf);
                stderr_buf = NULL;
                break;
            }
            stderr_buf = new_buf;
            memcpy(stderr_buf + stderr_size, temp, bytes_read);
            stderr_size += bytes_read;
            stderr_buf[stderr_size] = '\0';
        }
        close(pipe_err[0]);
        
        int status;
        waitpid(pid, &status, 0);
        
        if (out_stdout) *out_stdout = stdout_buf ? stdout_buf : strdup("");
        else free(stdout_buf);
        
        if (out_stderr) *out_stderr = stderr_buf ? stderr_buf : strdup("");
        else free(stderr_buf);
        
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        return -1;
    }
}

/* Helper to split a buffer into LineViews without modifying the buffer */
LineList split_lines(const char *buf) {
    LineList ll;
    ll.lines = NULL;
    ll.count = 0;
    
    if (!buf) return ll;
    
    const char *p = buf;
    while (*p != '\0') {
        const char *start = p;
        while (*p != '\n' && *p != '\0') {
            p++;
        }
        size_t len = p - start;
        
        LineView *new_lines = realloc(ll.lines, sizeof(LineView) * (ll.count + 1));
        if (!new_lines) {
            free(ll.lines);
            ll.lines = NULL;
            ll.count = 0;
            return ll;
        }
        ll.lines = new_lines;
        ll.lines[ll.count].start = start;
        ll.lines[ll.count].length = len;
        ll.count++;
        
        if (*p == '\n') p++;
    }
    return ll;
}

/* Workaround for Android/Termux rename issue on shared filesystems.
   Updates target archive from temporary lock file if it exists. */
void finalize_transaction(const char *file_path) {
    char *path_copy = strdup(file_path);
    char *filename = strrchr(path_copy, '/');
    char *parent = NULL;
    if (filename) {
        *filename = '\0';
        filename++;
        parent = path_copy;
    } else {
        filename = path_copy;
        parent = ".";
    }
    
    char temp_path[BUFFER_SIZE];
    char target_path[BUFFER_SIZE];
    struct stat st;
    
    /* Candidate 1: parent/RCS/,filename, -> parent/RCS/filename,v */
    snprintf(temp_path, sizeof(temp_path), "%s/RCS/,%s,", parent, filename);
    snprintf(target_path, sizeof(target_path), "%s/RCS/%s,v", parent, filename);
    if (stat(temp_path, &st) == 0) {
        struct stat target_st;
        if (stat(target_path, &target_st) == 0) {
            chmod(target_path, 0644);
        }
        if (rename(temp_path, target_path) == 0) {
            chmod(target_path, 0444);
        }
    }
    
    /* Candidate 2: parent/,filename, -> parent/filename,v */
    snprintf(temp_path, sizeof(temp_path), "%s/,%s,", parent, filename);
    snprintf(target_path, sizeof(target_path), "%s/%s,v", parent, filename);
    if (stat(temp_path, &st) == 0) {
        struct stat target_st;
        if (stat(target_path, &target_st) == 0) {
            chmod(target_path, 0644);
        }
        if (rename(temp_path, target_path) == 0) {
            chmod(target_path, 0444);
        }
    }
    
    free(path_copy);
}

/* Checks if the file is tracked (i.e. has a corresponding RCS archive file) */
int is_tracked(const char *file_path) {
    char *path_copy = strdup(file_path);
    char *filename = strrchr(path_copy, '/');
    char *parent = NULL;
    if (filename) {
        *filename = '\0';
        filename++;
        parent = path_copy;
    } else {
        filename = path_copy;
        parent = ".";
    }
    
    char check_path1[BUFFER_SIZE];
    char check_path2[BUFFER_SIZE];
    struct stat st;
    
    snprintf(check_path1, sizeof(check_path1), "%s/RCS/%s,v", parent, filename);
    snprintf(check_path2, sizeof(check_path2), "%s/%s,v", parent, filename);
    
    int tracked = (stat(check_path1, &st) == 0 || stat(check_path2, &st) == 0);
    free(path_copy);
    return tracked;
}

/* Finds all tracked files in the current working directory or RCS/ folder */
TrackedFiles find_tracked_files() {
    TrackedFiles tf;
    tf.names = NULL;
    tf.count = 0;
    
    /* Read current directory */
    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            /* Note: We read all files; d_type check is omitted as some filesystems do not populate it */
            struct stat st;
            if (stat(entry->d_name, &st) == 0 && S_ISREG(st.st_mode)) {
                add_file(&tf, entry->d_name);
            }
        }
        closedir(dir);
    }
    
    /* Read RCS directory */
    dir = opendir("RCS");
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            char path[BUFFER_SIZE];
            snprintf(path, sizeof(path), "RCS/%s", entry->d_name);
            struct stat st;
            if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
                add_file(&tf, entry->d_name);
            }
        }
        closedir(dir);
    }
    
    /* Bubble sort names alphabetically */
    for (int i = 0; i < tf.count - 1; i++) {
        for (int j = i + 1; j < tf.count; j++) {
            if (strcmp(tf.names[i], tf.names[j]) > 0) {
                char *temp = tf.names[i];
                tf.names[i] = tf.names[j];
                tf.names[j] = temp;
            }
        }
    }
    
    return tf;
}

void free_tracked_files(TrackedFiles tf) {
    for (int i = 0; i < tf.count; i++) {
        free(tf.names[i]);
    }
    free(tf.names);
}

/* Resolves which file to track: returns dynamic string containing filename.
   Exits with error if file_arg is empty and multiple or no files are tracked. */
char *resolve_file(const char *file_arg) {
    if (file_arg && strlen(file_arg) > 0) {
        return strdup(file_arg);
    }
    
    TrackedFiles tf = find_tracked_files();
    if (tf.count == 0) {
        fprintf(stderr, "%sError: No tracked files found in the current directory.%s\n", ANSI_RED, ANSI_RESET);
        fprintf(stderr, "Use 'mrcs init <file>' to start tracking a file.\n");
        exit(1);
    } else if (tf.count == 1) {
        char *res = strdup(tf.names[0]);
        free_tracked_files(tf);
        return res;
    } else {
        fprintf(stderr, "%sError: Multiple tracked files found in this directory:%s\n", ANSI_RED, ANSI_RESET);
        for (int i = 0; i < tf.count; i++) {
            fprintf(stderr, "  - %s\n", tf.names[i]);
        }
        fprintf(stderr, "\nPlease specify which file to use, e.g.:\n");
        fprintf(stderr, "  mrcs <command> <file>\n");
        free_tracked_files(tf);
        exit(1);
    }
}

/* Checks if the tracked file has at least one committed revision */
int has_revisions(const char *file_path) {
    if (!is_tracked(file_path)) return 0;
    
    const char *argv[] = {"rlog", "-h", file_path, NULL};
    char *stdout_str = NULL;
    char *stderr_str = NULL;
    int code = run_command(argv, &stdout_str, &stderr_str);
    
    int has_revs = 0;
    if (code == 0 && stdout_str) {
        LineList ll = split_lines(stdout_str);
        for (int i = 0; i < ll.count; i++) {
            LineView lv = ll.lines[i];
            if (lv.length >= 16 && strncmp(lv.start, "total revisions:", 16) == 0) {
                int total = 0;
                /* Create dynamic copy of the line to parse */
                char *line_copy = strndup(lv.start, lv.length);
                if (sscanf(line_copy, "total revisions: %d", &total) == 1) {
                    has_revs = (total > 0);
                }
                free(line_copy);
                break;
            }
        }
        free(ll.lines);
    }
    free(stdout_str);
    free(stderr_str);
    return has_revs;
}

/* Retrieve the latest revision number */
char *get_current_rev(const char *file_path) {
    if (!is_tracked(file_path) || !has_revisions(file_path)) {
        return NULL;
    }
    
    const char *argv[] = {"rlog", "-h", file_path, NULL};
    char *stdout_str = NULL;
    char *stderr_str = NULL;
    int code = run_command(argv, &stdout_str, &stderr_str);
    
    char *current = NULL;
    if (code == 0 && stdout_str) {
        LineList ll = split_lines(stdout_str);
        for (int i = 0; i < ll.count; i++) {
            LineView lv = ll.lines[i];
            if (lv.length > 5 && strncmp(lv.start, "head:", 5) == 0) {
                int len = (int)lv.length - 5;
                const char *head_start = lv.start + 5;
                while (len > 0 && (*head_start == ' ' || *head_start == '\t')) {
                    head_start++;
                    len--;
                }
                /* Trim trailing spaces/newlines */
                while (len > 0 && (head_start[len-1] == ' ' || head_start[len-1] == '\t' || head_start[len-1] == '\r' || head_start[len-1] == '\n')) {
                    len--;
                }
                current = strndup(head_start, len);
                break;
            }
        }
        free(ll.lines);
    }
    free(stdout_str);
    free(stderr_str);
    return current;
}

/* Command: init */
int cmd_init(const char *file_path) {
    char *path_copy = strdup(file_path);
    char *filename = strrchr(path_copy, '/');
    char *parent = NULL;
    if (filename) {
        *filename = '\0';
        filename++;
        parent = path_copy;
    } else {
        filename = path_copy;
        parent = ".";
    }
    
    /* Ensure parent directories exist */
    struct stat st;
    if (stat(parent, &st) != 0) {
        #ifdef _WIN32
        mkdir(parent);
        #else
        mkdir(parent, 0755);
        #endif
    }
    
    /* Create standard RCS directory to keep workspace clean */
    char rcs_dir[BUFFER_SIZE];
    snprintf(rcs_dir, sizeof(rcs_dir), "%s/RCS", parent);
    if (stat(rcs_dir, &st) != 0) {
        #ifdef _WIN32
        mkdir(rcs_dir);
        #else
        mkdir(rcs_dir, 0755);
        #endif
    }
    
    /* Touch target file if it does not exist yet */
    int created_empty = 0;
    if (stat(file_path, &st) != 0) {
        FILE *f = fopen(file_path, "w");
        if (f) {
            fclose(f);
            created_empty = 1;
        } else {
            fprintf(stderr, "%sError: Could not create file %s%s\n", ANSI_RED, file_path, ANSI_RESET);
            free(path_copy);
            return 1;
        }
    }
    
    /* Run 'rcs -i -t-Tracked by mrcs file_path' (non-interactive init) */
    const char *argv1[] = {"rcs", "-i", "-t-Tracked by mrcs", file_path, NULL};
    char *stderr_str = NULL;
    int code = run_command(argv1, NULL, &stderr_str);
    if (code != 0) {
        fprintf(stderr, "%sError: RCS initialization failed: %s%s\n", ANSI_RED, stderr_str ? stderr_str : "", ANSI_RESET);
        free(stderr_str);
        free(path_copy);
        return 1;
    }
    free(stderr_str);
    
    /* Set locking mode to non-strict (unlocked workflow) */
    const char *argv2[] = {"rcs", "-U", file_path, NULL};
    code = run_command(argv2, NULL, &stderr_str);
    if (code != 0) {
        fprintf(stderr, "%sError: Failed to set non-strict locking: %s%s\n", ANSI_RED, stderr_str ? stderr_str : "", ANSI_RESET);
        free(stderr_str);
        free(path_copy);
        return 1;
    }
    free(stderr_str);
    
    finalize_transaction(file_path);
    
    int tty = isatty(STDOUT_FILENO);
    if (created_empty) {
        if (tty) printf("%sCreated empty file: %s%s\n", ANSI_GREEN, file_path, ANSI_RESET);
        else printf("Created empty file: %s\n", file_path);
    }
    if (tty) printf("%sInitialized tracking for: %s%s\n", ANSI_GREEN, file_path, ANSI_RESET);
    else printf("Initialized tracking for: %s\n", file_path);
    
    free(path_copy);
    return 0;
}

/* Prompt commit message using user's editor (with nano fallback) */
char *prompt_commit_message(const char *filename) {
    const char *editor = getenv("VISUAL");
    if (!editor) editor = getenv("EDITOR");
    if (!editor) editor = "nano";
    
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
    
    char temp_path[BUFFER_SIZE];
    snprintf(temp_path, sizeof(temp_path), "%s/mrcs_commit_XXXXXX", tmpdir);
    
    int fd = mkstemp(temp_path);
    if (fd == -1) {
        snprintf(temp_path, sizeof(temp_path), "./.mrcs_commit_XXXXXX");
        fd = mkstemp(temp_path);
        if (fd == -1) {
            perror("mkstemp");
            return NULL;
        }
    }
    
    FILE *fp = fdopen(fd, "w");
    if (!fp) {
        close(fd);
        unlink(temp_path);
        return NULL;
    }
    
    fprintf(fp, "\n");
    fprintf(fp, "# Please enter the commit message for your changes. Lines starting\n");
    fprintf(fp, "# with '#' will be ignored, and an empty message aborts the commit.\n");
    fprintf(fp, "#\n");
    fprintf(fp, "# Committing file: %s\n", filename);
    fprintf(fp, "#\n");
    fclose(fp);
    
    /* Parse editor arguments for running execvp */
    char *editor_copy = strdup(editor);
    char *args[64];
    int arg_count = 0;
    char *token = strtok(editor_copy, " \t");
    while (token && arg_count < 60) {
        args[arg_count++] = token;
        token = strtok(NULL, " \t");
    }
    args[arg_count++] = temp_path;
    args[arg_count] = NULL;
    
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        free(editor_copy);
        unlink(temp_path);
        return NULL;
    }
    
    if (pid == 0) {
        execvp(args[0], args);
        perror("execvp editor");
        exit(127);
    }
    
    int status;
    waitpid(pid, &status, 0);
    free(editor_copy);
    
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "%sError: Editor exited with non-zero status or failed to start.%s\n", ANSI_RED, ANSI_RESET);
        unlink(temp_path);
        return NULL;
    }
    
    FILE *read_fp = fopen(temp_path, "r");
    if (!read_fp) {
        unlink(temp_path);
        return NULL;
    }
    
    char *message = NULL;
    size_t msg_len = 0;
    char line[BUFFER_SIZE];
    
    while (fgets(line, sizeof(line), read_fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#') continue;
        
        size_t line_len = strlen(line);
        char *new_msg = realloc(message, msg_len + line_len + 1);
        if (!new_msg) {
            free(message);
            fclose(read_fp);
            unlink(temp_path);
            return NULL;
        }
        message = new_msg;
        strcpy(message + msg_len, line);
        msg_len += line_len;
    }
    fclose(read_fp);
    unlink(temp_path);
    
    /* Trim whitespaces */
    if (message) {
        while (msg_len > 0 && (message[msg_len - 1] == '\n' || message[msg_len - 1] == '\r' || message[msg_len - 1] == ' ' || message[msg_len - 1] == '\t')) {
            message[msg_len - 1] = '\0';
            msg_len--;
        }
        char *start = message;
        while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
            start++;
        }
        if (start != message) {
            char *trimmed = strdup(start);
            free(message);
            message = trimmed;
        }
    }
    
    return message;
}

/* Command: commit */
int cmd_commit(const char *file_path, const char *message_arg) {
    if (!is_tracked(file_path)) {
        fprintf(stderr, "%sError: File '%s' is not tracked by mrcs. Run 'mrcs init %s' first.%s\n", ANSI_RED, file_path, file_path, ANSI_RESET);
        return 1;
    }
    
    /* Prevent commits with no changes if revisions already exist */
    if (has_revisions(file_path)) {
        const char *argv_diff[] = {"rcsdiff", file_path, NULL};
        char *stdout_str = NULL;
        char *stderr_str = NULL;
        int code = run_command(argv_diff, &stdout_str, &stderr_str);
        free(stdout_str);
        free(stderr_str);
        if (code == 0) {
            printf("No changes detected. Nothing to commit.\n");
            return 0;
        }
    }
    
    char *message = NULL;
    if (message_arg) {
        message = strdup(message_arg);
    } else {
        message = prompt_commit_message(file_path);
    }
    
    if (!message || strlen(message) == 0) {
        printf("Aborting commit due to empty commit message.\n");
        free(message);
        return 0;
    }
    
    /* Buffer limit safety check */
    char *msg_flag = NULL;
    if (asprintf(&msg_flag, "-m%s", message) == -1) {
        free(message);
        return 1;
    }
    
    const char *argv_ci[] = {"ci", "-u", msg_flag, file_path, NULL};
    char *stderr_str = NULL;
    int code = run_command(argv_ci, NULL, &stderr_str);
    free(msg_flag);
    
    finalize_transaction(file_path);
    
    if (code != 0) {
        fprintf(stderr, "%sError: Commit failed: %s%s\n", ANSI_RED, stderr_str ? stderr_str : "", ANSI_RESET);
        free(stderr_str);
        free(message);
        return 1;
    }
    
    char rev[64] = "unknown";
    if (stderr_str) {
        char *p = strstr(stderr_str, "revision:");
        if (p) {
            p += 9;
            while (*p == ' ' || *p == '\t') p++;
            int i = 0;
            while ((p[i] >= '0' && p[i] <= '9') || p[i] == '.') {
                rev[i] = p[i];
                i++;
                if (i >= 60) break;
            }
            rev[i] = '\0';
        }
    }
    free(stderr_str);
    
    char *first_line = strdup(message);
    char *newline = strchr(first_line, '\n');
    if (newline) *newline = '\0';
    
    int tty = isatty(STDOUT_FILENO);
    if (tty) printf("%s[%s %s] %s%s\n", ANSI_GREEN, file_path, rev, first_line, ANSI_RESET);
    else printf("[%s %s] %s\n", file_path, rev, first_line);
    
    free(first_line);
    free(message);
    return 0;
}

/* Command: log */
int cmd_log(const char *file_path) {
    if (!is_tracked(file_path)) {
        fprintf(stderr, "%sError: File '%s' is not tracked.%s\n", ANSI_RED, file_path, ANSI_RESET);
        return 1;
    }
    if (!has_revisions(file_path)) {
        printf("No revisions found for %s.\n", file_path);
        return 0;
    }
    
    const char *argv[] = {"rlog", file_path, NULL};
    char *stdout_str = NULL;
    char *stderr_str = NULL;
    int code = run_command(argv, &stdout_str, &stderr_str);
    
    if (code != 0) {
        fprintf(stderr, "%sError: Failed to get log: %s%s\n", ANSI_RED, stderr_str ? stderr_str : "", ANSI_RESET);
        free(stdout_str);
        free(stderr_str);
        return 1;
    }
    free(stderr_str);
    
    LineList ll = split_lines(stdout_str);
    int in_revision = 0;
    char rev_num[64] = "";
    char author[128] = "";
    char date[128] = "";
    char *msg_buf = NULL;
    size_t msg_len = 0;
    int first_print = 1;
    
    for (int i = 0; i < ll.count; i++) {
        LineView lv = ll.lines[i];
        
        if (lv.length >= 77 && strncmp(lv.start, "=============================================================================", 77) == 0) {
            print_revision(&in_revision, &first_print, rev_num, author, date, &msg_buf, &msg_len);
            break;
        }
        
        if (lv.length >= 28 && strncmp(lv.start, "----------------------------", 28) == 0) {
            print_revision(&in_revision, &first_print, rev_num, author, date, &msg_buf, &msg_len);
            
            in_revision = 1;
            rev_num[0] = '\0';
            author[0] = '\0';
            date[0] = '\0';
            
            if (i + 1 < ll.count) {
                i++;
                LineView rev_lv = ll.lines[i];
                if (rev_lv.length > 9 && strncmp(rev_lv.start, "revision ", 9) == 0) {
                    int len = (int)rev_lv.length - 9;
                    if (len > 63) len = 63;
                    snprintf(rev_num, sizeof(rev_num), "%.*s", len, rev_lv.start + 9);
                }
            }
            
            if (i + 1 < ll.count) {
                i++;
                LineView meta_lv = ll.lines[i];
                char *meta_str = strndup(meta_lv.start, meta_lv.length);
                
                char *p_date = strstr(meta_str, "date:");
                if (p_date) {
                    p_date += 5;
                    while (*p_date == ' ' || *p_date == '\t') p_date++;
                    int k = 0;
                    while (p_date[k] != ';' && p_date[k] != '\0') {
                        date[k] = p_date[k];
                        k++;
                    }
                    date[k] = '\0';
                }
                
                char *p_auth = strstr(meta_str, "author:");
                if (p_auth) {
                    p_auth += 7;
                    while (*p_auth == ' ' || *p_auth == '\t') p_auth++;
                    int k = 0;
                    while (p_auth[k] != ';' && p_auth[k] != '\0') {
                        author[k] = p_auth[k];
                        k++;
                    }
                    author[k] = '\0';
                }
                free(meta_str);
            }
        } else if (in_revision) {
            char *new_msg = realloc(msg_buf, msg_len + lv.length + 2);
            if (new_msg) {
                msg_buf = new_msg;
                memcpy(msg_buf + msg_len, lv.start, lv.length);
                msg_len += lv.length;
                msg_buf[msg_len++] = '\n';
                msg_buf[msg_len] = '\0';
            }
        }
    }
    
    free(ll.lines);
    free(stdout_str);
    return 0;
}

/* Command: diff */
int cmd_diff(const char *file_path, const char *rev1, const char *rev2) {
    if (!is_tracked(file_path)) {
        fprintf(stderr, "%sError: File '%s' is not tracked.%s\n", ANSI_RED, file_path, ANSI_RESET);
        return 1;
    }
    
    int arg_count = 0;
    const char *argv[10];
    argv[arg_count++] = "rcsdiff";
    argv[arg_count++] = "-u";
    
    char *r1 = NULL;
    char *r2 = NULL;
    
    if (rev1) {
        if (asprintf(&r1, "-r%s", rev1) == -1) {
            return 1;
        }
        argv[arg_count++] = r1;
    }
    if (rev2) {
        if (asprintf(&r2, "-r%s", rev2) == -1) {
            free(r1);
            return 1;
        }
        argv[arg_count++] = r2;
    }
    argv[arg_count++] = file_path;
    argv[arg_count] = NULL;
    
    char *stdout_str = NULL;
    char *stderr_str = NULL;
    int code = run_command(argv, &stdout_str, &stderr_str);
    
    free(r1);
    free(r2);
    
    if (code == 2) {
        if (stderr_str && (strstr(stderr_str, "no revisions present") || strstr(stdout_str, "no revisions present"))) {
            fprintf(stderr, "%sError: No revisions present in the repository yet.%s\n", ANSI_RED, ANSI_RESET);
        } else {
            fprintf(stderr, "%srcsdiff failed: %s%s\n", ANSI_RED, stderr_str ? stderr_str : "", ANSI_RESET);
        }
        free(stdout_str);
        free(stderr_str);
        return 1;
    }
    free(stderr_str);
    
    if (stdout_str && strlen(stdout_str) > 0) {
        LineList ll = split_lines(stdout_str);
        int tty = isatty(STDOUT_FILENO);
        
        for (int i = 0; i < ll.count; i++) {
            LineView lv = ll.lines[i];
            if (lv.length >= 3 && strncmp(lv.start, "---", 3) == 0) {
                if (lv.length >= 4 && lv.start[3] == ' ') {
                    if (tty) printf("%s%.*s%s\n", ANSI_RED, (int)lv.length, lv.start, ANSI_RESET);
                    else printf("%.*s\n", (int)lv.length, lv.start);
                } else {
                    printf("%.*s\n", (int)lv.length, lv.start);
                }
            } else if (lv.length >= 3 && strncmp(lv.start, "+++", 3) == 0) {
                if (tty) printf("%s%.*s%s\n", ANSI_GREEN, (int)lv.length, lv.start, ANSI_RESET);
                else printf("%.*s\n", (int)lv.length, lv.start);
            } else if (lv.length >= 1 && lv.start[0] == '-') {
                if (tty) printf("%s%.*s%s\n", ANSI_RED, (int)lv.length, lv.start, ANSI_RESET);
                else printf("%.*s\n", (int)lv.length, lv.start);
            } else if (lv.length >= 1 && lv.start[0] == '+') {
                if (tty) printf("%s%.*s%s\n", ANSI_GREEN, (int)lv.length, lv.start, ANSI_RESET);
                else printf("%.*s\n", (int)lv.length, lv.start);
            } else if (lv.length >= 2 && strncmp(lv.start, "@@", 2) == 0) {
                if (tty) printf("%s%.*s%s\n", ANSI_CYAN, (int)lv.length, lv.start, ANSI_RESET);
                else printf("%.*s\n", (int)lv.length, lv.start);
            } else if (lv.length >= 3 && (strncmp(lv.start, "===", 3) == 0 || strncmp(lv.start, "RCS file:", 9) == 0 || strncmp(lv.start, "retrieving", 10) == 0 || strncmp(lv.start, "diff ", 5) == 0)) {
                if (tty) printf("%s%.*s%s\n", ANSI_BLUE, (int)lv.length, lv.start, ANSI_RESET);
                else printf("%.*s\n", (int)lv.length, lv.start);
            } else {
                printf("%.*s\n", (int)lv.length, lv.start);
            }
        }
        free(ll.lines);
    }
    
    free(stdout_str);
    return 0;
}

/* Command: restore */
int cmd_restore(const char *file_path, const char *revision) {
    if (!is_tracked(file_path)) {
        fprintf(stderr, "%sError: File '%s' is not tracked.%s\n", ANSI_RED, file_path, ANSI_RESET);
        return 1;
    }
    
    char *rev_flag = NULL;
    if (asprintf(&rev_flag, "-r%s", revision) == -1) {
        return 1;
    }
    
    const char *argv[] = {"co", "-f", "-u", rev_flag, file_path, NULL};
    char *stderr_str = NULL;
    int code = run_command(argv, NULL, &stderr_str);
    free(rev_flag);
    
    finalize_transaction(file_path);
    
    if (code != 0) {
        fprintf(stderr, "%sError: Restore failed: %s%s\n", ANSI_RED, stderr_str ? stderr_str : "", ANSI_RESET);
        free(stderr_str);
        return 1;
    }
    free(stderr_str);
    
    if (isatty(STDOUT_FILENO)) {
        printf("%sRestored %s to revision %s.%s\n", ANSI_GREEN, file_path, revision, ANSI_RESET);
    } else {
        printf("Restored %s to revision %s.\n", file_path, revision);
    }
    return 0;
}

/* Command: current */
int cmd_current(const char *file_path) {
    char *rev = get_current_rev(file_path);
    if (rev) {
        printf("%s\n", rev);
        free(rev);
        return 0;
    } else {
        printf("No revisions committed yet.\n");
        return 0;
    }
}

/* Retrieve state of tracked file */
int get_status_code(const char *file_path) {
    if (!is_tracked(file_path)) return 0;     /* untracked */
    if (!has_revisions(file_path)) return 1;  /* uncommitted */
    
    const char *argv[] = {"rcsdiff", file_path, NULL};
    char *stdout_str = NULL;
    char *stderr_str = NULL;
    int code = run_command(argv, &stdout_str, &stderr_str);
    free(stdout_str);
    free(stderr_str);
    
    if (code == 0) return 2; /* clean */
    if (code == 1) return 3; /* modified */
    return 4;                /* error */
}

/* Command: status */
int cmd_status(const char *file_path_arg) {
    if (file_path_arg) {
        int code = get_status_code(file_path_arg);
        char *rev = get_current_rev(file_path_arg);
        char rev_str[128];
        if (rev) {
            snprintf(rev_str, sizeof(rev_str), "[%s]", rev);
            free(rev);
        } else {
            strcpy(rev_str, "[no revisions]");
        }
        
        int tty = isatty(STDOUT_FILENO);
        printf("%-20s ", file_path_arg);
        if (tty) printf("%s%-20s%s ", ANSI_CYAN, rev_str, ANSI_RESET);
        else printf("%-20s ", rev_str);
        
        if (code == 2) {
            if (tty) printf("%sclean%s\n", ANSI_GREEN, ANSI_RESET);
            else printf("clean\n");
        } else if (code == 3) {
            if (tty) printf("%smodified%s\n", ANSI_RED, ANSI_RESET);
            else printf("modified\n");
        } else if (code == 1) {
            if (tty) printf("%suncommitted%s\n", ANSI_YELLOW, ANSI_RESET);
            else printf("uncommitted\n");
        } else if (code == 0) {
            if (tty) printf("%suntracked%s\n", ANSI_YELLOW, ANSI_RESET);
            else printf("untracked\n");
        } else {
            if (tty) printf("%serror%s\n", ANSI_RED, ANSI_RESET);
            else printf("error\n");
        }
    } else {
        TrackedFiles tf = find_tracked_files();
        if (tf.count == 0) {
            fprintf(stderr, "%sError: No tracked files found in the current directory.%s\n", ANSI_RED, ANSI_RESET);
            fprintf(stderr, "Use 'mrcs init <file>' to start tracking a file.\n");
            return 1;
        }
        for (int i = 0; i < tf.count; i++) {
            cmd_status(tf.names[i]);
        }
        free_tracked_files(tf);
    }
    return 0;
}

/* Command: list */
int cmd_list() {
    TrackedFiles tf = find_tracked_files();
    if (tf.count == 0) {
        printf("No files are currently being tracked.\n");
        return 0;
    }
    
    int tty = isatty(STDOUT_FILENO);
    if (tty) printf("%sTracked files in the current directory:%s\n", ANSI_BOLD, ANSI_RESET);
    else printf("Tracked files in the current directory:\n");
    
    for (int i = 0; i < tf.count; i++) {
        char *rev = get_current_rev(tf.names[i]);
        char rev_str[128];
        if (rev) {
            snprintf(rev_str, sizeof(rev_str), "revision %s", rev);
            free(rev);
        } else {
            strcpy(rev_str, "no revisions");
        }
        
        int code = get_status_code(tf.names[i]);
        const char *st_str = "unknown";
        const char *st_color = "red";
        if (code == 2) { st_str = "clean"; st_color = "green"; }
        else if (code == 3) { st_str = "modified"; st_color = "red"; }
        else if (code == 1) { st_str = "uncommitted"; st_color = "yellow"; }
        else if (code == 0) { st_str = "untracked"; st_color = "yellow"; }
        
        printf("  ");
        if (tty) printf("%s%-25s%s ", ANSI_CYAN, tf.names[i], ANSI_RESET);
        else printf("%-25s ", tf.names[i]);
        
        printf("(%s) [", rev_str);
        if (tty) {
            const char *color_code = ANSI_RED;
            if (strcmp(st_color, "green") == 0) color_code = ANSI_GREEN;
            else if (strcmp(st_color, "yellow") == 0) color_code = ANSI_YELLOW;
            printf("%s%s%s]\n", color_code, st_str, ANSI_RESET);
        } else {
            printf("%s]\n", st_str);
        }
    }
    free_tracked_files(tf);
    return 0;
}

/* Command: delete */
int cmd_delete(const char *file_path, const char *revision, int force) {
    if (!is_tracked(file_path)) {
        fprintf(stderr, "%sError: File '%s' is not tracked.%s\n", ANSI_RED, file_path, ANSI_RESET);
        return 1;
    }
    
    if (!force) {
        if (isatty(STDOUT_FILENO)) {
            printf("%sWarning: You are about to permanently delete revision %s of %s from history.%s\n", ANSI_YELLOW, revision, file_path, ANSI_RESET);
        } else {
            printf("Warning: You are about to permanently delete revision %s of %s from history.\n", revision, file_path);
        }
        printf("Are you sure? [y/N]: ");
        fflush(stdout);
        char response[64];
        if (fgets(response, sizeof(response), stdin)) {
            char *p = response;
            while (*p == ' ' || *p == '\t') p++;
            if (*p != 'y' && *p != 'Y') {
                printf("Aborted.\n");
                return 0;
            }
        } else {
            printf("Aborted.\n");
            return 0;
        }
    }
    
    char *rev_flag = NULL;
    if (asprintf(&rev_flag, "-o%s", revision) == -1) {
        return 1;
    }
    
    const char *argv[] = {"rcs", rev_flag, file_path, NULL};
    char *stderr_str = NULL;
    int code = run_command(argv, NULL, &stderr_str);
    free(rev_flag);
    
    finalize_transaction(file_path);
    
    if (code != 0) {
        fprintf(stderr, "%sError: Delete revision failed: %s%s\n", ANSI_RED, stderr_str ? stderr_str : "", ANSI_RESET);
        free(stderr_str);
        return 1;
    }
    free(stderr_str);
    
    if (isatty(STDOUT_FILENO)) {
        printf("%sSuccessfully deleted revision %s of %s.%s\n", ANSI_GREEN, revision, file_path, ANSI_RESET);
    } else {
        printf("Successfully deleted revision %s of %s.\n", revision, file_path);
    }
    return 0;
}

/* Command: show */
int cmd_show(const char *file_path) {
    if (!is_tracked(file_path)) {
        fprintf(stderr, "%sError: File '%s' is not tracked.%s\n", ANSI_RED, file_path, ANSI_RESET);
        return 1;
    }
    if (!has_revisions(file_path)) {
        printf("No revisions found for %s.\n", file_path);
        return 0;
    }
    
    char *current = get_current_rev(file_path);
    if (!current) {
        printf("No revisions found for %s.\n", file_path);
        return 0;
    }
    
    int major = 0, minor = 0;
    if (sscanf(current, "%d.%d", &major, &minor) != 2) {
        fprintf(stderr, "%sError: Failed to parse HEAD revision '%s'%s\n", ANSI_RED, current, ANSI_RESET);
        free(current);
        return 1;
    }
    
    if (minor == 1) {
        printf("Only one revision (%s) exists for %s. Nothing to show.\n", current, file_path);
        free(current);
        return 0;
    }
    
    char prev_rev[64];
    snprintf(prev_rev, sizeof(prev_rev), "%d.%d", major, minor - 1);
    
    int tty = isatty(STDOUT_FILENO);
    if (tty) {
        printf("%sShowing changes from %s to %s (last commit):%s\n", ANSI_BOLD, prev_rev, current, ANSI_RESET);
    } else {
        printf("Showing changes from %s to %s (last commit):\n", prev_rev, current);
    }
    
    int code = cmd_diff(file_path, prev_rev, current);
    free(current);
    return code;
}

/* Command: help */
void cmd_help() {
    int tty = isatty(STDOUT_FILENO);
    if (tty) {
        printf("%sModern Revision Control System (mrcs)%s\n\n", ANSI_BOLD, ANSI_RESET);
        printf("%sUsage:%s\n", ANSI_YELLOW, ANSI_RESET);
        printf("  mrcs <command> [arguments]\n\n");
        printf("%sCommands:%s\n", ANSI_YELLOW, ANSI_RESET);
        printf("  %sinit <file>%s              Initialize tracking for a file.\n", ANSI_GREEN, ANSI_RESET);
        printf("  %scommit [file]%s            Save a new revision of a file.\n", ANSI_GREEN, ANSI_RESET);
        printf("    -m \"message\"            Specify the commit message.\n");
        printf("  %slog [file]%s               Show pretty revision history/commit logs.\n", ANSI_GREEN, ANSI_RESET);
        printf("  %sdiff [args]%s              Show unified colorized diff.\n", ANSI_GREEN, ANSI_RESET);
        printf("    -r <rev>                Compare against revision <rev>.\n");
        printf("    -r <rev1> -r <rev2>     Compare <rev1> against <rev2>.\n");
        printf("  %srestore <rev> [file]%s     Restore file to an older revision.\n", ANSI_GREEN, ANSI_RESET);
        printf("  %sshow [file]%s              Show the diff introduced by the last commit.\n", ANSI_GREEN, ANSI_RESET);
        printf("  %sstatus [file]%s            Show status of tracked file(s).\n", ANSI_GREEN, ANSI_RESET);
        printf("  %scurrent [file]%s           Display current revision of a file.\n", ANSI_GREEN, ANSI_RESET);
        printf("  %slist%s                     List all tracked files in the current directory.\n", ANSI_GREEN, ANSI_RESET);
        printf("  %sdelete <rev> [file]%s      Delete a revision from history.\n", ANSI_GREEN, ANSI_RESET);
        printf("    -f, --force             Skip confirmation prompt.\n");
        printf("  %shelp%s                     Show this help message.\n\n", ANSI_GREEN, ANSI_RESET);
        printf("%sDescription:%s\n", ANSI_YELLOW, ANSI_RESET);
        printf("  Version one file with zero friction. mrcs is built for individual files\n");
        printf("  and does not require initializing a full Git repository.\n");
    } else {
        printf("Modern Revision Control System (mrcs)\n\n");
        printf("Usage:\n");
        printf("  mrcs <command> [arguments]\n\n");
        printf("Commands:\n");
        printf("  init <file>              Initialize tracking for a file.\n");
        printf("  commit [file]            Save a new revision of a file.\n");
        printf("    -m \"message\"            Specify the commit message.\n");
        printf("  log [file]               Show pretty revision history/commit logs.\n");
        printf("  diff [args]              Show unified colorized diff.\n");
        printf("    -r <rev>                Compare against revision <rev>.\n");
        printf("    -r <rev1> -r <rev2>     Compare <rev1> against <rev2>.\n");
        printf("  restore <rev> [file]     Restore file to an older revision.\n");
        printf("  show [file]              Show the diff introduced by the last commit.\n");
        printf("  status [file]            Show status of tracked file(s).\n");
        printf("  current [file]           Display current revision of a file.\n");
        printf("  list                     List all tracked files in the current directory.\n");
        printf("  delete <rev> [file]      Delete a revision from history.\n");
        printf("    -f, --force             Skip confirmation prompt.\n");
        printf("  help                     Show this help message.\n\n");
        printf("Description:\n");
        printf("  Version one file with zero friction. mrcs is built for individual files\n");
        printf("  and does not require initializing a full Git repository.\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        cmd_help();
        return 0;
    }
    
    const char *command = argv[1];
    
    if (strcmp(command, "help") == 0 || strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
        cmd_help();
        return 0;
    }
    
    if (strcmp(command, "init") == 0) {
        if (argc < 3) {
            fprintf(stderr, "%sError: Please specify a file to track, e.g.: mrcs init doc.txt%s\n", ANSI_RED, ANSI_RESET);
            return 1;
        }
        return cmd_init(argv[2]);
    }
    
    if (strcmp(command, "commit") == 0) {
        const char *message = NULL;
        const char *file_arg = NULL;
        
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--message") == 0) {
                if (i + 1 < argc) {
                    message = argv[i+1];
                    i++;
                } else {
                    fprintf(stderr, "%sError: -m/--message option requires an argument.%s\n", ANSI_RED, ANSI_RESET);
                    return 1;
                }
            } else if (strncmp(argv[i], "-m", 2) == 0) {
                message = argv[i] + 2;
            } else {
                if (!file_arg) {
                    file_arg = argv[i];
                } else {
                    fprintf(stderr, "%sError: Unexpected argument '%s'.%s\n", ANSI_RED, argv[i], ANSI_RESET);
                    return 1;
                }
            }
        }
        
        char *resolved = resolve_file(file_arg);
        int code = cmd_commit(resolved, message);
        free(resolved);
        return code;
    }
    
    if (strcmp(command, "log") == 0) {
        const char *file_arg = (argc >= 3) ? argv[2] : NULL;
        char *resolved = resolve_file(file_arg);
        int code = cmd_log(resolved);
        free(resolved);
        return code;
    }
    
    if (strcmp(command, "diff") == 0) {
        const char *rev1 = NULL;
        const char *rev2 = NULL;
        const char *pos_args[8];
        int pos_count = 0;
        
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-r") == 0) {
                if (i + 1 < argc) {
                    if (!rev1) rev1 = argv[i+1];
                    else if (!rev2) rev2 = argv[i+1];
                    else {
                        fprintf(stderr, "%sError: Too many revision flags (maximum 2).%s\n", ANSI_RED, ANSI_RESET);
                        return 1;
                    }
                    i++;
                } else {
                    fprintf(stderr, "%sError: -r option requires an argument.%s\n", ANSI_RED, ANSI_RESET);
                    return 1;
                }
            } else if (argv[i][0] == '-' && argv[i][1] == 'r') {
                if (!rev1) rev1 = argv[i] + 2;
                else if (!rev2) rev2 = argv[i] + 2;
                else {
                    fprintf(stderr, "%sError: Too many revision flags (maximum 2).%s\n", ANSI_RED, ANSI_RESET);
                    return 1;
                }
            } else {
                if (pos_count < 8) {
                    pos_args[pos_count++] = argv[i];
                }
            }
        }
        
        char *resolved_file = NULL;
        
        if (pos_count == 0) {
            resolved_file = resolve_file(NULL);
        } else if (pos_count == 1) {
            const char *arg = pos_args[0];
            struct stat st;
            if (stat(arg, &st) == 0 || is_tracked(arg)) {
                resolved_file = strdup(arg);
            } else if (is_revision(arg)) {
                resolved_file = resolve_file(NULL);
                if (!rev1) rev1 = arg;
                else if (!rev2) rev2 = arg;
            } else {
                resolved_file = strdup(arg);
            }
        } else if (pos_count == 2) {
            const char *arg1 = pos_args[0];
            const char *arg2 = pos_args[1];
            struct stat st;
            if (is_revision(arg1) && is_revision(arg2)) {
                resolved_file = resolve_file(NULL);
                if (!rev1) { rev1 = arg1; rev2 = arg2; }
                else if (!rev2) { rev2 = arg1; }
            } else if (is_revision(arg1) && (stat(arg2, &st) == 0 || is_tracked(arg2))) {
                resolved_file = strdup(arg2);
                if (!rev1) rev1 = arg1;
                else if (!rev2) rev2 = arg1;
            } else {
                resolved_file = strdup(arg2);
                if (!rev1) rev1 = arg1;
                else if (!rev2) rev2 = arg1;
            }
        } else if (pos_count >= 3) {
            resolved_file = strdup(pos_args[pos_count - 1]);
            if (!rev1) { rev1 = pos_args[0]; rev2 = pos_args[1]; }
            else if (!rev2) { rev2 = pos_args[0]; }
        }
        
        int code = cmd_diff(resolved_file, rev1, rev2);
        free(resolved_file);
        return code;
    }
    
    if (strcmp(command, "show") == 0) {
        const char *file_arg = (argc >= 3) ? argv[2] : NULL;
        char *resolved = resolve_file(file_arg);
        int code = cmd_show(resolved);
        free(resolved);
        return code;
    }
    
    if (strcmp(command, "restore") == 0) {
        if (argc < 3) {
            fprintf(stderr, "%sError: Please specify a revision to restore, e.g.: mrcs restore 1.1%s\n", ANSI_RED, ANSI_RESET);
            return 1;
        }
        const char *revision = argv[2];
        const char *file_arg = (argc >= 4) ? argv[3] : NULL;
        char *resolved = resolve_file(file_arg);
        int code = cmd_restore(resolved, revision);
        free(resolved);
        return code;
    }
    
    if (strcmp(command, "status") == 0) {
        const char *file_arg = (argc >= 3) ? argv[2] : NULL;
        return cmd_status(file_arg);
    }
    
    if (strcmp(command, "current") == 0) {
        const char *file_arg = (argc >= 3) ? argv[2] : NULL;
        char *resolved = resolve_file(file_arg);
        int code = cmd_current(resolved);
        free(resolved);
        return code;
    }
    
    if (strcmp(command, "list") == 0) {
        return cmd_list();
    }
    
    if (strcmp(command, "delete") == 0) {
        if (argc < 3) {
            fprintf(stderr, "%sError: Please specify a revision to delete, e.g.: mrcs delete 1.1%s\n", ANSI_RED, ANSI_RESET);
            return 1;
        }
        const char *revision = argv[2];
        const char *file_arg = NULL;
        int force = 0;
        
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) {
                force = 1;
            } else {
                if (!file_arg) file_arg = argv[i];
            }
        }
        
        char *resolved = resolve_file(file_arg);
        int code = cmd_delete(resolved, revision, force);
        free(resolved);
        return code;
    }
    
    fprintf(stderr, "%sError: Unknown command '%s'. Run 'mrcs help' for usage.%s\n", ANSI_RED, command, ANSI_RESET);
    return 1;
}
