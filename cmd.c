// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>

#include <fcntl.h>
#include <unistd.h>

#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	if (dir == NULL)
		return true;

	char *next_dir = get_word(dir);

	if (next_dir[0] == '/') {
		if (chdir(next_dir) == -1)
			return false;
		return true;
	}

	char *path = getcwd(NULL, PATH_MAX);

	if (path == NULL)
		return false;

	size_t current_path_len = strlen(path);

	if (current_path_len + strlen(next_dir) + 1 >= PATH_MAX) {
		free(path);
		return false;
	}

	memmove(path + current_path_len + 1, next_dir, strlen(next_dir) + 1);

	path[current_path_len] = '/';

	if (chdir(path) == -1) {
		free(path);
		return false;
	}

	free(path);
	return true;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	return SHELL_EXIT;
}

void file_operations(simple_command_t *s)
{
	if (s->in != NULL) {
		int in_fd = open(s->in->string, O_RDONLY);

		if (in_fd == -1)
			exit(1);
		if (dup2(in_fd, STDIN_FILENO) == -1)
			exit(1);
		close(in_fd);
	}

	if (s->out != NULL || s->err != NULL) {
		int out_fd = -1;
		int err_fd = -1;
		int counter = 0;

		char *out = get_word(s->out);
		char *err = get_word(s->err);

		if (s->io_flags & (IO_OUT_APPEND | IO_ERR_APPEND)) {
			out_fd = open(out, O_WRONLY | O_CREAT | O_APPEND, 0644);
			counter = 1;
		} else {
			out_fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
			counter = 2;
		}

		dup2(out_fd, STDOUT_FILENO);

		if (err != NULL) {
			if (out != NULL && strcmp(out, err) == 0) {
				err_fd = out_fd;
			} else {
				if (counter == 1)
					err_fd = open(err, O_WRONLY | O_CREAT | O_APPEND, 0644);
				else if (counter == 2)
					err_fd = open(err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
			}
			dup2(err_fd, STDERR_FILENO);
		}

		if (out_fd != -1 && out_fd != err_fd)
			close(out_fd);
		if (err_fd != -1)
			close(err_fd);
	}
}

int parse_simple_cd_case(char *command, simple_command_t *s)
{
	int out = fcntl(STDOUT_FILENO, F_DUPFD, STDOUT_FILENO);
	int err = fcntl(STDERR_FILENO, F_DUPFD, STDERR_FILENO);

	file_operations(s);
	free(command);

	if (dup2(out, STDOUT_FILENO) == -1)
		exit(1);
	if (dup2(err, STDERR_FILENO) == -1)
		exit(1);

	return (shell_cd(s->params)) ? 0 : -1;
}

int parse_simple_quit_case(char *command)
{
	free(command);
	return shell_exit();
}

int parse_simple_pwd_case(char *command, simple_command_t *s)
{
	int out = fcntl(STDOUT_FILENO, F_DUPFD, STDOUT_FILENO);
	int err = fcntl(STDERR_FILENO, F_DUPFD, STDERR_FILENO);

	file_operations(s);
	free(command);

	char *path;

	path = getcwd(NULL, PATH_MAX);
	dprintf(STDOUT_FILENO, "%s\n", path);

	if (dup2(out, STDOUT_FILENO) == -1)
		exit(1);
	if (dup2(err, STDERR_FILENO) == -1)
		exit(1);

	return 0;
}

int parse_simple_external(char *command, simple_command_t *s)
{
	pid_t pid = fork();
	int state;

	switch (pid) {
	case -1:
		free(command);
		return -1;

	case 0:
		file_operations(s);
		int len;
		char **args = get_argv(s, &len);

		if (args == NULL)
			exit(EXIT_FAILURE);
		if (execvp(command, args) == -1) {
			fprintf(stderr, "Execution failed for '%s'\n", command);
			free(args);
			free(command);
			exit(EXIT_FAILURE);
		}
		break;

	default:
		if (waitpid(pid, &state, 0) == -1) {
			free(command);
			return -1;
		}
		free(command);
		if (WIFEXITED(state))
			return WSTOPSIG(state);
		else
			return -1;
	}
	return 0;
}


/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	if (s == NULL || s->verb == NULL)
		return -1;

	char *com = get_word(s->verb);

	if (strcmp(com, "cd") == 0)
		return parse_simple_cd_case(com, s);
	else if (strcmp(com, "exit") == 0 || strcmp(com, "quit") == 0)
		return parse_simple_quit_case(com);
	else if (strcmp(com, "pwd") == 0)
		return parse_simple_pwd_case(com, s);

	char *sign = strchr(com, '=');

	if (sign != NULL)
		return putenv(com);
	return parse_simple_external(com, s);
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
	command_t *father)
{
	pid_t pid1, pid2;
	int status1, status2;

	pid1 = fork();
	if (pid1 == -1)
		return false;

	if (pid1 == 0)
		exit(parse_command(cmd1, level + 1, father));

	pid2 = fork();
	if (pid2 == -1)
		return false;

	if (pid2 == 0)
		exit(parse_command(cmd2, level + 1, father));

	if (waitpid(pid1, &status1, 0) == -1)
		return false;
	if (waitpid(pid2, &status2, 0) == -1)
		return false;

	if (WIFEXITED(status1) && WIFEXITED(status2))
		return WSTOPSIG(status1) == 0 && WSTOPSIG(status2) == 0;

	return false;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
	command_t *father)
{
	int pipefd[2];
	pid_t pid1, pid2;
	int state;

	if (pipe(pipefd) == -1)
		return false;

	pid1 = fork();

	if (pid1 == 0) {
		close(pipefd[READ]);
		if (dup2(pipefd[WRITE], STDOUT_FILENO) == -1)
			exit(1);
		exit(!parse_command(cmd1, level, father));
		return true;
	}

	pid2 = fork();
	if (pid2 == 0) {
		close(pipefd[WRITE]);
		if (dup2(pipefd[READ], STDIN_FILENO) == -1)
			exit(1);
		exit(!parse_command(cmd2, level, father));
		return true;
	}

	close(pipefd[READ]);
	close(pipefd[WRITE]);

	if (waitpid(pid1, &state, 0) == -1)
		return false;
	if (waitpid(pid2, &state, 0) == -1)
		return false;

	if (WIFSTOPPED(state) || WSTOPSIG(state) != 0)
		return false;

	return true;
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	/* TODO: sanity checks */

	if (c->op == OP_NONE) {
		/* TODO: Execute a simple command. */
		int result = parse_simple(c->scmd, level, c);
		return result; /* TODO: Replace with actual exit code of command. */
	}

	switch (c->op) {
	case OP_SEQUENTIAL:
		/* TODO: Execute the commands one after the other. */
		parse_command(c->cmd1, level, c);
		parse_command(c->cmd2, level, c);
		break;

	case OP_PARALLEL:
		/* TODO: Execute the commands simultaneously. */
		run_in_parallel(c->cmd1, c->cmd2, level, c);
		break;

	case OP_CONDITIONAL_NZERO:
	{
		int ret1 = parse_command(c->cmd1, level, c);

		if (ret1 != 0)
			return parse_command(c->cmd2, level + 1, c);
		return ret1;
	}
	break;

	case OP_CONDITIONAL_ZERO:
	{
		int ret1 = parse_command(c->cmd1, level, c);

		if (ret1 == 0)
			return parse_command(c->cmd2, level + 1, c);
		return ret1;
	}
	break;

	case OP_PIPE:
		/* TODO: Redirect the output of the first command to the
		 * input of the second.
		 */
	{
		if (!run_on_pipe(c->cmd1, c->cmd2, level, c))
			return 0;
		else
			return 1;
	}

	break;

	default:
		return SHELL_EXIT;
	}

	return 0; /* TODO: Replace with actual exit code of command. */
}
