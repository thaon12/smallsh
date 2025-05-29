// Citation for the parser function
// Date: 5/24/2025
// Copied from:
// Source url: https://canvas.oregonstate.edu/courses/1999732/assignments/9997827?module_item_id=25329384

/**
 * A sample program for parsing a command line. If you find it useful,
 * feel free to adapt this code for Assignment 4.
 * Do fix memory leaks and any additional issues you find.
 */

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>

#define INPUT_LENGTH 2048
#define MAX_ARGS	 512
#define MAX_BG_PROCESSES 100

// Global variable to track foreground-only mode
volatile sig_atomic_t fg_only_mode = 0;


struct command_line
{
	char *argv[MAX_ARGS + 1];
	int argc;
	char *input_file;
	char *output_file;
	bool is_bg;
};

struct command_line *parse_input()
{
	char input[INPUT_LENGTH];
	struct command_line *curr_command = (struct command_line *) calloc(1, sizeof(struct command_line));

	// Get input
	printf(": ");
	fflush(stdout);
	fgets(input, INPUT_LENGTH, stdin);

	// Tokenize the input
	char *token = strtok(input, " \n");
	while(token){
		if(!strcmp(token,"<")){
			curr_command->input_file = strdup(strtok(NULL," \n"));
		} else if(!strcmp(token,">")){
			curr_command->output_file = strdup(strtok(NULL," \n"));
		} else if(!strcmp(token,"&")){
			curr_command->is_bg = true;
		} else{
			curr_command->argv[curr_command->argc++] = strdup(token);
		}
		token=strtok(NULL," \n");
	}
	return curr_command;
}

void handle_sigtstp(int signo) {
    const char* on_message = "\nEntering foreground-only mode (& is now ignored)\n";
    const char* off_message = "\nExiting foreground-only mode\n";

    if (fg_only_mode == 0) {
        fg_only_mode = 1;
        write(STDOUT_FILENO, on_message, strlen(on_message));
    } else {
        fg_only_mode = 0;
        write(STDOUT_FILENO, off_message, strlen(off_message));
    }
}

void free_command(struct command_line* cmd) {
    for (int i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
    }
    if (cmd->input_file) free(cmd->input_file);
    if (cmd->output_file) free(cmd->output_file);
    free(cmd);
}



