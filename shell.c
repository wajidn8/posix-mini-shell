#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LINE 1024
#define MAX_ARGS 64

typedef struct {
    char *argv[MAX_ARGS];
    int argc;
    char *input_file;
    char *output_file;
} Command;

/*
 * Read one complete line from the terminal.
 * fgets() keeps the input inside our fixed 1024-byte buffer. If the user
 * enters a longer line, we discard the rest so the next prompt starts cleanly.
 */
static int read_line(char *line, size_t size)
{
    int character;

    if (fgets(line, (int)size, stdin) == NULL) {
        return 0;
    }

    if (strchr(line, '\n') == NULL && !feof(stdin)) {
        while ((character = getchar()) != '\n' && character != EOF) {
            /* Discard the rest of an overlong line. */
        }

        fprintf(stderr, "myshell: input line is too long (maximum 1024 characters)\n");
        line[0] = '\0';
        return 1;
    }

    return 1;
}

/*
 * Reset a Command structure before parsing a new command.
 * The command arguments point into the original input buffer, so there is
 * nothing to free here.
 */
static void initialize_command(Command *command)
{
    int index;

    command->argc = 0;
    command->input_file = NULL;
    command->output_file = NULL;

    for (index = 0; index < MAX_ARGS; index++) {
        command->argv[index] = NULL;
    }
}

/*
 * Convert the input line into one or two commands.
 * Whitespace separates words. The special tokens |, < and > are treated as
 * shell operators. Only one pipe is accepted because that is the scope of
 * this project.
 */
static int parse_line(char *line, Command *first, Command *second, int *has_pipe)
{
    char *save_pointer = NULL;
    char *token;
    Command *current = first;

    initialize_command(first);
    initialize_command(second);
    *has_pipe = 0;

    token = strtok_r(line, " \t\r\n", &save_pointer);

    while (token != NULL) {
        if (strcmp(token, "|") == 0) {
            if (*has_pipe) {
                fprintf(stderr, "myshell: only one pipe is supported\n");
                return -1;
            }

            if (first->argc == 0) {
                fprintf(stderr, "myshell: pipe needs a command before it\n");
                return -1;
            }

            *has_pipe = 1;
            current = second;
        } else if (strcmp(token, "<") == 0) {
            token = strtok_r(NULL, " \t\r\n", &save_pointer);

            if (token == NULL || strcmp(token, "|") == 0 ||
                strcmp(token, "<") == 0 || strcmp(token, ">") == 0) {
                fprintf(stderr, "myshell: expected a file after '<'\n");
                return -1;
            }

            if (current->input_file != NULL) {
                fprintf(stderr, "myshell: input redirection specified more than once\n");
                return -1;
            }

            current->input_file = token;
        } else if (strcmp(token, ">") == 0) {
            token = strtok_r(NULL, " \t\r\n", &save_pointer);

            if (token == NULL || strcmp(token, "|") == 0 ||
                strcmp(token, "<") == 0 || strcmp(token, ">") == 0) {
                fprintf(stderr, "myshell: expected a file after '>'\n");
                return -1;
            }

            if (current->output_file != NULL) {
                fprintf(stderr, "myshell: output redirection specified more than once\n");
                return -1;
            }

            current->output_file = token;
        } else {
            if (current->argc >= MAX_ARGS - 1) {
                fprintf(stderr, "myshell: too many arguments (maximum %d)\n", MAX_ARGS - 1);
                return -1;
            }

            current->argv[current->argc] = token;
            current->argc++;
            current->argv[current->argc] = NULL;
        }

        token = strtok_r(NULL, " \t\r\n", &save_pointer);
    }

    if (*has_pipe && second->argc == 0) {
        fprintf(stderr, "myshell: pipe needs a command after it\n");
        return -1;
    }

    return 0;
}

/*
 * Open the requested redirection files and connect them to standard input
 * or standard output using dup2(). This function is called in a child before
 * execvp(), and may also be used temporarily by a built-in command.
 */
