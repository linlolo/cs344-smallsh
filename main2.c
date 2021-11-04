# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <stdbool.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <signal.h>
# include <unistd.h>

// declare global variable for foreground only
bool foreground_only = false;

struct pidNode {
	pid_t pid;
	struct pidNode *next;
};

// adds a pidNode at the beginning of the linked list
void addNode(struct pidNode* head, pid_t pid) {
	struct pidNode *newNode = (struct pidNode*)malloc(sizeof(struct pidNode));

	newNode->pid = pid;
	newNode->next = head;
	head = newNode;
}

void handle_SIGTSTP(int signo) {
	if (foreground_only) {
		char* message = "\nExiting foreground-only mode\n";
		write(STDOUT_FILENO, message, strlen(message) + 1);
		fflush(stdout);
		foreground_only = false;
	}
	else {
		char* message = "\nEntering foreground-only mode (& is now ignored)\n";
		write(STDOUT_FILENO, message, strlen(message) + 1);
		fflush(stdout);
		foreground_only = true;
	}
}

char* pid_to_string(pid_t num) {
	char* temp_string = malloc(sizeof(pid_t) + 1);
	sprintf(temp_string, "%d", num);
	return temp_string;
}

// function to prompt user for input
void getInput(char* input) {
	printf(": ");
	fflush(stdout);
	fgets(input, 2048, stdin);
}

// replaces "$$" found in string argument with pid of process
// pid has already been converted to a string
char* string_expansion(char* str1, char* pid) {
	int ind1 = 0, ind2 = 0;
	char* temp_string = malloc(strlen(str1) + 1);
	// copy each char from str1 to temp_string
	// if "$$" is found, append pid to string
	while (ind1 < strlen(str1)) {
		if (str1[ind1] == '$' && str1[ind1 + 1] == '$') {
			temp_string = realloc(temp_string, strlen(temp_string) + strlen(pid));
			strcat(temp_string, pid);
			ind1++;
			ind2 = ind2 + strlen(pid) - 1;
		}
		else {
			temp_string[ind2] = str1[ind1];
		}
		ind1++;
		ind2++;
	}
	temp_string[ind2] = 0;

	return temp_string;
}

