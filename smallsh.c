/*
smallsh
Alex Li
4/27/21
*/

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>

// Global variables
int foregroundMode = 0;  // Tracks mode program is running in
int foregroundHelper = 0;  // Determines if a foreground child process is currently running (used to handle SIGTSTP)
int childStatus;  // Status of current foreground child process

// Expands the variable $$ in the input string
void expandVar(char *line, char *newLine)
{   
    // Specifies substring ($$) to expand
    char subStr[] = "$$";
    int subStrLen = strlen(subStr);
    
    // Retrieves PID and converts it to a string
    int replaceNum = getpid();
    char replaceStr[2049];
    sprintf(replaceStr, "%d", replaceNum);
    int replaceStrLen = strlen(replaceStr);
    
    // Buffer used to store new string with expanded variables
    char buffer[2049];
    char *insert_point = &buffer[0];
    const char *ptr = newLine;

    // Loops through input string
    while (1)
    {
        // Searches for next occurrence of substring
        const char *p = strstr(ptr, subStr);

        // If no more occurences of substring, copy remaining part of input to buffer
        if (p == NULL)
        {
            strcpy(insert_point, ptr);
            break;
        }

        // Else, copies part of input string before substring to buffer
        memcpy(insert_point, ptr, p - ptr);
        insert_point += p - ptr;

        // Copies PID to buffer
        memcpy(insert_point, replaceStr, replaceStrLen);
        insert_point += replaceStrLen;

        // Moves buffer pointer past the copied PID
        ptr = p + subStrLen;
    }

    // Copies and returns new string with expanded variable(s)
    strcpy(newLine, buffer);
};

// Structure for storing elements of a command
struct command 
{
    char *name;
    char *arguments[513];
    char *inputFile;
    char *outputFile;
    int mode;  // Whether command will run in foreground/background
    int argCount;
};

// Parses space-delimited string and returns new command structure
struct command *createCommand(char *line)
{
    // Allocates space for current command structure, initializes pointer to string
    struct command *currCommand = malloc(sizeof(struct command));
    char *saveptr;

    // Initializes members (or elements) of the new command structure
    currCommand->inputFile = NULL;
    currCommand->outputFile = NULL;
    currCommand->mode = 0;
    currCommand->argCount = 0;

    // Token for command name
    char *token = strtok_r(line, " ", &saveptr);
    token[strcspn(token, "\n")] = 0;
    currCommand->name = calloc(strlen(token) + 1, sizeof(char));
    strcpy(currCommand->name, token);

    // Token for optional command components
    token = strtok_r(NULL, " ", &saveptr);
    while (token != NULL)
    {
        // Token for input file
        if (strcmp(token, "<\n") == 0 || strcmp(token, "<\0") == 0)
        {
            token = strtok_r(NULL, " ", &saveptr);
            token[strcspn(token, "\n")] = 0;
            currCommand->inputFile = calloc(strlen(token) + 1, sizeof(char));
            strcpy(currCommand->inputFile, token);
        }
        // Token for output file
        else if (strcmp(token, ">\n") == 0 || strcmp(token, ">\0") == 0)
        {
            token = strtok_r(NULL, " ", &saveptr);
            token[strcspn(token, "\n")] = 0;
            currCommand->outputFile = calloc(strlen(token) + 1, sizeof(char));
            strcpy(currCommand->outputFile, token);
        }
        // Token for command mode (foreground vs background)
        else if (strcmp(token, "&\n") == 0 || strcmp(token, "&\0") == 0)
        {
            token = strtok_r(NULL, " ", &saveptr);
            if (token == NULL || strcmp(token, "\n") == 0)
            {
                // Only parses background commands if foreground-only mode is OFF
                if (foregroundMode == 0)
                {
                    currCommand->mode = 1;
                }
                break;
            }
        }
        // Token for command argument(s)
        else
        {
            if (strcmp(token, "\n") != 0)
            {
                token[strcspn(token, "\n")] = 0;
                currCommand->arguments[currCommand->argCount] = token;
                currCommand->argCount += 1;
            }
        }
        token = strtok_r(NULL, " ", &saveptr);
    }
    return currCommand;
};

// Signal handler for SIGINT
void handle_SIGINT(int signo)
{
    // Function is empty as parent process ignores SIGINT
};

