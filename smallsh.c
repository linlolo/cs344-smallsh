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

// create struct to store background pids in a linked list
struct pidNode {
	pid_t pid;
	struct pidNode *next;
};

struct pidNode* head = NULL;

// adds a pidNode at the beginning of the linked list
struct pidNode* addNode(pid_t pid) {
	struct pidNode *newNode = (struct pidNode*)malloc(sizeof(struct pidNode));

	newNode->pid = pid;
	newNode->next = head;
	head = newNode;
	return newNode;
}

// removes specified node from linked list
struct pidNode* removeNode(struct pidNode* prev, struct pidNode* cur) {
	struct pidNode* next = cur->next;
	if (head == cur) {
		head = next;
	}
	else {
		prev->next = next;
	}
	free(cur);
	return next;
}

// signal handler for SIGTSTP. Enter and exit foreground-only mode
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

// convert pid to string of numbers
char* pidToString(pid_t num) {
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
void parseInput(char** arguments, char* input, char* curpid, bool *foreground) {
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
	if (strcmp(arguments[index - 1], "&") == 0) {
		*foreground = false;
		arguments[index - 1] = NULL;
	}
	if (foreground_only) {
		*foreground = true;
	}
}

// check if command provided is a built in command
bool builtInCommand(char** arguments, int* exitStatus, struct pidNode* head) {
	// if command is kill, exit shell
	if (strcmp(arguments[0], "exit") == 0) {
		if (head != NULL) {
			struct pidNode* next = head->next;
			while (head != NULL) {
				next = head->next;
				kill(head->pid, 9);
				free(head);
				head = next;
			}
		}
		exit(0);
	}
	// if command is cd, change directory
	else if (strcmp(arguments[0], "cd") == 0) {
		// if directory is not provided, then cd to HOME
		if (arguments[1] == '\0') {
			chdir(getenv("HOME"));
		}
		else {
			chdir(arguments[1]);
		}
		return true;
	}
	// if command is status, print exit status of last process
	else if (strcmp(arguments[0], "status") == 0) {
		if (*exitStatus <= 1) {
			printf("exit value %d\n", *exitStatus);
		}
		else {
			printf("terminated by signal %d\n", *exitStatus);
		}
		fflush(stdout);
		return true;
	}
	return false;
}

// function to check if background processes have finished executing
void checkProcesses() {
	struct pidNode* prev = head;
	struct pidNode* cur = head;
	int processStatus;
	pid_t process;
	// iterate through linked list to check for finished processes
	while (cur != NULL) {
		process = waitpid(cur->pid, &processStatus, WNOHANG);
		if (process > 0) {
			if (WIFEXITED(processStatus)) {
				printf("background pid %d is done: exit value %d\n", process, processStatus);
				fflush(stdout);
			}
			else {
				printf("Process %d has been terminated by signal %d\n", process, processStatus);
				fflush(stdout);
			}
			// remove finished processes from list
			cur = removeNode(prev, cur);
		}
		else {
			prev = cur;
			cur = cur->next;
		}
	}
}

// function to execute command as input by user
void runCommand(char** arguments, bool* foreground, int* exitStatus, struct pidNode* head, struct sigaction ignore_action, struct sigaction SIGINT_action) {
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
		sigaction(SIGTSTP, &ignore_action, NULL);
		// foreground processes terminates when receiving SIGINT
		if (*foreground) {
			// reset SIGINT to default action if run in foreground
			SIGINT_action.sa_handler = SIG_DFL;
			sigaction(SIGINT, &SIGINT_action, NULL);
		}

		// iterate through arguments for input/output redirection
		for (int i = 0; arguments[i]; i++) {
			// redirect input sign, so open read only file. exit error 1 if not available
			if (strcmp(arguments[i], "<") == 0) {
				arguments[i] = NULL;
				// if no file or process running in background, redirect to /dev/null
				if (arguments[i + 1] == NULL || !foreground) {
					sourceFD = open("/dev/null", O_RDONLY);
				}
				else {
					sourceFD = open(arguments[i + 1], O_RDONLY);
				}
				if (sourceFD == -1) {
					perror("error opening read file for input");
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
				arguments[i] = NULL;
				// if no file or process running in background redirect to /dev/null
				if (arguments[i + 1] == NULL || !foreground) {
					targetFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0640);
				}
				else {
					targetFD = open(arguments[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0640);
				}
				if (targetFD == -1) {
					perror("error opening write file for output");
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
		perror("Error");
		exit(1);
		break;
	default:
		// close file descriptors on when child exec finishes
		fcntl(sourceFD, F_SETFD, FD_CLOEXEC);
		fcntl(targetFD, F_SETFD, FD_CLOEXEC);

		// wait for child process if command to be ran as foreground process
		if (*foreground) {
			pid_t processPid = waitpid(process, &processStatus, 0);
			// update exit status when child process finishes
			if (WIFEXITED(processStatus)) {
				*exitStatus = WEXITSTATUS(processStatus);
			}
			else {
				*exitStatus = WTERMSIG(processStatus);
				printf("Process %d has been terminated by signal %d\n", process, processStatus);
				fflush(stdout);
			}
		}
		else {
			// child is background process and continue to execute
			printf("background pid is %d\n", process);
			fflush(stdout);
			head = addNode(process);
			}
		break;
	}
}

int main(int argc, char* argv[]) {
	struct sigaction SIGTSTP_action = { 0 }, SIGINT_action = { 0 }, ignore_action = { 0 };
	
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

	int exitStatus;
	char input[2049];
	char* arguments[513];
	char* curpid = pidToString(getpid());

	while (true) {
		// check background process right before return control to user
		checkProcesses();
		getInput(input);
		// skip input if nothing is entered or is a comment
		if (strcmp(input, "\n") == 0 || input[0] == '#') {
			continue;
		}
		bool foreground = true;		// foreground set to true by default unless "&" argument found
		
		parseInput(arguments, input, curpid, &foreground);
		if (builtInCommand(arguments, &exitStatus, head)) continue;
		runCommand(arguments, &foreground, &exitStatus, head, ignore_action, SIGINT_action);

		// free up space for arguments
		memset(input, 0, sizeof(input));
	}
	return EXIT_SUCCESS;
}
