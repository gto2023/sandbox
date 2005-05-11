/*
**    Path sandbox for the gentoo linux portage package system, initially
**    based on the ROCK Linux Wrapper for getting a list of created files
**
**  to integrate with bash, bash should have been built like this
**
**  ./configure --prefix=<prefix> --host=<host> --without-gnu-malloc
**
**  it's very important that the --enable-static-link option is NOT specified
**    
**    Copyright (C) 2001 Geert Bevin, Uwyn, http://www.uwyn.com
**    Distributed under the terms of the GNU General Public License, v2 or later 
**    Author : Geert Bevin <gbevin@uwyn.com>
**  $Header$
*/

/* #define _GNU_SOURCE */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include "sandbox.h"

int preload_adaptable = 1;
int cleaned_up = 0;
int print_debug = 0;
int stop_called = 0;

/* Read pids file, and load active pids into an array.	Return number of pids in array */
int load_active_pids(int fd, int **pids)
{
	char *data = NULL;
	char *ptr = NULL, *ptr2 = NULL;
	int my_pid;
	int num_pids = 0;
	long len;

	pids[0] = NULL;

	len = file_length(fd);

	/* Allocate and zero datablock to read pids file */
	data = (char *)malloc((len + 1) * sizeof(char));
	memset(data, 0, len + 1);

	/* Start at beginning of file */
	lseek(fd, 0L, SEEK_SET);

	/* read entire file into a buffer */
	read(fd, data, len);

	ptr = data;

	/* Loop and read all pids */
	while (1) {
		/* Find new line */
		ptr2 = strchr(ptr, '\n');
		if (ptr2 == NULL)
			break;	/* No more PIDs */

		/* Clear the \n. And  ptr  should have a null-terminated decimal string */
		ptr2[0] = 0;

		my_pid = atoi(ptr);

		/* If the PID is still alive, add it to our array */
		if ((0 != my_pid) && (0 == kill(my_pid, 0))) {
			pids[0] = (int *)realloc(pids[0], (num_pids + 1) * sizeof(int));
			pids[0][num_pids] = my_pid;
			num_pids++;
		}

		/* Put ptr past the NULL we just wrote */
		ptr = ptr2 + 1;
	}

	if (data)
		free(data);
	data = NULL;

	return num_pids;
}

/* Read ld.so.preload file, and loads dirs into an array.  Return number of entries in array */
int load_preload_libs(int fd, char ***preloads)
{
	char *data = NULL;
	char *ptr = NULL, *ptr2 = NULL;
	int num_entries = 0;
	long len;

	preloads[0] = NULL;

	len = file_length(fd);

	/* Allocate and zero datablock to read pids file */
	data = (char *)malloc((len + 1) * sizeof(char));
	memset(data, 0, len + 1);

	/* Start at beginning of file */
	lseek(fd, 0L, SEEK_SET);

	/* read entire file into a buffer */
	read(fd, data, len);

	ptr = data;

	/* Loop and read all pids */
	while (1) {
		/* Find new line */
		ptr2 = strchr(ptr, '\n');

		/* Clear the \n. And  ptr  should have a null-terminated decimal string
		 * Don't break from the loop though because the last line may not
		 * terminated with a \n
		 */
		if (NULL != ptr2)
			ptr2[0] = 0;

		/* If listing does not match our libname, add it to the array */
		if ((strlen(ptr)) && (NULL == strstr(ptr, LIB_NAME))) {
			preloads[0] = (char **)realloc(preloads[0], (num_entries + 1) * sizeof(char **));
			preloads[0][num_entries] = strdup(ptr);
			num_entries++;
		}

		if (NULL == ptr2)
			break;	/* No more PIDs */

		/* Put ptr past the NULL we just wrote */
		ptr = ptr2 + 1;
	}

	if (data)
		free(data);
	data = NULL;

	return num_entries;
}