static int handle_redirection(const Command *command)
{
    int file_descriptor;

    if (command->input_file != NULL) {
        file_descriptor = open(command->input_file, O_RDONLY);

        if (file_descriptor == -1) {
            fprintf(stderr, "myshell: cannot open '%s': %s\n",
                    command->input_file, strerror(errno));
            return -1;
        }

        if (dup2(file_descriptor, STDIN_FILENO) == -1) {
            fprintf(stderr, "myshell: dup2 failed for input: %s\n", strerror(errno));
            close(file_descriptor);
            return -1;
        }

        close(file_descriptor);
    }

    if (command->output_file != NULL) {
        file_descriptor = open(command->output_file,
                               O_WRONLY | O_CREAT | O_TRUNC,
                               0644);

        if (file_descriptor == -1) {
            fprintf(stderr, "myshell: cannot open '%s': %s\n",
                    command->output_file, strerror(errno));
            return -1;
        }

        if (dup2(file_descriptor, STDOUT_FILENO) == -1) {
            fprintf(stderr, "myshell: dup2 failed for output: %s\n", strerror(errno));
            close(file_descriptor);
            return -1;
        }

        close(file_descriptor);
    }

    return 0;
}

/*
 * Run commands that belong to the shell itself instead of creating a child.
 * cd is especially important: a child can change its own working directory,
 * but that change disappears when the child exits. The shell process must
 * call chdir() itself for the new directory to affect later commands.
 *
 * Return values:
 *   0 = not a built-in
 *   1 = built-in handled
 *   2 = exit requested
 */
static int handle_builtin(const Command *command)
{
    int saved_stdin;
    int saved_stdout;
    int redirection_active = 0;

    if (command->argc == 0) {
        return 1;
    }

    if (strcmp(command->argv[0], "exit") != 0 &&
        strcmp(command->argv[0], "cd") != 0 &&
        strcmp(command->argv[0], "pwd") != 0 &&
        strcmp(command->argv[0], "echo") != 0) {
        return 0;
    }

    if (command->input_file != NULL || command->output_file != NULL) {
        saved_stdin = dup(STDIN_FILENO);
        saved_stdout = dup(STDOUT_FILENO);

        if (saved_stdin == -1 || saved_stdout == -1) {
            fprintf(stderr, "myshell: failed to save standard file descriptors: %s\n",
                    strerror(errno));

            if (saved_stdin != -1) {
                close(saved_stdin);
            }
            if (saved_stdout != -1) {
                close(saved_stdout);
            }

            return 1;
        }

        if (handle_redirection(command) == -1) {
            dup2(saved_stdin, STDIN_FILENO);
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdin);
            close(saved_stdout);
            return 1;
        }

        redirection_active = 1;
    }

    if (strcmp(command->argv[0], "exit") == 0) {
        if (command->argc > 1) {
            fprintf(stderr, "myshell: exit does not take arguments\n");
        }

        if (redirection_active) {
            fflush(stdout);
            dup2(saved_stdin, STDIN_FILENO);
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdin);
            close(saved_stdout);
        }

        return 2;
    }

    if (strcmp(command->argv[0], "cd") == 0) {
        if (command->argc != 2) {
            fprintf(stderr, "myshell: usage: cd <path>\n");
        } else if (chdir(command->argv[1]) == -1) {
            fprintf(stderr, "myshell: cd: %s\n", strerror(errno));
        }
    } else if (strcmp(command->argv[0], "pwd") == 0) {
        char current_directory[MAX_LINE];

        if (command->argc != 1) {
            fprintf(stderr, "myshell: usage: pwd\n");
        } else if (getcwd(current_directory, sizeof(current_directory)) == NULL) {
            fprintf(stderr, "myshell: pwd: %s\n", strerror(errno));
        } else {
            printf("%s\n", current_directory);
        }
    } else if (strcmp(command->argv[0], "echo") == 0) {
        int index;

        for (index = 1; index < command->argc; index++) {
            printf("%s", command->argv[index]);

            if (index + 1 < command->argc) {
                printf(" ");
            }
        }

        printf("\n");
    }

    if (redirection_active) {
        fflush(stdout);
        dup2(saved_stdin, STDIN_FILENO);
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdin);
        close(saved_stdout);
    }

    return 1;
}

/*
 * Start one ordinary external command. The child replaces itself with the
 * requested program through execvp(). The parent waits for that child before
 * displaying another prompt.
 */
static int execute_command(const Command *command)
{
    pid_t child_process;
    int status;

    child_process = fork();

    if (child_process == -1) {
        fprintf(stderr, "myshell: fork failed: %s\n", strerror(errno));
        return -1;
    }

    if (child_process == 0) {
        if (handle_redirection(command) == -1) {
            _exit(1);
        }

        execvp(command->argv[0], command->argv);

        fprintf(stderr, "myshell: %s: %s\n",
                command->argv[0], strerror(errno));
        _exit(127);
    }

    while (waitpid(child_process, &status, 0) == -1) {
        if (errno != EINTR) {
            fprintf(stderr, "myshell: waitpid failed: %s\n", strerror(errno));
            return -1;
        }
    }

    return 0;
}