// Signal handler for SIGTSTP
void handle_SIGTSTP(int signo)
{
    // If program is not currently in foreground-only mode
    if (foregroundMode == 0)
    {
        // If there is a foreground process currently running, wait for it to terminate first
        if (foregroundHelper != 0)
        {
            foregroundHelper = waitpid(foregroundHelper, &childStatus, 0);
            foregroundHelper = 0;
            char *message = "\nEntering foreground-only mode (& is now ignored)\n";
            write(STDOUT_FILENO, message, 50);
        }
        // Else, print informative message immediately
        else
        {
            char *message = "\nEntering foreground-only mode (& is now ignored)\n";
            write(STDOUT_FILENO, message, 50);
            char *message2 = ": ";
            write(STDOUT_FILENO, message2, 2);
        }
        // Program switches to foreground-only mode
        foregroundMode = 1;
    }

    // If program is currently in foreground-only mode
    else
    {
        // If there is a foreground process currently running, wait for it to terminate first
        if (foregroundHelper != 0)
        {
            foregroundHelper = waitpid(foregroundHelper, &childStatus, 0);
            foregroundHelper = 0;
            char *message = "\nExiting foreground-only mode\n";
            write(STDOUT_FILENO, message, 30);
        }
        // Else, print informative message immediately
        else
        {
            char *message = "\nExiting foreground-only mode\n";
            write(STDOUT_FILENO, message, 30);
            char *message2 = ": ";
            write(STDOUT_FILENO, message2, 2);
        }
        // Program switches to normal running mode
        foregroundMode = 0;
    }
};

