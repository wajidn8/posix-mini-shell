# POSIX Mini Shell

A small Unix-style command-line shell written in C using POSIX system calls.

The purpose of this project is to understand how a shell works at the operating-system level instead of just using an existing shell like Bash.

## Features

* Read commands up to 1024 characters
* Multiple spaces between arguments are supported
* Run external commands using `fork()` and `execvp()`
* Wait for child processes using `waitpid()`
* Built-in commands:

  * `cd`
  * `pwd`
  * `echo`
  * `exit`
* Output redirection using `>`
* Input redirection using `<`
* One pipe using `|`
* Error handling for invalid commands
* Prompt shows the current working directory

## How it works

A shell mainly does three things:

1. Read a command from the user
2. Parse the command into a program name and arguments
3. Execute the command

The basic flow in this project is:

```text
User enters command
        |
        v
    read_line()
        |
        v
    parse_line()
        |
        v
  Built-in command?
     /        \
   yes         no
    |           |
    v           v
execute      fork()
directly        |
                v
          child process
                |
                v
          execvp(command)
                |
                v
          external program

Parent process
      |
      v
   waitpid()
```

The shell keeps repeating this process until the user enters `exit`.

## Process creation

For an external command, the shell uses `fork()`.

`fork()` creates a new process. After the call, there are two processes:

* The parent process is the shell.
* The child process is created to run the requested command.

The child then calls `execvp()`.

`execvp()` replaces the child process with the requested program.

For example, when the user enters:

```text
ls
```

the shell roughly does:

```text
shell
  |
  +-- fork()
        |
        +-- child -> execvp("ls", ...)
        |
        +-- parent -> waitpid()
```

The parent waits for the child to finish before displaying the next prompt.

## Why `cd` is a built-in

`cd` must be handled by the shell itself.

The current working directory belongs to a process. If `cd` were executed in a child process, only the child would change its directory. The shell (parent) would remain in the original directory.

Therefore, `cd` calls `chdir()` directly inside the shell process.

## Built-in commands

### `pwd`

Uses `getcwd()` to print the shell's current working directory.

```text
myshell:/home/user/project$ pwd
/home/user/project
```

### `cd`

Uses `chdir()` to change the shell's working directory.

```text
myshell:/home/user/project$ cd /tmp
myshell:/tmp$
```

### `echo`

Prints the arguments entered after `echo`.

```text
myshell:/tmp$ echo Hello World
Hello World
```

### `exit`

Terminates the shell cleanly.

## I/O redirection

Unix programs normally use standard file descriptors:

```text
0 -> standard input
1 -> standard output
2 -> standard error
```

This shell uses `open()` and `dup2()` to change where standard input or output goes.

### Output redirection

Example:

```text
echo Hello > output.txt
```

Instead of printing `Hello` to the terminal, the shell opens `output.txt` and connects the command's standard output to that file.

Conceptually:

```text
Before:

command
   |
   v
stdout (1)
   |
   v
terminal


After:

command
   |
   v
stdout (1)
   |
   v
output.txt
```

`dup2()` is used in the child process before `execvp()`.

The `>` operation creates the file if necessary and overwrites an existing file.

### Input redirection

Example:

```text
cat < output.txt
```

The file is opened and connected to standard input.

```text
output.txt
     |
     v
stdin (0)
     |
     v
    cat
```

Again, `dup2()` is used before the command is executed.

## Pipes

The shell supports one pipe.

Example:

```text
ls | grep txt
```

A pipe creates a communication channel between two processes.

The first command writes to the pipe:

```text
ls
 |
 | stdout
 v
+------+
| pipe |
+------+
   |
   | stdin
   v
grep txt
```

The shell uses:

```c
pipe()
```

to create the pipe, then uses `fork()` twice to create the two child processes.

`dup2()` connects:

* the first command's standard output to the pipe
* the second command's standard input to the pipe

The parent closes the unused pipe file descriptors and waits for both children.

Only one pipe is supported in this project.

## Error handling

If a command does not exist, `execvp()` fails.

For example:

```text
myshell:/home/user/project$ abcxyz
```

The shell prints an error message and continues running instead of terminating.

## Project structure

```text
posix-mini-shell/
|
├── shell.c
├── README.md
└── .gitignore
```

The implementation is kept in one C file so the complete flow can be followed easily.

## Build and run

This project uses GCC and POSIX system calls.

Compile:

```bash
gcc -Wall -Wextra -std=c11 shell.c -o myshell
```

Run:

```bash
./myshell
```

## Example commands

### Basic commands

```text
pwd
ls
echo Hello World
```

### Change directory

```text
cd /tmp
pwd
```

### Output redirection

```text
echo Hello from shell > test.txt
cat test.txt
```

### Input redirection

```text
cat < test.txt
```

### Pipe

```text
ls | grep txt
```

### Invalid command

```text
abcxyz
```

### Exit

```text
exit
```

## Limitations

This is intentionally a small shell and does not try to implement all features of Bash.

It does not support:

* Command history
* Tab completion
* Background processes using `&`
* Multiple pipes
* Quoted arguments
* Wildcard expansion such as `*.c`
* Environment variable expansion
* Shell scripting features
* Job control

The goal was to keep the implementation small enough to understand the process, file descriptor, redirection, and pipe operations clearly.

## What I learned

This project helped me understand how a shell interacts with the operating system, especially:

* Process creation with `fork()`
* Program execution with `execvp()`
* Process synchronization with `waitpid()`
* File descriptors
* `dup2()` and I/O redirection
* Inter-process communication using `pipe()`
* Changing the process working directory with `chdir()`