int main()
{
	struct command_line *curr_command;
	pid_t bg_pids[MAX_BG_PROCESSES]; // Array to store background process PIDs
	int bg_count = 0;  // Count of background processes

	// Set up signal handler for SIGTSTP
	struct sigaction sa_ignore = {0};
	sa_ignore.sa_handler = SIG_IGN;
	sigaction(SIGINT, &sa_ignore, NULL);

	// Set up signal handler for SIGTSTP to toggle foreground-only mode
	struct sigaction sa_tstp = {0};
	sa_tstp.sa_handler = handle_sigtstp;
	sa_tstp.sa_flags = SA_RESTART;  // Restart interrupted syscalls
	sigfillset(&sa_tstp.sa_mask);
	sigaction(SIGTSTP, &sa_tstp, NULL);


	while(true)
	{
		// Check for completed background processes before each prompt
		for (int i = 0; i < bg_count; i++) {
			int child_status;
			pid_t result = waitpid(bg_pids[i], &child_status, WNOHANG);
			if (result > 0) {
				// Process finished
				if (WIFEXITED(child_status)) {
					printf("background pid %d is done: exit value %d\n", bg_pids[i], WEXITSTATUS(child_status));
				} else if (WIFSIGNALED(child_status)) {
					printf("background pid %d is done: terminated by signal %d\n", bg_pids[i], WTERMSIG(child_status));
				}
				fflush(stdout);

				// Remove this pid from the list by shifting the array
				for (int j = i; j < bg_count - 1; j++) {
					bg_pids[j] = bg_pids[j + 1];
				}
				bg_count--;
				i--;  // Adjust index because we shifted
			}
		}

		// Parse the input command
		curr_command = parse_input();

		// Check if line is empty or a comment
		if (curr_command->argc == 0 || curr_command->argv[0][0] == '#') {
			free_command(curr_command);
			continue;
		}

		// Check if line is exit and break the loop if so
		if(!strcmp(curr_command->argv[0], "exit")) {
			free_command(curr_command);
			curr_command = NULL;
			for (int i = 0; i < bg_count; i++) {
				kill(bg_pids[i], SIGTERM);
			}

			break;
		}

		// Check if the command is cd
		if(!strcmp(curr_command->argv[0], "cd")) {
			if(curr_command->argc > 1) {
				if(chdir(curr_command->argv[1]) != 0) {
					perror("cd failed");
				}
			} else {
				// If no argument is given, change to home directory
				chdir(getenv("HOME"));
			}
			free_command(curr_command);
			continue;
		}

		// Check if command is status
		static int last_fg_status = 0;
		static bool last_fg_was_signal = false;
		// If the command is status, print the last foreground command's status
		if(!strcmp(curr_command->argv[0], "status")) {
			if (last_fg_was_signal) {
				printf("terminated by signal %d\n", last_fg_status);
			} else {
				printf("exit value %d\n", last_fg_status);
			}
			fflush(stdout);
			free_command(curr_command);
			continue;
		}

		// At this point we know the command is an external command
		// Fork a new process to execute the command
		pid_t pid = fork();
		if (pid < 0) {
			perror("fork failed");
			free_command(curr_command);
			continue;
		}
		// Child process
		if (pid == 0) {
			// Reset signals to default for the child process
			struct sigaction sa_int = {0};
			sa_int.sa_handler = SIG_DFL;
			sigaction(SIGINT, &sa_int, NULL);

			// Ignore SIGTSTP in the child process to prevent it from being affected by the parent’s foreground-only mode
			struct sigaction sa_ignore_tstp = {0};
			sa_ignore_tstp.sa_handler = SIG_IGN;
			sigaction(SIGTSTP, &sa_ignore_tstp, NULL);


			// Handle input redirection
			if (curr_command->input_file) {
				int inputFD = open(curr_command->input_file, O_RDONLY);
				if (inputFD == -1) {
					fprintf(stderr, "cannot open %s for input\n", curr_command->input_file);
					exit(1);
				}

				int result = dup2(inputFD, 0);
				if (result == -1) {
					perror("dup2"); 
					free_command(curr_command);
					exit(2); 
				}

				close(inputFD);
				
			}

			// Handle output redirection
			if (curr_command->output_file) {
				int outputFD = open(curr_command->output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
				if (outputFD == -1) {
					fprintf(stderr, "cannot open %s for output\n", curr_command->output_file);
					exit(1);
				}

				int result = dup2(outputFD, 1);
				if (result == -1) {
					perror("dup2"); 
					free_command(curr_command);
					exit(2); 
				}

				close(outputFD);
				
			}

			// Redirect background processes’ input/output to /dev/null if no redirection provided
			if (curr_command->is_bg && !curr_command->input_file) {
				int devNullFD = open("/dev/null", O_RDONLY);
				dup2(devNullFD, 0);
				close(devNullFD);
			}
			if (curr_command->is_bg && !curr_command->output_file) {
				int devNullFD = open("/dev/null", O_WRONLY);
				dup2(devNullFD, 1);
				close(devNullFD);
			}


			curr_command->argv[curr_command->argc] = NULL; // Null-terminate the argument list
			execvp(curr_command->argv[0], curr_command->argv);
			perror("execvp failed");
			free_command(curr_command);
			exit(EXIT_FAILURE);

		
		} 
		// Parent process
		else {
			if (fg_only_mode) {
				curr_command->is_bg = false;
			}

			// If the command is not background, wait for it to finish
			if (!curr_command->is_bg) {
				int status;
				pid_t wait_pid = waitpid(pid, &status, 0);
				if (wait_pid < 0) {
					perror("waitpid failed");
				} else {
					if (WIFEXITED(status)) {
						last_fg_status = WEXITSTATUS(status);
						last_fg_was_signal = false;
					} else if (WIFSIGNALED(status)) {
						last_fg_status = WTERMSIG(status);
						last_fg_was_signal = true;
					}
				}
			}
			// If the command is background, just print the PID
			else {
				printf("background pid is %d\n", pid);
				fflush(stdout);
				if (bg_count < MAX_BG_PROCESSES) {
					bg_pids[bg_count++] = pid;
				} else {
					fprintf(stderr, "Too many background processes!\n");
				}
			}
		}

		// Free the command structure and its components
		if (curr_command != NULL) {
			free_command(curr_command);
		}

	}

	// Free the last command if it exists
	if (curr_command != NULL) {
		free_command(curr_command);
	}


	return EXIT_SUCCESS;
}