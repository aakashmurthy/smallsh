#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define INPUTMAXLEN 2048
int foregroundOnly = 0;

// Stores requested command to be ran by user
struct Command {
    char *args[514];            // Command + 512 args + null terminator
    size_t n_args;
    char *input_file;
    char *output_file;
    size_t background;
};

// Parse input from user and create Command struct containing details of requested command
void parseInput(char* input, struct Command* cmd) {
    char* DELIMITER = " \n", *saveptr, *token;      // Setup delimiters for strtok
    if (input[0] == '#' || strlen(input) <= 1) {      // If input starts with # or is blank, return
        return;
    }
    // Replace instances of '$$' with pid of smallsh
    if (strstr(input, "$$")) {
        char *toksave;
        char tmpInput[INPUTMAXLEN];
        char *tok = strtok_r(input, "$", &toksave);
        int pid = getpid();

        // In cases where $$ exists, replace with pid
        strcpy(tmpInput, tok);
        sprintf(tmpInput + strlen(tmpInput), "%d", pid);
        tok = strtok_r(NULL, "$", &toksave);
        sprintf(tmpInput + strlen(tmpInput), tok);
        strcpy(input, tmpInput);
    }
    // First 'word' is always the target/command of the full command, save into command struct
    token = strtok_r(input, DELIMITER, &saveptr);
    cmd->args[cmd->n_args] = calloc(strlen(token)+1, sizeof(char));
    strcpy(cmd->args[cmd->n_args], token);
    cmd->n_args++;              // Increment args counter
    token = strtok_r(NULL, DELIMITER, &saveptr);

    // Iterate thru each 'word' in command
    while (token != NULL) {
        // If char is input redirect, add to struct
        if (!strcmp(token, "<")) {
            // Retrieve next token (must be input file) and add to struct
            token = strtok_r(NULL, DELIMITER, &saveptr);
            cmd->input_file = calloc(strlen(token)+1, sizeof(char));
            strcpy(cmd->input_file, token);
            // Advance to next token and continue
            token = strtok_r(NULL, DELIMITER, &saveptr);
            continue;
        }
        // If char is output redirect, add to struct, works same as input
        if (!strcmp(token, ">")) {
            token = strtok_r(NULL, DELIMITER, &saveptr);
            cmd->output_file = calloc(strlen(token)+1, sizeof(char));
            strcpy(cmd->output_file, token);
            token = strtok_r(NULL, DELIMITER, &saveptr);
            continue;
        }
        // Add to background processes
        cmd->args[cmd->n_args] = calloc(strlen(token)+1, sizeof(char));
        strcpy(cmd->args[cmd->n_args], token);
        cmd->n_args++;

        token = strtok_r(NULL, DELIMITER, &saveptr);
    }
    // If last argument is background special char, remove from args and set background flag
    if (cmd->n_args >= 1 && !strcmp(cmd->args[cmd->n_args-1], "&")) {
        free(cmd->args[cmd->n_args-1]);
        cmd->n_args--;
        cmd->background = 1;
    }
    cmd->args[cmd->n_args] = NULL;
    if (foregroundOnly) cmd->background = 0;            // If foreground only flag set, force background to false
}

// Handles redirects for foreground child processes that calls function
void handleRedirects(struct Command *cmd) {
    // If input file exists, open it
    if (cmd->input_file) {
        int fdin = open(cmd->input_file, O_RDONLY, 0);
        if (fdin == -1) {               // If error, exit (child will be exited)
            perror("Error opening input file...\n");
            exit(1);
        }
        int result = dup2(fdin, STDIN_FILENO);       // Redirect opened file to STDIN
        if (result == -1) {perror("Error changing pipes, exiting...\n"); exit(2);};
        close(fdin);
    } else if (cmd->background) {       // If input file does not exist and background is set...
        // Open /dev/null and redirect into STDIN
        int fdin = open("/dev/null", O_RDONLY);
        if (fdin == -1) {               // If error, exit (child will be exited)
            perror("Error opening input file...\n");
            exit(1);
        }
        int result = dup2(fdin, STDIN_FILENO);
        if (result == -1) {perror("Error changing pipes, exiting...\n"); exit(2);};
        close(fdin);
    }
    // Output file, works same as input file
    if (cmd->output_file) {
        int fdout = open(cmd->output_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fdout == -1) {
            perror("Error opening output file...\n");
            exit(1);
        }
        int result = dup2(fdout, STDOUT_FILENO);
        if (result == -1) {perror("Error changing pipes, exiting...\n"); exit(2);};
        close(fdout);
    } else if (cmd->background) {
        int fdout = open("/dev/null", O_WRONLY | O_TRUNC | O_CREAT);
        if (fdout == -1) {
            perror("Error opening output file...\n");
            exit(1);
        }
        int result = dup2(fdout, STDOUT_FILENO);
        if (result == -1) {perror("Error changing pipes, exiting...\n"); exit(2);};
        close(fdout);
    }
}