void cleanup()
{
	int i = 0;
	int success = 1;
	int pids_file = -1, num_of_pids = 0;
	int *pids_array = NULL;
	char pid_string[255];
	char *sandbox_pids_file;

	/* Generate sandbox pids-file path */
	sandbox_pids_file = get_sandbox_pids_file();

	/* Remove this sandbox's bash pid from the global pids
	 * file if it has rights to adapt the ld.so.preload file */
	if ((1 == preload_adaptable) && (0 == cleaned_up)) {
		cleaned_up = 1;
		success = 1;

		if (print_debug)
			printf("Cleaning up pids file.\n");

		/* Stat the PIDs file, make sure it exists and is a regular file */
		if (file_exist(sandbox_pids_file, 1) <= 0) {
			fprintf(stderr, ">>> pids file is not a regular file\n");
			success = 0;
			/* We should really not fail if the pidsfile is missing here, but
			 * rather just exit cleanly, as there is still some cleanup to do */
			return;
		}

		pids_file = file_open(sandbox_pids_file, "r+", 1, 0664, "portage");
		if (-1 == pids_file) {
			success = 0;
			/* Nothing more to do here */
			return;
		}

		/* Load "still active" pids into an array */
		num_of_pids = load_active_pids(pids_file, &pids_array);
		//printf("pids: %d\r\n", num_of_pids);


		file_truncate(pids_file);

		/* if pids are still running, write only the running pids back to the file */
		if (num_of_pids > 1) {
			for (i = 0; i < num_of_pids; i++) {
				if (pids_array[i] != getpid()) {
					sprintf(pid_string, "%d\n", pids_array[i]);

					if (write(pids_file, pid_string, strlen(pid_string)) != strlen(pid_string)) {
						perror(">>> pids file write");
						success = 0;
						break;
					}
				}
			}

			file_close(pids_file);
			pids_file = -1;
		} else {

			file_close(pids_file);
			pids_file = -1;

			/* remove the pidsfile, as this was the last sandbox */
			unlink(sandbox_pids_file);
		}

		if (pids_array != NULL)
			free(pids_array);
		pids_array = NULL;
	}

	free(sandbox_pids_file);
	if (0 == success)
		return;
}

void stop(int signum)
{
	if (stop_called == 0) {
		stop_called = 1;
		printf("Caught signal %d in pid %d\r\n", signum, getpid());
		cleanup();
	} else {
		fprintf(stderr, "Pid %d alreadly caught signal and is still cleaning up\n", getpid());
	}
}

void setenv_sandbox_write(char *home_dir, char *portage_tmp_dir, char *var_tmp_dir, char *tmp_dir)
{
	char buf[1024];

	/* bzero out entire buffer then append trailing 0 */
	memset(buf, 0, sizeof(buf));

	if (!getenv(ENV_SANDBOX_WRITE)) {
		/* these could go into make.globals later on */
		snprintf(buf, sizeof(buf),
			 "%s:%s/.gconfd/lock:%s/.bash_history:",
			 "/dev/zero:/dev/fd/:/dev/null:/dev/pts/:"
			 "/dev/vc/:/dev/pty:/dev/tty:/tmp/:"
			 "/dev/shm/ngpt:/var/log/scrollkeeper.log:"
			 "/usr/tmp/conftest:/usr/lib/conftest:"
			 "/usr/lib32/conftest:/usr/lib64/conftest:"
			 "/usr/tmp/cf:/usr/lib/cf:/usr/lib32/cf:/usr/lib64/cf",
			 home_dir, home_dir);

		if (NULL == portage_tmp_dir) {
			strncat(buf, tmp_dir, sizeof(buf));
			strncat(buf, ":", sizeof(buf));
			strncat(buf, var_tmp_dir, sizeof(buf));
			strncat(buf, ":/tmp/:/var/tmp/", sizeof(buf));
		} else {
			strncat(buf, portage_tmp_dir, sizeof(buf));
			strncat(buf, ":", sizeof(buf));
			strncat(buf, tmp_dir, sizeof(buf));
			strncat(buf, ":", sizeof(buf));
			strncat(buf, var_tmp_dir, sizeof(buf));
			strncat(buf, ":/tmp/:/var/tmp/", sizeof(buf));
		}
		buf[sizeof(buf) - 1] = '\0';
		setenv(ENV_SANDBOX_WRITE, buf, 1);
	}
}

void setenv_sandbox_predict(char *home_dir)
{
	char buf[1024];

	memset(buf, 0, sizeof(buf));

	if (!getenv(ENV_SANDBOX_PREDICT)) {
		/* these should go into make.globals later on */
		snprintf(buf, sizeof(buf), "%s/.:"
			 "/usr/lib/python2.0/:"
			 "/usr/lib/python2.1/:"
			 "/usr/lib/python2.2/:"
			 "/usr/lib/python2.3/:"
			 "/usr/lib/python2.4/:"
			 "/usr/lib/python2.5/:"
			 "/usr/lib/python3.0/:",
			 home_dir);

		buf[sizeof(buf) - 1] = '\0';
		setenv(ENV_SANDBOX_PREDICT, buf, 1);
	}
}

