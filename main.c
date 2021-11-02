# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <stdbool.h>
# include <sys/types.h>
# include <dirent.h>
# include <sys/stat.h>
# include <time.h>
# include <fcntl.h>
# include <sys/types.h>
# include <signal.h>
# include <unistd.h>

struct pidList {
	pid_t pid;
	struct pidList* next;
};

void handle_SIGINT(int signo) {
	char* message = "Caught SIGINT, sleeping for 10 seconds\n";
	write(STDOUT_FILENO, message, 39);
	// Raise SIGUSR2. However, since this signal is blocked until handle_SIGNIT
	// finishes, it will be delivered only when handle_SIGINT finishes
	raise(SIGUSR2);
	// Sleep for 10 seconds
	sleep(10);
}

void undo_SIGTSTP(int signo) {

}

void handle_SIGTSTP(int signo) {
	char* message = "Entering foreground-only mode (& is now ignored)\n";
	write(STDOUT_FILENO, message, 50);

}

// Handler for SIGUSR2
void handle_SIGUSR2(int signo) {
	char* message = "Caught SIGUSR2, exiting!\n";
	write(STDOUT_FILENO, message, 25);
	exit(0);
}

char* pid_to_string(pid_t num) {
	char* temp_string = malloc(sizeof(pid_t) + 1);
	sprintf(temp_string, "%d", num);
	return temp_string;
}