// Free memory in heap used by cmd
void freeCommand(struct Command* cmd) {
    for (int i = 0; i < cmd->n_args; i++) {
        if (cmd->args[i]) free(cmd->args[i]);
        cmd->args[i] = NULL;
    }
    if (cmd->input_file) free(cmd->input_file);
    cmd->input_file = NULL;
    if (cmd->output_file) free(cmd->output_file);
    cmd->output_file = NULL;
    if (cmd) free(cmd);
    cmd = NULL;
}

// Message for SIGINT handler, reports termination
void SIGINTmessage(int signo) {
    char* msg = "terminated by signal 2\n";
    write(STDOUT_FILENO, msg, 23);
}

// Loads message for SIGTSTP handler, flips foreground global flag
void SIGTSTPmessage(int signo) {
    if (foregroundOnly) {
        char* msg = "Exiting foreground-only mode\n";
        write(STDOUT_FILENO, msg, 29);
        fflush(stdout);
        foregroundOnly = 0;
    } else {
        char* msg = "Entering foreground-only mode (& is now ignored)\n";
        write(STDOUT_FILENO, msg, 49);
        fflush(stdout);
        foregroundOnly = 1;
    }
}

int main() {
    // Declare signal interceptors
    struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0}, SIGINT_default = {0};
    // Fill SIGINT struct and enable handling, backing up default settings into SIGINT_default
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_handler = SIGINTmessage;
    sigaction(SIGINT, &SIGINT_action, &SIGINT_default);

    // Fill SIGTSTP struct and enable handling
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_handler = SIGTSTPmessage;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // Declare tracking variables for shell
    int pidList[100] = {0};
    int pidtracker = 0;     
    int exitStatus = 0;      // Stores exit status of last ran command, positive if exit status, negative if signal
    while (1) {
        // Create command struct
        struct Command* cmd = calloc(1, sizeof(struct Command));
        cmd->n_args = 0;
        cmd->input_file = NULL;
        cmd->output_file = NULL;
        cmd->background = 0;

        // Iterate thru pid array and message when background pid done
        for (int i = 0; i < 100; i++) {
            if (pidList[i] <= 0) continue;                          // If element does not contain usable pid, skip iteration
            int exitMethod = -5;
            int tmp = waitpid(pidList[i], &exitMethod, WNOHANG);    // tmp is non-zero if process hasn't ended
            if (tmp != 0) {
                if (WIFEXITED(exitMethod)) {                        // Parse exit method, saving into exitStatus and reports status on background pid
                    printf("background pid %d is done: exit value %d\n", pidList[i], WEXITSTATUS(exitMethod));
                } else {
                    printf("background pid %d is done: terminated by signal %d\n", pidList[i], WTERMSIG(exitMethod));
                }
                fflush(stdout);
                pidList[i] = 0;
            }
        }

        // Print prompt and accept input from user
        printf(": ");
        fflush(stdout);
        char input[INPUTMAXLEN];
        fgets(input, INPUTMAXLEN, stdin); 
        parseInput(input, cmd);                             // Parse user input into Command struct

        if (cmd->n_args < 1) {
            freeCommand(cmd);
            continue;
        } else if (!strcmp(cmd->args[0], "exit")) {         // If exit is run, free command and exit
            freeCommand(cmd);
            exit(0);
        } else if (!strcmp(cmd->args[0], "cd")) {           // If cd is ran...
            if (cmd->n_args<=1) chdir(getenv("HOME"));      // If only 1 arg exists, (cd is ran on itself), change directory to home
            else chdir(cmd->args[1]);                       // Else, change directory to second argument (index 1)
        } else if (!strcmp(cmd->args[0], "status")) {       // If status called, return value of last exit status
            if (exitStatus >= 0) {
                printf("exit value %d\n", exitStatus);
                fflush(stdout);
            } else {
                printf("terminated by signal %d\n", abs(exitStatus));
                fflush(stdout);
            }
        } else {                                            // If not in-built command, run command
            pid_t forkpid = fork();                         // Create child process to run requested command
            int childExitMethod = -5;
            switch(forkpid) {
                case 0: // Child
                    if (!cmd->background) sigaction(SIGINT, &SIGINT_default, NULL);   // Restore default SIGINT handling if foreground process
                    handleRedirects(cmd);
                    execvp(cmd->args[0], cmd->args);
                    printf("bash: %s: %s\n", cmd->args[0], strerror(errno));        // If code gets here, execvp didn't work
                    fflush(stdout);
                    exit(1); break;                                                 // exit with error
                default: // Parent
                    if (!cmd->background) {             // If foreground, block until process finished
                        waitpid(forkpid, &childExitMethod, 0);
                        if (WIFEXITED(childExitMethod)) {
                            exitStatus = WEXITSTATUS(childExitMethod);      // save exit status as positive number to represent signal
                        } else {
                            exitStatus = 0;
                            exitStatus -= WTERMSIG(childExitMethod);        // flip exit status to negative to represent signal
                        }
                    } else {                            // If background, report pid and add to array
                        printf("background pid is %d\n", forkpid);
                        fflush(stdout);
                        pidList[pidtracker] = forkpid;
                        pidtracker++;
                        if (pidtracker >= 100) pidtracker = 0;  // Clamp PIDtracker to 0
                    }
                    break;
            }
        }
        freeCommand(cmd);
    }
    return 0;
}