int print_sandbox_log(char *sandbox_log)
{
	int sandbox_log_file = -1;
	char *beep_count_env = NULL;
	int i, color, beep_count = 0;
	long len = 0;
	char *buffer = NULL;

	sandbox_log_file = file_open(sandbox_log, "r", 1, 0664, "portage");
	if (-1 == sandbox_log_file)
		return 0;

	len = file_length(sandbox_log_file);
	buffer = (char *)malloc((len + 1) * sizeof(char));
	memset(buffer, 0, len + 1);
	read(sandbox_log_file, buffer, len);
	file_close(sandbox_log_file);

	color = ((getenv("NOCOLOR") != NULL) ? 0 : 1);

	if (color)
		printf("\e[31;01m");
	printf("--------------------------- ACCESS VIOLATION SUMMARY ---------------------------");
	if (color)
		printf("\033[0m");
	if (color)
		printf("\e[31;01m");
	printf("\nLOG FILE = \"%s\"", sandbox_log);
	if (color)
		printf("\033[0m");
	printf("\n\n");
	printf("%s", buffer);
	if (buffer)
		free(buffer);
	buffer = NULL;
	printf("\e[31;01m--------------------------------------------------------------------------------\033[0m\n");

	beep_count_env = getenv(ENV_SANDBOX_BEEP);
	if (beep_count_env)
		beep_count = atoi(beep_count_env);
	else
		beep_count = DEFAULT_BEEP_COUNT;

	for (i = 0; i < beep_count; i++) {
		fputc('\a', stderr);
		if (i < beep_count - 1)
			sleep(1);
	}
	return 1;
}

int spawn_shell(char *argv_bash[])
{
#ifndef NO_FORK
	int pid;
	int status = 0;
	int ret = 0;

	pid = fork();

	/* Child's process */
	if (0 == pid) {
#endif
		execv(argv_bash[0], argv_bash);
#ifndef NO_FORK
		return 0;
	} else if (pid < 0) {
		return 0;
	}
	ret = waitpid(pid, &status, 0);
	if ((-1 == ret) || (status > 0))
		return 0;
#endif
	return 1;
}