// Contains logic for smallsh
void main()
{
    pid_t spawnpid = -5;  // Initializes spawnpid to arbitrary number for comparison later
    int backgroundStatus;  // Stores the status of background child processes (not used in this program)
    int backgroundPids[2049] = {0};  // Array containing the PIDs of background child processes
    int array_size = sizeof(backgroundPids) / sizeof(int);
    int x = 0;  // Stores number of PIDs in backgroundPids
    int statusTracker = 0;  // Ensures status command works if no foreground command has ran yet

    // Initialize a new, empty sigaction struct
    struct sigaction SIGINT_action = {0};
    // Register custom signal handler function
    SIGINT_action.sa_handler = handle_SIGINT;
    // Block all catchable signals while handle_SIGINT is running
    sigfillset(&SIGINT_action.sa_mask);
    // Allows for automatic restarts of interrupted system calls after signal handler done
    SIGINT_action.sa_flags = SA_RESTART;
    // Install signal handler for SIGINT (CTRL-C)
    sigaction(SIGINT, &SIGINT_action, NULL);

    // Initialize a new, empty sigaction struct
    struct sigaction SIGTSTP_action = {0};
    // Register custom signal handler function
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    // Blocks all catchable signals while handle_SIGTSTP is running
    sigfillset(&SIGTSTP_action.sa_mask);
    // Allows for automatic restarts of interrupted system calls after signal handler done
    SIGTSTP_action.sa_flags = SA_RESTART;
    // Install signal handler for SIGTSTP (CTRL-Z)
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // Continuously displays smallsh prompt and waits for command
    while (1)
    {
        // Checks and cleans up any non-completed background processes, displays update message
        if (x != 0) {
            int y;
            int z = 0;
            // Loops through array of background PIDs, checking for terminated processes
            for (y = 0; y < array_size; y++)
            {
                if (backgroundPids[y] != 0)
                {
                    pid_t test;
                    test = waitpid(backgroundPids[y], &backgroundStatus, WNOHANG);
                    if (test != 0)
                    {
                        if (WIFEXITED(backgroundStatus))
                        {
                            printf("background pid %d is done: exit value %d\n", test, WEXITSTATUS(backgroundStatus));
                            fflush(stdout);
                        }
                        else
                        {
                            printf("background pid %d is done: terminated by signal %d\n", test, WTERMSIG(backgroundStatus));
                            fflush(stdout);
                        }
                        backgroundPids[y] = 0;
                        z -= 1;
                    }
                }
            }
            x += z;
        }

        // Prints colon symbol for each command line
        printf(": ");
        fflush(stdout);

        // Parses user input, ignoring blank lines or comments
        char *line = NULL;
        size_t len = 0;
        ssize_t nread;
        nread = getline(&line, &len, stdin);
        if (strlen(line) == 1 || strncmp(line, "#", 1) == 0)
        {
            continue;
        }

        // Creates new array used to store input with expanded variables
        char newLine[2048];
        strcpy(newLine, line);
        expandVar(line, newLine);

        // Creates new command structure
        struct command *newCommand = createCommand(newLine);

        // Built-in exit command
        if (strcmp(newCommand->name, "exit") == 0)
        {
            // Kills all background child processes
            int a;
            for (a = 0; a < array_size; a++)
            {
                if (backgroundPids[a] != 0)
                {
                    kill(backgroundPids[a], SIGKILL);
                }
            }
            // smallsh terminates itself
            exit(0);
        }

        // Built-in cd command
        if (strcmp(newCommand->name, "cd") == 0)
        {
            char *homeDir;

            // If no arguments, changes directory to path in HOME environment variable
            if (newCommand->argCount == 0 || 
            (strcmp(newCommand->arguments[0], "~") == 0) ||
            (strcmp(newCommand->arguments[0], "$HOME") == 0))
            {
                homeDir = getenv("HOME");
                chdir(homeDir);
            }
            // Else, changes directory to custom path specified by the first argument
            else
            {
                homeDir = newCommand->arguments[0];
                chdir(homeDir);
            }
            continue;
        }

        // Built-in status command
        if (strcmp(newCommand->name, "status") == 0)
        {
            // Returns exit status of last foreground process ran by smallsh
            if (WIFEXITED(childStatus))
            {
                printf("exit value %d\n", WEXITSTATUS(childStatus));
                fflush(stdout);
                continue;
            }
            // Returns terminating signal of last foreground process ran by smallsh
            else
            {
                // Returns exit value of 0 if no foreground command has been run yet
                if (statusTracker == 0)
                {
                    printf("exit value 0\n");
                    fflush(stdout);
                    continue;
                }
                printf("terminated by signal %d\n", WTERMSIG(childStatus));
                fflush(stdout);
                continue;
            }
        }

        // "Command-only" commands
        if (newCommand->argCount == 0 && 
        newCommand->inputFile == NULL && 
        newCommand->outputFile == NULL)
        {
            // Restructures command into exec() argument
            char cmd[2049] = "/bin/";
            strcat(cmd, newCommand->name);
            char *newcmd[] = {cmd, NULL};

            // Fork a new process
            pid_t spawnpid = fork();
            switch(spawnpid)
            {
                // Display error message if unable to fork new process
                case -1:
                    perror("fork()\n");
                    exit(2);
                    break;
                // Child process replaces current program with command passed in exec()
                case 0:
                    // Background child processes ignore SIGINT
                    if (newCommand->mode != 0)
                    {
                        signal(SIGINT, SIG_IGN);
                    }
                    // All child processes ignore SIGTSTP
                    signal(SIGTSTP, SIG_IGN);

                    execv(newcmd[0], newcmd);
                    // exec() returns if there is an error
                    perror(newCommand->name);
                    exit(1);
                    break;
                // Parent process checks if child process is running in the foreground or background
                default:
                    // Parent process waits for foreground child process's termination, then continues loop
                    if (newCommand->mode == 0)
                    {
                        foregroundHelper = spawnpid;
                        spawnpid = waitpid(spawnpid, &childStatus, 0);
                        // Prints message if foreground child process is terminated by SIGINT
                        if (WIFSIGNALED(childStatus))
                        {
                            printf("terminated by signal %d\n", WTERMSIG(childStatus));
                            fflush(stdout);
                        }
                        if (statusTracker == 0)
                        {
                            statusTracker = 1;
                        }
                        foregroundHelper = 0;
                        continue;
                    }
                    // Parent process does not wait for background child process's termination, immediately continues loop
                    else
                    {
                        printf("background pid is %d\n", spawnpid);
                        fflush(stdout);
                        // Child's PID is added to array of background PIDs
                        int d;
                        for (d = 0; d < array_size; d++)
                        {
                            if (backgroundPids[d] == 0)
                            {
                                backgroundPids[d] = spawnpid;
                                break;
                            }
                        }
                        x += 1;
                        spawnpid = waitpid(spawnpid, &backgroundStatus, WNOHANG);
                        continue;
                    }
            }
        }

        // Commands with arguments only
        if (newCommand->argCount != 0 && 
        newCommand->inputFile == NULL && 
        newCommand->outputFile == NULL)
        {
            // Restructures command into exec() argument
            char cmd[2049] = "/bin/";
            strcat(cmd, newCommand->name);
            char *newcmd[515] = {NULL};
            int i;
            int j = newCommand->argCount;
            newcmd[0] = cmd;
            for (i = 0; i < j; i++)
            {
                newcmd[i + 1] = newCommand->arguments[i];
            }
            
            // Fork a new process
            pid_t spawnpid = fork();
            switch(spawnpid)
            {
                // Display error message if unable to fork new process
                case -1:
                    perror("fork()\n");
                    exit(2);
                    break;
                // Child process replaces current program with command passed in exec()
                case 0:
                    // Background child processes ignore SIGINT
                    if (newCommand->mode != 0)
                    {
                        signal(SIGINT, SIG_IGN);
                    }
                    // All child processes ignore SIGTSTP
                    signal(SIGTSTP, SIG_IGN);
                    
                    execv(newcmd[0], newcmd);
                    // exec() returns if there is an error
                    perror(newCommand->name);
                    exit(1);
                    break;
                // Parent process checks if child process is running in the foreground or background
                default:
                    // Parent process waits for foreground child process's termination, then continues loop
                    if (newCommand->mode == 0)
                    {
                        foregroundHelper = spawnpid;
                        spawnpid = waitpid(spawnpid, &childStatus, 0);
                        // Prints message if foreground child process is terminated by SIGINT
                        if (WIFSIGNALED(childStatus))
                        {
                            printf("terminated by signal %d\n", WTERMSIG(childStatus));
                            fflush(stdout);
                        }
                        if (statusTracker == 0)
                        {
                            statusTracker = 1;
                        }
                        foregroundHelper = 0;
                        continue;
                    }
                    // Parent process does not wait for background child process's termination, immediately continues loop
                    else
                    {
                        printf("background pid is %d\n", spawnpid);
                        fflush(stdout);
                        // Child's PID is added to array of background PIDs
                        int d;
                        for (d = 0; d < array_size; d++)
                        {
                            if (backgroundPids[d] == 0)
                            {
                                backgroundPids[d] = spawnpid;
                                break;
                            }
                        }
                        x += 1;
                        spawnpid = waitpid(spawnpid, &backgroundStatus, WNOHANG);
                        continue;
                    }
            }
        }

        // Commands with redirection only
        if (newCommand->argCount == 0 && 
        (newCommand->inputFile != NULL || 
        newCommand->outputFile != NULL))
        {
            // Restructures command into exec() argument
            char cmd[2049] = "/bin/";
            strcat(cmd, newCommand->name);
            char *newcmd[] = {cmd, NULL};

            // Commands with output file only
            if (newCommand->inputFile == NULL && newCommand->outputFile != NULL)
            {
                // Creates path of output file
                int file_descriptor;
                char outputFilePath[2049] = "./";
                strcat(outputFilePath, newCommand->outputFile);

                // Fork a new process
                pid_t spawnpid = fork();
                switch(spawnpid)
                {
                    // Display error message if unable to fork new process
                    case -1:
                        perror("fork()\n");
                        exit(2);
                        break;
                    // Child process replaces current program with command passed in exec()
                    case 0:
                        // Background child processes ignore SIGINT
                        if (newCommand->mode != 0)
                        {
                            signal(SIGINT, SIG_IGN);
                        }
                        // All child processes ignore SIGTSTP
                        signal(SIGTSTP, SIG_IGN);

                        // Attempts to open output file, prints message if fails
                        file_descriptor = open(outputFilePath, O_WRONLY | O_CREAT | O_TRUNC, 0777);
                        if (file_descriptor == -1)
                        {
                            printf("cannot open %s for output\n", newCommand->outputFile);
                            fflush(stdout);
                            exit(1);
                            break;
                        } 
                        
                        // Redirects stdout to output file
                        dup2(file_descriptor, 1);

                        // Redirects stdin to /dev/null if running in background
                        if (newCommand->mode != 0)
                        {
                            int devNull = open("/dev/null", O_WRONLY);
                            dup2(devNull, 0);
                        }

                        execv(newcmd[0], newcmd);
                        // exec() returns if there is an error
                        perror(newCommand->name);
                        exit(1);
                        break;
                    // Parent process checks if child process is running in the foreground or background
                    default:
                        // Parent process waits for foreground child process's termination, then continues loop
                        if (newCommand->mode == 0)
                        {
                            foregroundHelper = spawnpid;
                            spawnpid = waitpid(spawnpid, &childStatus, 0);
                            // Prints message if foreground child process is terminated by SIGINT
                            if (WIFSIGNALED(childStatus))
                            {
                                printf("terminated by signal %d\n", WTERMSIG(childStatus));
                                fflush(stdout);
                            }
                            if (statusTracker == 0)
                            {
                                statusTracker = 1;
                            }
                            foregroundHelper = 0;
                            continue;
                        }
                        // Parent process does not wait for background child process's termination, immediately continues loop
                        else
                        {
                            printf("background pid is %d\n", spawnpid);
                            fflush(stdout);
                            // Child's PID is added to array of background PIDs
                            int d;
                            for (d = 0; d < array_size; d++)
                            {
                                if (backgroundPids[d] == 0)
                                {
                                    backgroundPids[d] = spawnpid;
                                    break;
                                }
                            }
                            x += 1;
                            spawnpid = waitpid(spawnpid, &backgroundStatus, WNOHANG);
                            continue;
                        }
                }
            }

            // Commands with input file only
            if (newCommand->inputFile != NULL && newCommand->outputFile == NULL)
            {
                // Creates path of input file
                int file_descriptor;
                char inputFilePath[2049] = "./";
                strcat(inputFilePath, newCommand->inputFile);

                // Fork a new process
                pid_t spawnpid = fork();
                switch(spawnpid)
                {
                    // Display error message if unable to fork new process
                    case -1:
                        perror("fork()\n");
                        exit(2);
                        break;
                    // Child process replaces current program with command passed in exec()
                    case 0:
                        // Background child processes ignore SIGINT
                        if (newCommand->mode != 0)
                        {
                            signal(SIGINT, SIG_IGN);
                        }
                        // All child processes ignore SIGTSTP
                        signal(SIGTSTP, SIG_IGN);

                        // Attempts to open input file, prints message if fails
                        file_descriptor = open(inputFilePath, O_RDONLY, 0777);
                        if (file_descriptor == -1)
                        {
                            printf("cannot open %s for input\n", newCommand->inputFile);
                            fflush(stdout);
                            exit(1);
                            break;
                        } 
                        
                        // Redirects stdin to input file
                        dup2(file_descriptor, 0);

                        // Redirects stdout to /dev/null if running in background
                        if (newCommand->mode != 0)
                        {
                            int devNull = open("/dev/null", O_WRONLY);
                            dup2(devNull, 1);
                        }

                        execv(newcmd[0], newcmd);
                        // exec() returns if there is an error
                        perror(newCommand->name);
                        exit(1);
                        break;
                    // Parent process checks if child process is running in the foreground or background
                    default:
                        // Parent process waits for foreground child process's termination, then continues loop
                        if (newCommand->mode == 0)
                        {
                            foregroundHelper = spawnpid;
                            spawnpid = waitpid(spawnpid, &childStatus, 0);
                            // Prints message if foreground child process is terminated by SIGINT
                            if (WIFSIGNALED(childStatus))
                            {
                                printf("terminated by signal %d\n", WTERMSIG(childStatus));
                                fflush(stdout);
                            }
                            if (statusTracker == 0)
                            {
                                statusTracker = 1;
                            }
                            foregroundHelper = 0;
                            continue;
                        }
                        // Parent process does not wait for background child process's termination, immediately continues loop
                        else
                        {
                            printf("background pid is %d\n", spawnpid);
                            fflush(stdout);
                            // Child's PID is added to array of background PIDs
                            int d;
                            for (d = 0; d < array_size; d++)
                            {
                                if (backgroundPids[d] == 0)
                                {
                                    backgroundPids[d] = spawnpid;
                                    break;
                                }
                            }
                            x += 1;
                            spawnpid = waitpid(spawnpid, &backgroundStatus, WNOHANG);
                            continue;
                        }
                }
            }

            // Commands with both input and output
            else
            {
                // Creates path of input file
                int input_descriptor;
                char inputFilePath[2049] = "./";
                strcat(inputFilePath, newCommand->inputFile);

                // Creates path of output file
                int output_descriptor;
                char outputFilePath[2049] = "./";
                strcat(outputFilePath, newCommand->outputFile);

                // Fork a new process
                pid_t spawnpid = fork();
                switch(spawnpid)
                {
                    // Display error message if unable to fork new process
                    case -1:
                        perror("fork()\n");
                        exit(2);
                        break;
                    // Child process replaces current program with command passed in exec()
                    case 0:
                        // Background child processes ignore SIGINT
                        if (newCommand->mode != 0)
                        {
                            signal(SIGINT, SIG_IGN);
                        }
                        // All child processes ignore SIGTSTP
                        signal(SIGTSTP, SIG_IGN);

                        // Attempts to open input and output file, prints message if fails
                        input_descriptor = open(inputFilePath, O_RDONLY, 0777);
                        if (input_descriptor == -1)
                        {
                            printf("cannot open %s for input\n", newCommand->inputFile);
                            fflush(stdout);
                            exit(1);
                            break;
                        } 
                        output_descriptor = open(outputFilePath, O_WRONLY | O_CREAT | O_TRUNC, 0777);
                        if (output_descriptor == -1)
                        {
                            printf("cannot open %s for output\n", newCommand->outputFile);
                            fflush(stdout);
                            exit(1);
                            break;
                        } 
                        
                        // Redirects stdin to input file
                        dup2(input_descriptor, 0);

                        // Redirects stdout to output file
                        dup2(output_descriptor, 1);

                        execv(newcmd[0], newcmd);
                        // exec() returns if there is an error
                        perror(newCommand->name);
                        exit(1);
                        break;
                    // Parent process checks if child process is running in the foreground or background
                    default:
                        // Parent process waits for foreground child process's termination, then continues loop
                        if (newCommand->mode == 0)
                        {
                            foregroundHelper = spawnpid;
                            spawnpid = waitpid(spawnpid, &childStatus, 0);
                            // Prints message if foreground child process is terminated by SIGINT
                            if (WIFSIGNALED(childStatus))
                            {
                                printf("terminated by signal %d\n", WTERMSIG(childStatus));
                                fflush(stdout);
                            }
                            if (statusTracker == 0)
                            {
                                statusTracker = 1;
                            }
                            foregroundHelper = 0;
                            continue;
                        }
                        // Parent process does not wait for background child process's termination, immediately continues loop
                        else
                        {
                            printf("background pid is %d\n", spawnpid);
                            fflush(stdout);
                            // Child's PID is added to array of background PIDs
                            int d;
                            for (d = 0; d < array_size; d++)
                            {
                                if (backgroundPids[d] == 0)
                                {
                                    backgroundPids[d] = spawnpid;
                                    break;
                                }
                            }
                            x += 1;
                            spawnpid = waitpid(spawnpid, &backgroundStatus, WNOHANG);
                            continue;
                        }
                }
            }
        }

        // Commands with arguments and redirection
        if (newCommand->argCount != 0 && 
        newCommand->inputFile != NULL || 
        newCommand->outputFile != NULL)
        {
            // Restructures command into exec() argument
            char cmd[2049] = "/bin/";
            strcat(cmd, newCommand->name);
            char *newcmd[515] = {NULL};
            int i;
            int j = newCommand->argCount;
            newcmd[0] = cmd;
            for (i = 0; i < j; i++)
            {
                newcmd[i + 1] = newCommand->arguments[i];
            }

            // Commands with output file only
            if (newCommand->inputFile == NULL && newCommand->outputFile != NULL)
            {
                // Creates path of output file
                int file_descriptor;
                char outputFilePath[2049] = "./";
                strcat(outputFilePath, newCommand->outputFile);

                // Fork a new process
                pid_t spawnpid = fork();
                switch(spawnpid)
                {
                    // Display error message if unable to fork new process
                    case -1:
                        perror("fork()\n");
                        exit(2);
                        break;
                    // Child process replaces current program with command passed in exec()
                    case 0:
                        // Background child processes ignore SIGINT
                        if (newCommand->mode != 0)
                        {
                            signal(SIGINT, SIG_IGN);
                        }
                        // All child processes ignore SIGTSTP
                        signal(SIGTSTP, SIG_IGN);

                        // Attempts to open output file, prints message if fails
                        file_descriptor = open(outputFilePath, O_WRONLY | O_CREAT | O_TRUNC, 0777);
                        if (file_descriptor == -1)
                        {
                            printf("cannot open %s for output\n", newCommand->outputFile);
                            fflush(stdout);
                            exit(1);
                            break;
                        } 
                        
                        // Redirects stdout to output file
                        dup2(file_descriptor, 1);

                        // Redirects stdin to /dev/null if running in background
                        if (newCommand->mode != 0)
                        {
                            int devNull = open("/dev/null", O_WRONLY);
                            dup2(devNull, 0);
                        }

                        execv(newcmd[0], newcmd);
                        // exec() returns if there is an error
                        perror(newCommand->name);
                        exit(1);
                        break;
                    // Parent process checks if child process is running in the foreground or background
                    default:
                        // Parent process waits for foreground child process's termination, then continues loop
                        if (newCommand->mode == 0)
                        {
                            foregroundHelper = spawnpid;
                            spawnpid = waitpid(spawnpid, &childStatus, 0);
                            // Prints message if foreground child process is terminated by SIGINT
                            if (WIFSIGNALED(childStatus))
                            {
                                printf("terminated by signal %d\n", WTERMSIG(childStatus));
                                fflush(stdout);
                            }
                            if (statusTracker == 0)
                            {
                                statusTracker = 1;
                            }
                            foregroundHelper = 0;
                            continue;
                        }
                        // Parent process does not wait for background child process's termination, immediately continues loop
                        else
                        {
                            printf("background pid is %d\n", spawnpid);
                            fflush(stdout);
                            // Child's PID is added to array of background PIDs
                            int d;
                            for (d = 0; d < array_size; d++)
                            {
                                if (backgroundPids[d] == 0)
                                {
                                    backgroundPids[d] = spawnpid;
                                    break;
                                }
                            }
                            x += 1;
                            spawnpid = waitpid(spawnpid, &backgroundStatus, WNOHANG);
                            continue;
                        }
                }
            }

            // Commands with input file only
            if (newCommand->inputFile != NULL && newCommand->outputFile == NULL)
            {
                // Creates path of input file
                int file_descriptor;
                char inputFilePath[2049] = "./";
                strcat(inputFilePath, newCommand->inputFile);

                // Fork a new process
                pid_t spawnpid = fork();
                switch(spawnpid)
                {
                    // Display error message if unable to fork new process
                    case -1:
                        perror("fork()\n");
                        exit(2);
                        break;
                    // Child process replaces current program with command passed in exec()
                    case 0:
                        // Background child processes ignore SIGINT
                        if (newCommand->mode != 0)
                        {
                            signal(SIGINT, SIG_IGN);
                        }
                        // All child processes ignore SIGTSTP
                        signal(SIGTSTP, SIG_IGN);

                        // Attempts to open input file, prints message if fails
                        file_descriptor = open(inputFilePath, O_RDONLY, 0777);
                        if (file_descriptor == -1)
                        {
                            printf("cannot open %s for input\n", newCommand->inputFile);
                            fflush(stdout);
                            exit(1);
                            break;
                        } 
                        
                        // Redirects stdin to input file
                        dup2(file_descriptor, 0);

                        // Redirects stdout to /dev/null if running in background
                        if (newCommand->mode != 0)
                        {
                            int devNull = open("/dev/null", O_WRONLY);
                            dup2(devNull, 1);
                        }

                        execv(newcmd[0], newcmd);
                        // exec() returns if there is an error
                        perror(newCommand->name);
                        exit(1);
                        break;
                    // Parent process checks if child process is running in the foreground or background
                    default:
                        // Parent process waits for foreground child process's termination, then continues loop
                        if (newCommand->mode == 0)
                        {
                            foregroundHelper = spawnpid;
                            spawnpid = waitpid(spawnpid, &childStatus, 0);
                            // Prints message if foreground child process is terminated by SIGINT
                            if (WIFSIGNALED(childStatus))
                            {
                                printf("terminated by signal %d\n", WTERMSIG(childStatus));
                                fflush(stdout);
                            }
                            if (statusTracker == 0)
                            {
                                statusTracker = 1;
                            }
                            foregroundHelper = 0;
                            continue;
                        }
                        // Parent process does not wait for background child process's termination, immediately continues loop
                        else
                        {
                            printf("background pid is %d\n", spawnpid);
                            fflush(stdout);
                            // Child's PID is added to array of background PIDs
                            int d;
                            for (d = 0; d < array_size; d++)
                            {
                                if (backgroundPids[d] == 0)
                                {
                                    backgroundPids[d] = spawnpid;
                                    break;
                                }
                            }
                            x += 1;
                            spawnpid = waitpid(spawnpid, &backgroundStatus, WNOHANG);
                            continue;
                        }
                }
            }

            // Commands with both input and output
            else
            {
                // Creates path of input file
                int input_descriptor;
                char inputFilePath[2049] = "./";
                strcat(inputFilePath, newCommand->inputFile);

                // Creates path of output file
                int output_descriptor;
                char outputFilePath[2049] = "./";
                strcat(outputFilePath, newCommand->outputFile);

                // Fork a new process
                pid_t spawnpid = fork();
                switch(spawnpid)
                {
                    // Display error message if unable to fork new process
                    case -1:
                        perror("fork()\n");
                        exit(2);
                        break;
                    // Child process replaces current program with command passed in exec()
                    case 0:
                        // Background child processes ignore SIGINT
                        if (newCommand->mode != 0)
                        {
                            signal(SIGINT, SIG_IGN);
                        }
                        // All child processes ignore SIGTSTP
                        signal(SIGTSTP, SIG_IGN);

                        // Attempts to open input and output file, prints message if fails
                        input_descriptor = open(inputFilePath, O_RDONLY, 0777);
                        if (input_descriptor == -1)
                        {
                            printf("cannot open %s for input\n", newCommand->inputFile);
                            fflush(stdout);
                            exit(1);
                            break;
                        } 
                        output_descriptor = open(outputFilePath, O_WRONLY | O_CREAT | O_TRUNC, 0777);
                        if (output_descriptor == -1)
                        {
                            printf("cannot open %s for output\n", newCommand->outputFile);
                            fflush(stdout);
                            exit(1);
                            break;
                        } 
                        
                        // Redirects stdin to input file
                        dup2(input_descriptor, 0);

                        // Redirects stdout to output file
                        dup2(output_descriptor, 1);

                        execv(newcmd[0], newcmd);
                        // exec() returns if there is an error
                        perror(newCommand->name);
                        exit(1);
                        break;
                    // Parent process checks if child process is running in the foreground or background
                    default:
                        // Parent process waits for foreground child process's termination, then continues loop
                        if (newCommand->mode == 0)
                        {
                            foregroundHelper = spawnpid;
                            spawnpid = waitpid(spawnpid, &childStatus, 0);
                            // Prints message if foreground child process is terminated by SIGINT
                            if (WIFSIGNALED(childStatus))
                            {
                                printf("terminated by signal %d\n", WTERMSIG(childStatus));
                                fflush(stdout);
                            }
                            if (statusTracker == 0)
                            {
                                statusTracker = 1;
                            }
                            foregroundHelper = 0;
                            continue;
                        }
                        // Parent process does not wait for background child process's termination, immediately continues loop
                        else
                        {
                            printf("background pid is %d\n", spawnpid);
                            fflush(stdout);
                            // Child's PID is added to array of background PIDs
                            int d;
                            for (d = 0; d < array_size; d++)
                            {
                                if (backgroundPids[d] == 0)
                                {
                                    backgroundPids[d] = spawnpid;
                                    break;
                                }
                            }
                            x += 1;
                            spawnpid = waitpid(spawnpid, &backgroundStatus, WNOHANG);
                            continue;
                        }
                }
            }
        }
    }
};