// parses input provided by user. checks and updates variable foreground
void parseInput(char** arguments, bool *foreground) {
	int index = 0;
	char* token;
	char* inputPtr;
	token = strtok_r(input, " \n", &inputPtr);
	// delimit input string by space and new line
	while (token != NULL) {
		// check if any argument has $$ and expand when appropraite
		arguments[index] = string_expansion(token, curpid);
		index++;
		token = strtok_r(NULL, " \n", &inputPtr);
	}
	// last argument must be NULL for execvp to work
	arguments[index] = NULL;
	// if last argument is "&", then command is a background process
	if (strcmp(arguments[index - 1], "&") == 0 && (!foreground_only) {
		*foreground = false;
	}
}

// check if command provided is a built in command
bool builtInCommand(char** arguments, int* exitStatus) {
	// if command is kill, exit shell
	if (strcmp(arguments[0], "exit") == 0) {
		struct pidNode* next = head->next;
			while (head != NULL) {
				next = head->next;
				kill(head->pid, 9);
				free(head);
				head = next;
				exit(0);
			}
	}
	// if command is cd, change directory
	else if (strcmp(arguments[0], "cd") == 0) {
		if (arguments[1] == '\0') {
			chdir(getenv("HOME"));
		}
		else {
			chdir(arguments[1]);
		}
		return true;
	}
	// if command is status, print status
	else if (strcmp(arguments[0], "status") == 0) {
		printf("exit value %d\n", *exitStatus);
		fflush(stdout);
		return true;
	}
	return false;
}

// function to check if background processes have finished executing
void checkProcesses(struct pidNode* head) {
	struct pidNode* cur = head;
	int processStatus;
	pid_t process;
	while (cur != NULL) {
		process = waitpid(cur->pid, &processStatus, WNOHANG);
		if (process > 0) {
			printf("background pid %d is done: exit value %d\n", process, processStatus);
			fflush(stdout);
		}
	}
}

// function to execute command as input by user
void runCommand(char** arguments, bool* foreground, int* exitStatus, struct node* head) {
	int sourceFD, targetFD, processStatus;
	pid_t process = fork();
	switch (process) {
	case -1:
		// error with fork()
		perror("fork() failed\n");
		exit(1);
		break;
	case 0:
		// all child processes ignore SIGTSTP
		ignore_action.sa_handler = SIG_IGN;
		sigaction(SIGTSTP, &ignore_action, NULL);
		// foreground processes terminates when receiving SIGINT
		if (*foreground) {
			// reset SIGINT to default action if run in foreground
			SIGINT_action.sa_handler = SIG_DFL;
			sigfillset(&SIGINT_action.sa_mask);
			SIGINT_action.sa_flags = 0;
			sigaction(SIGINT, &SIGINT_action, NULL);
		}

		// iterate through arguments for input/output redirection
		for (int i = 0; arguments[i]; i++) {
			// redirect input sign, so open read only file. exit error 1 if not available
			if (strcmp(arguments[i], "<") == 0) {
				// if no file or process running in background redirect to /dev/null
				if (arguments[i + 1] == NULL || !foreground) {
					targetFD = open("/dev/null", O_RDONLY);
				}
				else {
					targetFD = open(arguments[i + 1], O_RDONLY);
				}
				if (sourceFD == -1) {
					perror("error opening read only file");
					exit(1);
				}
				// use dup2 to redirect stdin to file
				int result = dup2(sourceFD, 0);
				if (result == -1) {
					perror("error duplicating file descriptor");
					exit(1);
				}
				// arguments[i] = NULL;
			}
			// redirect output sign, so open write only file. exit error 1 if not available
			else if (strcmp(arguments[i], ">") == 0) {
				// if no file or process running in background redirect to /dev/null
				if (arguments[i + 1] == NULL || !foreground) {
					targetFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0640);
				}
				else {
					targetFD = open(arguments[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 064);
				}
				if (targetFD == -1) {
					perror("error opening write only file");
					exit(1);
				}

				// use dup2 to redirect stdout to file
				int result = dup2(targetFD, 1);
				if (result == -1) {
					perror("error duplicating file descriptor");
					exit(1);
				}
			}
		}
		execvp(arguments[0], arguments);
		perror(execvp);
		exit(1);
		break;
	default:
		// wait for child process if command to be ran as foreground process
		fcntl(sourceFD, F_SETFD, FD_CLOEXEC);
		fcntl(targetFD, F_SETFD, FD_CLOEXEC);

		if (foreground) {
			printf("this is a foreground process\n");
			fflush(stdout);
			pid_t processPid = waitpid(process, &processStatus, 0);
			if (WIFEXITED(processPid)) {
				*exitStatus = WEXITSTATUS(processStatus);
			}
			else {
				*exitStatus = WTERMSIG(childStatus);
				printf("Terminated by %d", *exitStatus)
			}
		}
		else {
			printf("background pid is %d\n", process);
			fflush(stdout);
			addNode(head, process);
			checkProcesses(head);
		}
		break;
	}
}

int main(int argc, char* argv[]) {
	struct sigaction SIGTSTP_action = { 0 }, ignore_action = { 0 };
	
	// set handler to ignore certain signals
	ignore_action.sa_handler = SIG_IGN;

	// set handler for TSTP signal
	SIGTSTP_action.sa_handler = handle_SIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
	
	// Register the ignore_action as the handler for SIGINT
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &ignore_action, NULL);

	char input[2049];
	char* arguments[513];
	struct pidNode* head = NULL;
	int exitStatus;

	while (true) {
		getInput(input);
		if (strcmp(input, "\n") == 0 || input[0] == '#') {
			continue;
		}
		bool foreground = true;		// foreground set to true by default unless "&" argument found
		
		parseInput(arguments, *foreground);
		runCommand(arguments, *foreground, *exitStatus, head);

		// free up space for arguments
		for (int i = 0; arguments[i]; i++) {
			free(arguments[i]);
		}
		memset(input, 0, sizeof(input));
	}

	/*
	int index = 0;				// used to delimit input
	int sourceFD;
	int targetFD;
	char input[2049];			// used to take in input
	char* arguments[513];		// used to split input into array
	char* token;				// strtok token
	char* inputPtr;				// used for strtok Ptr
	char* curpid = pid_to_string(getpid());

	struct sigaction SIGTSTP_action = { 0 }, SIGTERM_action = { 0 }, ignore_action = { 0 }, default_action = { 0 }, SIGINT_action = { 0 }, SIGCHLD_action = { 0 };
	struct pidNode* head = NULL;
	struct pidNode* cur = NULL;

	// Block all catchable signals while handle_SIGTSTP is running
	SIGTSTP_action.sa_handler = handle_SIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	// SIGTSTP to restart function on interrupt
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);

	// The ignore_action struct as SIG_IGN as its signal handler
	ignore_action.sa_handler = SIG_IGN;

	// Register the ignore_action as the handler for SIGINT
	sigaction(SIGINT, &ignore_action, NULL);
	sigaction(SIGTERM, &ignore_action, NULL);

	while (true) {
		int exitStatus;
		int cleanupStatus;
		bool foreground = true;		// used to check if command is executed in foreground
		index = 0;
		pid_t cleanup = waitpid(-1, &cleanupStatus, WNOHANG);
		if (cleanup > 0) {
			printf("Background process %d is done: Exit value %d\n", cleanup, cleanupStatus);
		}
		printf(": ");
		fflush(stdout);
		fgets(input, 2048, stdin);
		// check special inputs
		// check for no input or input that begins with "#"
		if (strcmp(input, "\n") == 0 || input[0] == '#') {
			continue;
		}
		else {
			// delimit input into an array of strings and check for $$ expansion
			// first argument is built in command
			token = strtok_r(input, " \n", &inputPtr);
			// delimit input string by space and new line
			while (token != NULL) {
				// check if any argument has $$ and expand when appropraite
				char* temp_string = string_expansion(token, curpid);
				arguments[index] = calloc(strlen(temp_string) + 1, sizeof(char));
				strcpy(arguments[index], temp_string);
				free(temp_string);
				index++;
				token = strtok_r(NULL, " \n", &inputPtr);
			}
			arguments[index] = NULL;

			if (check_builtin(arguments, status)) {}
			else {
				// last argument is &, so process will be run in background, and don't include argument
				if (strcmp(arguments[index - 1], "&") == 0) {
					foreground = false;
					arguments[index - 1] = NULL;
				}

				// SIGTSTP signal for foreground only mode is on
				if (foreground_only) foreground = true;

				// fork child process to execute command
				pid_t process = fork();
				switch (process) {
				case -1:
					// error with fork()
					perror("fork() failed\n");
					exit(1);
					break;
				case 0:
					// background processes ignores SIGTSTP
					ignore_action.sa_handler = SIG_IGN;
					sigaction(SIGTSTP, &ignore_action, NULL);

					// execute command as child process, return 1 as exit status if exec fails
					for (int i = 0; arguments[i]; i++) {
						// redirect input sign, so open read only file. exit error 1 if not available
						if (strcmp(arguments[i], "<") == 0) {
							if (foreground) {
								sourceFD = open(arguments[i + 1], O_RDONLY);
							}
							else {
								sourceFD = open("/dev/null", O_RDONLY);
							}
							if (sourceFD == -1) {
								perror("error opening read only file");
								exit(1);
							}
							// use dup2 to redirect stdin to file
							int result = dup2(sourceFD, 0);
							if (result == -1) {
								perror("source dup2()");
								exit(1);
							}
							fcntl(sourceFD, F_SETFD, FD_CLOEXEC);
							arguments[i] = NULL;
						}
						// redirect output sign, so open write only file. exit error 1 if not available
						else if (strcmp(arguments[i], ">") == 0) {
							if (foreground) {
								targetFD = open(arguments[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
							}
							else {
								targetFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
							}
							if (targetFD == -1) {
								perror("error opening write only file");
								exit(1);
							}
							// use dup2 to redirect stdout to file
							int result = dup2(targetFD, 1);
							if (result == -1) {
								perror("source dup2()");
								exit(1);
							}
							fcntl(targetFD, F_SETFD, FD_CLOEXEC);
							arguments[i] = NULL;
						}
					}
					if (foreground) {
						// reset SIGINT to default action if run in foreground
						SIGINT_action.sa_handler = handle_SIGINT;
						sigfillset(&SIGINT_action.sa_mask);
						SIGINT_action.sa_flags = 0;
						sigaction(SIGINT, &SIGINT_action, NULL);
					}
					execvp(arguments[0], arguments);
					perror(execvp);
					exit(1);
					break;
				default:
					// wait for child process if command to be ran as foreground process
					fcntl(sourceFD, F_SETFD, FD_CLOEXEC);
					fcntl(targetFD, F_SETFD, FD_CLOEXEC);

					if (foreground) {
						printf("this is a foreground process\n");
						fflush(stdout);
						pid_t processPid = waitpid(process, &exitStatus, 0);
					}
					else {
						/*
						SIGCHLD_action.sa_handler = handle_SIGCHLD;
						sigfillset(&SIGCHLD_action.sa_mask);
						SIGCHLD_action.sa_flags = SA_RESTART;
						sigaction(SIGCHLD, &SIGCHLD_action, NULL);
						pid_t processPid = waitpid(process, &exitStatus, WNOHANG);
						*/
						printf("background pid is %d\n", process);
						fflush(stdout);
					}
					break;
				}
				// free up space for input and argument array
			}
			for (int i = 0; arguments[i]; i++) {
				free(arguments[i]);
			}
			memset(input, 0, sizeof(input));
		}
	}
	return EXIT_SUCCESS;
	*/
}