char* string_expansion(char* str1, char* pid) {
	int ind1 = 0, ind2 = 0;
	char* temp_string = malloc(strlen(str1) + 1);
	while (ind1 < strlen(str1)) {
		if (str1[ind1] == '$' && str1[ind1+1] == '$') {
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

bool check_builtin(char* arg, char* input, int exitStatus) {
	// if command is kill, exit shell
	if (strcmp(arg, "exit") == 0) {
		exit(0);
	}
	// if command is cd, change directory
	else if (strcmp(arg, "cd") == 0) {
		char* inputPtr;
		char* token = strtok_r(input, " \n", &inputPtr);
		token = strtok_r(NULL, " \n", &inputPtr);
		if (token == NULL) {
			chdir(getenv("HOME"));
		}
		else {
			chdir(token);
		}
		return true;
	}
	// if command is status, print status
	else if (strcmp(arg, "status") == 0) {
		printf("exit value %d\n", exitStatus);
		fflush(stdout);
		return true;
	}
	return false;
}

bool execute_command(char** arguments, char** arg_in, char** arg_out, char* input, char* curpid, int exitStatus) {
	char* token;
	char* inputPtr;
	int left = 0;
	int index = 0;
	bool foreground;

	fgets(input, 2048, stdin);
	// check special inputs
	// check for no input or input that begins with "#"
	if (strcmp(input, "\n") == 0 || input[0] == '#') {
		return false;
	}
	else {
		// delimit input into an array of strings and check for $$ expansion
		token = strtok_r(input, " \n", &inputPtr);
		if (check_builtin(token, input, exitStatus)) {
			return false;
		}
		else {
			while (token != NULL) {
				char* temp_string = string_expansion(token, curpid);
				arguments[index] = calloc(strlen(temp_string) + 1, sizeof(char));
				strcpy(arguments[index], temp_string);
				free(temp_string);
				index++;
				token = strtok_r(NULL, " \n", &inputPtr);
			}
			foreground = false;
			arguments[index] = NULL;
			printf("arg1 is %s arg2 is %s\n", arguments[0], arguments[1]);
			fflush(stdout);
			pid_t process = fork();
			switch (process) {
			case -1:
				// error with fork()
				perror("fork() failed\n");
				exit(1);
				break;
			case 0:
				// execute command as child process, return 1 as exit status if exec fails
				for (int i = 0; arguments[i]; i++) {
					if (strcmp(arguments[i], "<") == 0) {
						int sourceFD = open(arguments[i+1], O_RDONLY);
						if (sourceFD == -1) {
							perror("source open()");
							exit(1);
						}
						int result = dup2(sourceFD, 0);
						if (result == -1) {
							perror("source dup2()");
							exit(2);
						}
						fcntl(sourceFD, F_SETFD, FD_CLOEXEC);
						arguments[i] = NULL;
					}
					else if (strcmp(arguments[i], ">") == 0) {
						int targetFD = open(arguments[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
						if (targetFD == -1) {
							perror("source open()");
							exit(1);
						}
						int result = dup2(targetFD, 1);
						if (result == -1) {
							perror("source dup2()");
							exit(2);
						}
						fcntl(targetFD, F_SETFD, FD_CLOEXEC);
						arguments[i] = NULL;
					}
				}
				execvp(arguments[0], arguments);
				perror(execvp);
				exit(1);
				break;
			default:
				// wait for child process if command to be ran as foreground process
				if (foreground) {
					pid_t processPid = waitpid(process, &exitStatus, 0);
				}
				break;
			}
			// free up space for input and argument array
			memset(input, 0, sizeof(input));
			for (int i = 0; arguments[i]; i++) {
				free(arguments[i]);
			}
		}
	}
	return true;
}

int find_arr_len(char* arguments[]) {
	int i = 0;
	while (i < 512) {
		if (arguments[i] == 0) {
			break;
		}
		else {
			i++;
		}
	}
	return i-1;
}

void copy_array(char** source, char** dest) {
	for (int i = 0; source[i]; i++) {
		strcpy(dest[i], source[i]);
	}
}

void execute_process(char** arg1, char** arg2, bool foreground) {

}

int main(int argc, char* argv[]) {
	int exitStatus;				// used to keep track of exit status
	int index;					// used to delimit input
	char input[2049];			// used to take in input
	char* arguments[512];		// used to split input into array
	char* arg_in[512];			// argument array for input
	char* arg_out[512];			// argument array for output
	char* token;				// strtok token
	char* inputPtr;				// used for strtok Ptr
	char* cur_path[PATH_MAX];	// used to store current working directory
	bool foreground;			// used to check if command is executed in foreground
	char* curpid = pid_to_string(getpid());

	int count = 0;

	/*
	// copied from exploration
	struct sigaction SIGINT_action = { 0 }, SIGTSTP_action = {0}, SIGUSR2_action = { 0 }, ignore_action = { 0 };

	// Fill out the SIGINT_action struct
	// Register handle_SIGINT as the signal handler
	SIGINT_action.sa_handler = handle_SIGINT;
	// Block all catchable signals while handle_SIGINT is running
	sigfillset(&SIGINT_action.sa_mask);
	// No flags set
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);

	// Fill out the SIGUSR2_action struct
	// Register handle_SIGUSR2 as the signal handler
	SIGUSR2_action.sa_handler = handle_SIGUSR2;
	// Block all catchable signals while handle_SIGUSR2 is running
	sigfillset(&SIGUSR2_action.sa_mask);
	// No flags set
	SIGUSR2_action.sa_flags = 0;
	sigaction(SIGUSR2, &SIGUSR2_action, NULL);

	// The ignore_action struct as SIG_IGN as its signal handler
	ignore_action.sa_handler = SIG_IGN;

	// Register the ignore_action as the handler for SIGTERM, SIGHUP, SIGQUIT.
	// So all three of these signals will be ignored.
	sigaction(SIGTERM, &ignore_action, NULL);
	sigaction(SIGHUP, &ignore_action, NULL);
	sigaction(SIGQUIT, &ignore_action, NULL);

	struct pidList* head = NULL;		// instantiate linked list to store background process PIDs

	getcwd(cur_path, sizeof(cur_path));
	chdir(getenv("HOME"));
	chdir("vsprojects");
	*/

	while (count < 10) {
		foreground = true;
		printf(": ");
		fflush(stdout);
		execute_command(arguments, arg_in, arg_out, input, curpid, exitStatus);
		printf("should have returned true\n");
		
		// free up space for input and argument array
		count++;
		}
	return EXIT_SUCCESS;
}
