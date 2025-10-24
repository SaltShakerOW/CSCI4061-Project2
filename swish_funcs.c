#define _GNU_SOURCE

#include "swish_funcs.h"

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job_list.h"
#include "string_vector.h"

#define MAX_ARGS 10

int tokenize(char *s, strvec_t *tokens) {
    // TODO Task 0: Tokenize string s
    // Assume each token is separated by a single space (" ")
    // Use the strtok() function to accomplish this
    // Add each token to the 'tokens' parameter (a string vector)
    // Return 0 on success, -1 on error

    if (s == NULL || tokens == NULL) {
        fprintf(stderr, "tokenize: NULL input\n");
        return -1;
    }

    if (*s == '\0') {
        return 0;
    }

    char *token = strtok(s, " ");
    while (token != NULL) {
        if (strvec_add(tokens, token) == -1) {
            fprintf(stderr, "tokenize: failed to add token\n");
            return -1;
        }
        token = strtok(NULL, " ");
    }

    return 0;
}

int run_command(strvec_t *tokens) {
    // TODO Task 2: Execute the specified program (token 0) with the
    // specified command-line arguments
    // THIS FUNCTION SHOULD BE CALLED FROM A CHILD OF THE MAIN SHELL PROCESS
    // Hint: Build a string array from the 'tokens' vector and pass this into execvp()
    // Another Hint: You have a guarantee of the longest possible needed array, so you
    // won't have to use malloc.

    //sigaction setup and initalization
    struct sigaction sigact;
    sigact.sa_handler = SIG_DFL;
    if (sigfillset(&sigact.sa_mask) == -1) {
        perror("sigfillset");
        return -1;
    }
    sigact.sa_flags = 0;

    if (sigaction(SIGTTOU, &sigact, NULL) == -1 || sigaction(SIGTTIN, &sigact, NULL) == -1) { //restore SIGTTOU and SIGTTIN to child
        perror("sigaction");
        return -1;
    }

    pid_t pid = getpid(); //set groupid of process to process id
    if (setpgid(pid, pid) == -1) {
        perror("setpgid");
        return -1;
    }

    // handle redirection before building args array
    int input_fd = -1, output_fd = -1;
    int input_redirect_pos  = strvec_find(tokens, "<");
    int output_redirect_pos = strvec_find(tokens, ">");
    int append_redirect_pos = strvec_find(tokens, ">>");

    //input redirection (<)
    if (input_redirect_pos != -1) {
        if (input_redirect_pos + 1 >= tokens->length) {
            fprintf(stderr, "Failed to open input file\n");
            return -1;
        }
        const char *input_file = strvec_get(tokens, input_redirect_pos + 1);
        input_fd = open(input_file, O_RDONLY);
        if (input_fd == -1) {
            perror("Failed to open input file");
            return -1;
        }
        if (dup2(input_fd, STDIN_FILENO) == -1) {
            perror("dup2");
            close(input_fd);
            return -1;
        }
        close(input_fd);
    }

    // output redirection (>)
    if (output_redirect_pos != -1) {
        if (output_redirect_pos + 1 >= tokens->length) {
            fprintf(stderr, "Failed to open output file\n");
            return -1;
        }
        const char *output_file = strvec_get(tokens, output_redirect_pos + 1);
        output_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        if (output_fd == -1) {
            perror("Failed to open output file");
            return -1;
        }
        if (dup2(output_fd, STDOUT_FILENO) == -1) {
            perror("dup2");
            close(output_fd);
            return -1;
        }
        close(output_fd);
    }

    //append redirection (>>)
    if (append_redirect_pos != -1) {
        if (append_redirect_pos + 1 >= tokens->length) {
            fprintf(stderr, "Failed to open output file\n");
            return -1;
        }
        const char *output_file = strvec_get(tokens, append_redirect_pos + 1);
        output_fd = open(output_file, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
        if (output_fd == -1) {
            perror("Failed to open output file");
            return -1;
        }
        if (dup2(output_fd, STDOUT_FILENO) == -1) {
            perror("dup2");
            close(output_fd);
            return -1;
        }
        close(output_fd);
    }

    //build args array
    char *args[MAX_ARGS + 1];
    int arg_count = 0;

    for (int i = 0; i < tokens->length; i++) {
        const char *token = strvec_get(tokens, i);
        if (strcmp(token, "<") == 0 || strcmp(token, ">") == 0 || strcmp(token, ">>") == 0) {
            i++;
            continue;
        }
        if (arg_count >= MAX_ARGS) {
            fprintf(stderr, "Error: Too many arguments\n");
            return -1;
        }
        args[arg_count] = (char *)token;
        arg_count++;
    }

    args[arg_count] = NULL; // NULL sentinel

    execvp(args[0], args); // run the command

    perror("exec"); // error execvp didn't run
    return -1;

    // TODO Task 3: Extend this function to perform output redirection before exec()'ing
    // Check for '<' (redirect input), '>' (redirect output), '>>' (redirect and append output)
    // entries inside of 'tokens' (the strvec_find() function will do this for you)
    // Open the necessary file for reading (<), writing (>), or appending (>>)
    // Use dup2() to redirect stdin (<), stdout (> or >>)
    // DO NOT pass redirection operators and file names to exec()'d program
    // E.g., "ls -l > out.txt" should be exec()'d with strings "ls", "-l", NULL

    // TODO Task 4: You need to do two items of setup before exec()'ing
    // 1. Restore the signal handlers for SIGTTOU and SIGTTIN to their defaults.
    // The code in main() within swish.c sets these handlers to the SIG_IGN value.
    // Adapt this code to use sigaction() to set the handlers to the SIG_DFL value.
    // 2. Change the process group of this process (a child of the main shell).
    // Call getpid() to get its process ID then call setpgid() and use this process
    // ID as the value for the new process group ID

    return 0;
}

int resume_job(strvec_t *tokens, job_list_t *jobs, int is_foreground) {
    // TODO Task 5: Implement the ability to resume stopped jobs in the foreground
    // 1. Look up the relevant job information (in a job_t) from the jobs list
    //    using the index supplied by the user (in tokens index 1)
    //    Feel free to use sscanf() or atoi() to convert this string to an int
    // 2. Call tcsetpgrp(STDIN_FILENO, <job_pid>) where job_pid is the job's process ID
    // 3. Send the process the SIGCONT signal with the kill() system call
    // 4. Use the same waitpid() logic as in main -- don't forget WUNTRACED
    // 5. If the job has terminated (not stopped), remove it from the 'jobs' list
    // 6. Call tcsetpgrp(STDIN_FILENO, <shell_pid>). shell_pid is the *current*
    //    process's pid, since we call this function from the main shell process

    //NOTE TO SELF: system calls -> perror, regular stuff -> fprintf
    if (tokens->length < 2) { //check for enough tokens
        fprintf(stderr, "Job index out of bounds\n");
        return -1;
    }
    int index = atoi(strvec_get(tokens, 1)); //grab the job index from str vector

    job_t *job = job_list_get(jobs, index); // get the job information
    if (job == NULL) {
        fprintf(stderr, "Job index out of bounds\n");
        return -1;
    }

    if (is_foreground) { //resume task in the foreground

        if (tcsetpgrp(STDIN_FILENO, job->pid) == -1) { //set job to foreground
            perror("tcsetpgrp");
            return -1;
        }

        if (kill(job->pid, SIGCONT) == -1) { //send resume signal
            perror("kill");
            return -1;
        }

        int status;
        if (waitpid(job->pid, &status, WUNTRACED) == -1) { //wait for job to finish
            perror("waitpid");
            return -1;
        }

        if (WIFSTOPPED(status)) { //update job to stopped if it stopped again
            job->status = STOPPED;
        } else { //remove job from list if it's complete
            job_list_remove(jobs, index);
        }

        if (tcsetpgrp(STDIN_FILENO, getpid()) == -1) { //set shell back to foreground
            perror("tcsetpgrp");
            return -1;
        }

    } else { //resume task in the background
        if (kill(job->pid, SIGCONT) == -1) { //send resume signal
            perror("kill");
            return -1;
        }
        job->status = BACKGROUND;
    }


    // TODO Task 6: Implement the ability to resume stopped jobs in the background.
    // This really just means omitting some of the steps used to resume a job in the foreground:
    // 1. DO NOT call tcsetpgrp() to manipulate foreground/background terminal process group
    // 2. DO NOT call waitpid() to wait on the job
    // 3. Make sure to modify the 'status' field of the relevant job list entry to BACKGROUND
    //    (as it was STOPPED before this)

    return 0;
}

int await_background_job(strvec_t *tokens, job_list_t *jobs) {
    // TODO Task 6: Wait for all background jobs to stop or terminate
    // 1. Iterate through the jobs list, ignoring any stopped jobs
    // 2. For a background job, call waitpid() with WUNTRACED.
    // 3. If the job has stopped (check with WIFSTOPPED), change its
    //    status to STOPPED. If the job has terminated, do nothing until the
    //    next step (don't attempt to remove it while iterating through the list).
    // 4. Remove all background jobs (which have all just terminated) from jobs list.
    //    Use the job_list_remove_by_status() function.
    if (tokens->length < 2) {
        fprintf(stderr, "Job index out of bounds\n");
        return -1;
    }

    int index = atoi(strvec_get(tokens, 1));
    job_t *job = job_list_get(jobs, index);

    if (job == NULL) {
        fprintf(stderr, "Job index out of bounds\n");
        return -1;
    }

    if (job->status != BACKGROUND) {
        fprintf(stderr, "Job index is for stopped process not background process\n");
        return -1;
    }

    int status;
    if (waitpid(job->pid, &status, WUNTRACED) == -1) {
        perror("waitpid");
        return -1;
    }

    if (WIFSTOPPED(status)) {
        job->status = STOPPED;
    } else {
        job_list_remove(jobs, index);
    }

    return 0;
}

int await_all_background_jobs(job_list_t *jobs) {
    job_t *current = jobs->head;
    while (current != NULL) {
        if (current->status == BACKGROUND) {
            int status;
            if (waitpid(current->pid, &status, WUNTRACED) == -1) {
                perror("waitpid");
                return -1;
            }

            if (WIFSTOPPED(status)) {
                current->status = STOPPED;
            }
        }
        current = current->next;
    }

    // remove all terminated background jobs
    job_list_remove_by_status(jobs, BACKGROUND);

    return 0;
}