int main(int argc, char **argv)
{
	int ret = 0, i = 0, success = 1;
	int sandbox_log_presence = 0;
	int sandbox_log_file = -1;
	int pids_file = -1;
	long len;

	int *pids_array = NULL;
	int num_of_pids = 0;

	// char run_arg[255];
	char portage_tmp_dir[PATH_MAX];
	char var_tmp_dir[PATH_MAX];
	char tmp_dir[PATH_MAX];
	char sandbox_log[255];
	char sandbox_debug_log[255];
	char sandbox_dir[255];
	char sandbox_lib[255];
	char *sandbox_pids_file;
	char sandbox_rc[255];
	char pid_string[255];
	char **argv_bash = NULL;

	char *run_str = "-c";
	char *home_dir = NULL;
	char *tmp_string = NULL;

	/* Only print info if called with no arguments .... */
	if (argc < 2)
		print_debug = 1;

	if (print_debug)
		printf("========================== Gentoo linux path sandbox ===========================\n");

	/* check if a sandbox is already running */
	if (NULL != getenv(ENV_SANDBOX_ON)) {
		fprintf(stderr, "Not launching a new sandbox instance\n");
		fprintf(stderr, "Another one is already running in this process hierarchy.\n");
		exit(1);
	} else {

		/* determine the location of all the sandbox support files */
		if (print_debug)
			printf("Detection of the support files.\n");

		/* Generate base sandbox path */
		tmp_string = get_sandbox_path(argv[0]);
		strncpy(sandbox_dir, tmp_string, 254);
		if (tmp_string)
			free(tmp_string);
		tmp_string = NULL;
		strcat(sandbox_dir, "/");

		/* Generate sandbox lib path */
		tmp_string = get_sandbox_lib(sandbox_dir);
		strncpy(sandbox_lib, tmp_string, 254);
		if (tmp_string)
			free(tmp_string);
		tmp_string = NULL;

		/* Generate sandbox pids-file path */
		sandbox_pids_file = get_sandbox_pids_file();

		/* Generate sandbox bashrc path */
		tmp_string = get_sandbox_rc(sandbox_dir);
		strncpy(sandbox_rc, tmp_string, 254);
		if (tmp_string)
			free(tmp_string);
		tmp_string = NULL;

		/* verify the existance of required files */
		if (print_debug)
			printf("Verification of the required files.\n");

#ifndef SB_HAVE_64BIT_ARCH
		if (file_exist(sandbox_lib, 0) <= 0) {
			fprintf(stderr, "Could not open the sandbox library at '%s'.\n", sandbox_lib);
			return -1;
		}
#endif
		if (file_exist(sandbox_rc, 0) <= 0) {
			fprintf(stderr, "Could not open the sandbox rc file at '%s'.\n", sandbox_rc);
			return -1;
		}

		/* set up the required environment variables */
		if (print_debug)
			printf("Setting up the required environment variables.\n");

		/* Generate sandbox log full path */
		tmp_string = get_sandbox_log();
		strncpy(sandbox_log, tmp_string, 254);
		if (tmp_string)
			free(tmp_string);
		tmp_string = NULL;

		setenv(ENV_SANDBOX_LOG, sandbox_log, 1);

		sprintf(pid_string, "%d", getpid());
		snprintf(sandbox_debug_log, sizeof(sandbox_debug_log), "%s%s%s",
			 DEBUG_LOG_FILE_PREFIX, pid_string, LOG_FILE_EXT);
		setenv(ENV_SANDBOX_DEBUG_LOG, sandbox_debug_log, 1);

		home_dir = getenv("HOME");
		if (!home_dir) {
			home_dir = "/tmp";
			setenv("HOME", home_dir, 1);
		}

		if (NULL == realpath(getenv("PORTAGE_TMPDIR") ? getenv("PORTAGE_TMPDIR")
		                                              : "/var/tmp/portage",
					portage_tmp_dir)) {
			perror(">>> get portage_tmp_dir");
			exit(1);
		}
		if (NULL == realpath("/var/tmp", var_tmp_dir)) {
			perror(">>> get var_tmp_dir");
			exit(1);
		}
		if (NULL == realpath("/tmp", tmp_dir)) {
			perror(">>> get tmp_dir");
			exit(1);
		}

		setenv(ENV_SANDBOX_DIR, sandbox_dir, 1);
		setenv(ENV_SANDBOX_LIB, sandbox_lib, 1);
		setenv(ENV_SANDBOX_BASHRC, sandbox_rc, 1);
		if ((NULL != getenv("LD_PRELOAD")) &&
		    /* FIXME: for now, do not use current LD_PRELOAD if
		     * it contains libtsocks, as it breaks sandbox, bug #91541.
		     */
		    (NULL == strstr(getenv("LD_PRELOAD"), "libtsocks"))) {
			tmp_string = malloc(strlen(getenv("LD_PRELOAD")) +
					strlen(sandbox_lib) + 2);
			if (NULL == tmp_string) {
				perror(">>> Out of memory (LD_PRELOAD)");
				exit(1);
			}
			strncpy(tmp_string, sandbox_lib, strlen(sandbox_lib));
			strncat(tmp_string, " ", 1);
			strncat(tmp_string, getenv("LD_PRELOAD"), strlen(getenv("LD_PRELOAD")));
			setenv("LD_PRELOAD", tmp_string, 1);
			free(tmp_string);
		} else {
			setenv("LD_PRELOAD", sandbox_lib, 1);
		}

		if (!getenv(ENV_SANDBOX_DENY))
			setenv(ENV_SANDBOX_DENY, LD_PRELOAD_FILE, 1);

		if (!getenv(ENV_SANDBOX_READ))
			setenv(ENV_SANDBOX_READ, "/", 1);

		/* Set up Sandbox Write path */
		setenv_sandbox_write(home_dir, portage_tmp_dir, var_tmp_dir, tmp_dir);
		setenv_sandbox_predict(home_dir);

		setenv(ENV_SANDBOX_ON, "1", 0);

		/* if the portage temp dir was present, cd into it */
		if (NULL != portage_tmp_dir)
			chdir(portage_tmp_dir);

		argv_bash = (char **)malloc(6 * sizeof(char *));
		argv_bash[0] = strdup("/bin/bash");
		argv_bash[1] = strdup("-rcfile");
		argv_bash[2] = strdup(sandbox_rc);

		if (argc < 2)
			argv_bash[3] = NULL;
		else
			argv_bash[3] = strdup(run_str);	/* "-c" */

		argv_bash[4] = NULL;	/* strdup(run_arg); */
		argv_bash[5] = NULL;

		if (argc >= 2) {
			for (i = 1; i < argc; i++) {
				if (NULL == argv_bash[4])
					len = 0;
				else
					len = strlen(argv_bash[4]);

				argv_bash[4] = (char *)realloc(argv_bash[4], (len + strlen(argv[i]) + 2) * sizeof(char));

				if (0 == len)
					argv_bash[4][0] = 0;
				if (1 != i)
					strcat(argv_bash[4], " ");

				strcat(argv_bash[4], argv[i]);
			}
		}

		/* set up the required signal handlers */
		signal(SIGHUP, &stop);
		signal(SIGINT, &stop);
		signal(SIGQUIT, &stop);
		signal(SIGTERM, &stop);

		/* this one should NEVER be set in ebuilds, as it is the one
		 * private thing libsandbox.so use to test if the sandbox
		 * should be active for this pid, or not.
		 *
		 * azarah (3 Aug 2002)
		 */

		setenv("SANDBOX_ACTIVE", "armedandready", 1);

		/* Load our PID into PIDs file */
		success = 1;
		if (file_exist(sandbox_pids_file, 1) < 0) {
			success = 0;
			fprintf(stderr, ">>> %s is not a regular file\n", sandbox_pids_file);
		} else {
			pids_file = file_open(sandbox_pids_file, "r+", 1, 0664, "portage");
			if (-1 == pids_file)
				success = 0;
		}
		if (1 == success) {
			/* Grab still active pids */
			num_of_pids = load_active_pids(pids_file, &pids_array);

			/* Zero out file */
			file_truncate(pids_file);

			/* Output active pids, and append our pid */
			for (i = 0; i < num_of_pids + 1; i++) {
				/* Time for our entry */
				if (i == num_of_pids)
					sprintf(pid_string, "%d\n", getpid());
				else
					sprintf(pid_string, "%d\n", pids_array[i]);

				if (write(pids_file, pid_string, strlen(pid_string)) != strlen(pid_string)) {
					perror(">>> pids file write");
					success = 0;
					break;
				}
			}
			/* Clean pids_array */
			if (pids_array)
				free(pids_array);
			pids_array = NULL;
			num_of_pids = 0;

			/* We're done with the pids file */
			file_close(pids_file);
		}

		/* Something went wrong, bail out */
		if (0 == success) {
			perror(">>> pids file write");
			exit(1);
		}

		/* STARTING PROTECTED ENVIRONMENT */
		if (print_debug) {
			printf("The protected environment has been started.\n");
			printf("--------------------------------------------------------------------------------\n");
		}

		if (print_debug)
			printf("Shell being started in forked process.\n");

		/* Start Bash */
		if (!spawn_shell(argv_bash)) {
			if (print_debug)
				fprintf(stderr, ">>> shell process failed to spawn\n");
			success = 0;
		}

		/* Free bash stuff */
		for (i = 0; i < 6; i++) {
			if (argv_bash[i])
				free(argv_bash[i]);
			argv_bash[i] = NULL;
		}
		if (argv_bash)
			free(argv_bash);
		argv_bash = NULL;

		if (print_debug)
			printf("Cleaning up sandbox process\n");

		cleanup();

		if (print_debug) {
			printf("========================== Gentoo linux path sandbox ===========================\n");
			printf("The protected environment has been shut down.\n");
		}

		if (file_exist(sandbox_log, 0)) {
			sandbox_log_presence = 1;
			success = 1;
			if (!print_sandbox_log(sandbox_log))
				success = 0;

#if 0
			if (!success)
				exit(1);
#endif

			sandbox_log_file = -1;
		} else if (print_debug) {
			printf("--------------------------------------------------------------------------------\n");
		}

		free(sandbox_pids_file);

		if ((sandbox_log_presence) || (!success))
			return 1;
		else
			return 0;
	}
}

// vim:noexpandtab noai:cindent ai