/*
 * Run two commands connected by one pipe.
 * pipe() creates two file descriptors: one for reading and one for writing.
 * The first child sends stdout into the pipe, and the second child reads from
 * it as stdin. Both children then use execvp() to become the requested programs.
 */
static int handle_pipe(const Command *first, const Command *second)
{
    int pipe_descriptors[2];
    pid_t first_child;
    pid_t second_child;
    int first_status;
    int second_status;

    if (pipe(pipe_descriptors) == -1) {
        fprintf(stderr, "myshell: pipe failed: %s\n", strerror(errno));
        return -1;
    }

    first_child = fork();

    if (first_child == -1) {
        fprintf(stderr, "myshell: first fork failed: %s\n", strerror(errno));
        close(pipe_descriptors[0]);
        close(pipe_descriptors[1]);
        return -1;
    }

    if (first_child == 0) {
        close(pipe_descriptors[0]);

        if (dup2(pipe_descriptors[1], STDOUT_FILENO) == -1) {
            fprintf(stderr, "myshell: dup2 failed for pipe output: %s\n",
                    strerror(errno));
            _exit(1);
        }

        close(pipe_descriptors[1]);

        if (handle_redirection(first) == -1) {
            _exit(1);
        }

        execvp(first->argv[0], first->argv);

        fprintf(stderr, "myshell: %s: %s\n",
                first->argv[0], strerror(errno));
        _exit(127);
    }

    second_child = fork();

    if (second_child == -1) {
        fprintf(stderr, "myshell: second fork failed: %s\n", strerror(errno));
        close(pipe_descriptors[0]);
        close(pipe_descriptors[1]);
        waitpid(first_child, NULL, 0);
        return -1;
    }

    if (second_child == 0) {
        close(pipe_descriptors[1]);

        if (dup2(pipe_descriptors[0], STDIN_FILENO) == -1) {
            fprintf(stderr, "myshell: dup2 failed for pipe input: %s\n",
                    strerror(errno));
            _exit(1);
        }

        close(pipe_descriptors[0]);

        if (handle_redirection(second) == -1) {
            _exit(1);
        }

        execvp(second->argv[0], second->argv);

        fprintf(stderr, "myshell: %s: %s\n",
                second->argv[0], strerror(errno));
        _exit(127);
    }

    close(pipe_descriptors[0]);
    close(pipe_descriptors[1]);

    while (waitpid(first_child, &first_status, 0) == -1) {
        if (errno != EINTR) {
            fprintf(stderr, "myshell: waitpid failed for first child: %s\n",
                    strerror(errno));
            break;
        }
    }

    while (waitpid(second_child, &second_status, 0) == -1) {
        if (errno != EINTR) {
            fprintf(stderr, "myshell: waitpid failed for second child: %s\n",
                    strerror(errno));
            break;
        }
    }

    return 0;
}

/*
 * Display the prompt with the shell's current working directory.
 * getcwd() is called every time so the prompt changes immediately after cd.
 */
static void print_prompt(void)
{
    char current_directory[MAX_LINE];

    if (getcwd(current_directory, sizeof(current_directory)) == NULL) {
        printf("myshell$ ");
    } else {
        printf("myshell:%s$ ", current_directory);
    }

    fflush(stdout);
}

/*
 * Main program for the shell.
 * It repeatedly prints a prompt, reads a line, parses it, handles built-ins,
 * and otherwise starts the required external command(s).
 */
int main(void)
{
    char line[MAX_LINE + 1];
    Command first_command;
    Command second_command;
    int has_pipe;
    int builtin_result;

    while (1) {
        print_prompt();

        if (!read_line(line, sizeof(line))) {
            printf("\n");
            break;
        }

        if (line[0] == '\0') {
            continue;
        }

        if (parse_line(line, &first_command, &second_command, &has_pipe) == -1) {
            continue;
        }

        if (first_command.argc == 0) {
            continue;
        }

        if (has_pipe) {
            if (strcmp(first_command.argv[0], "cd") == 0 ||
                strcmp(first_command.argv[0], "pwd") == 0 ||
                strcmp(first_command.argv[0], "echo") == 0 ||
                strcmp(first_command.argv[0], "exit") == 0) {
                fprintf(stderr, "myshell: built-in commands cannot be used in a pipe\n");
                continue;
            }

            handle_pipe(&first_command, &second_command);
            continue;
        }

        builtin_result = handle_builtin(&first_command);

        if (builtin_result == 2) {
            break;
        }

        if (builtin_result == 1) {
            continue;
        }

        execute_command(&first_command);
    }

    return 0;
}

