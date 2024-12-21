// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>

#include "cmd.h"
#include "utils.h"
#include "my_string.h"

#define READ		0
#define WRITE		1

static int shell_fwrite(const void *buff, size_t size, size_t nitems,int fd) {
 	size_t total_bytes = size * nitems;
 	size_t total_writen_bytes = 0;

 	while (total_bytes != total_writen_bytes) {
 		size_t remained_bytes = total_bytes - total_writen_bytes;
 		size_t written_bytes = write(fd, buff + total_writen_bytes, remained_bytes);

		if (written_bytes < 0)
			return written_bytes;
 		total_writen_bytes += written_bytes;
 	}

 	return total_writen_bytes;
}

static int run_command(simple_command_t *s, char *params[]) {
	if (s->in) {
		int in = open(s->in->string, O_RDONLY | O_CREAT, 0644);
		if (dup2(in, 0) == -1) {
			return -1;
		}
		close(in);
	}

	if (s->out) {
		int out;
		if (s->io_flags & IO_OUT_APPEND)
			out = open(s->out->string, O_WRONLY | O_CREAT | O_APPEND, 0644);
		else
			out = open(s->out->string, O_WRONLY | O_CREAT | O_TRUNC, 0644);

		if (dup2(out, 1) == -1)
			return -1;
		close(out);
	}

	if (s->err) {
		if (s->out && !my_strcmp(s->err->string, s->out->string)) {
			if (dup2(1, 2) == -1)
				return -1;
		} else {
			int err;
			if (s->io_flags & IO_ERR_APPEND)
				err = open(s->err->string, O_WRONLY | O_CREAT | O_APPEND, 0644);
			else
				err = open(s->err->string, O_WRONLY | O_CREAT | O_TRUNC, 0644);

			if (dup2(err, 2) == -1)
				return -1;
			close(err);
		}
	}
		
	return execvp(s->verb->string, params);
}

static int run_external_command(simple_command_t *s) {
	char *params[1001];
	int pos = 0;
	params[pos++] = (char*) s->verb->string;
	word_t *param = s->params;
	
	while (param) {
		params[pos] = (char*)param->string;
		pos++;
		param = param->next_word;
	}
	params[pos] = NULL;

	pid_t pid = fork();
	if (pid == 0) {
		return run_command(s, params);
	} else {
		int status;
		waitpid(pid, &status, 0);
		return status;
	}
}

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	return 0;
	//DIE(!dir, "dir can't be null");

	return chdir(dir->string);
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	_exit(0);
	return 0;
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	if (!s) {
		char *message = "s can't be null\n";
		shell_fwrite(message, 1, my_strlen(message), 1);
	}

	if (!my_strcmp(s->verb->string, "exit"))
		return shell_exit();

	/* TODO: If variable assignment, execute the assignment and return
	 * the exit status.
	 */

	return run_external_command(s);
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* TODO: Execute cmd1 and cmd2 simultaneously. */

	return true; /* TODO: Replace with actual exit status. */
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	/* TODO: Redirect the output of cmd1 to the input of cmd2. */

	return true; /* TODO: Replace with actual exit status. */
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	if (!c) {
		char *message = "c can't be null\n";
		shell_fwrite(message, 1, my_strlen(message), 1);
	}

	if (c->op == OP_NONE) {
		return parse_simple(c->scmd, level, father);
	}

	switch (c->op) {
	case OP_SEQUENTIAL:
		/* TODO: Execute the commands one after the other. */
		break;

	case OP_PARALLEL:
		/* TODO: Execute the commands simultaneously. */
		break;

	case OP_CONDITIONAL_NZERO:
		/* TODO: Execute the second command only if the first one
		 * returns non zero.
		 */
		break;

	case OP_CONDITIONAL_ZERO:
		/* TODO: Execute the second command only if the first one
		 * returns zero.
		 */
		break;

	case OP_PIPE:
		/* TODO: Redirect the output of the first command to the
		 * input of the second.
		 */
		break;

	default:
		return SHELL_EXIT;
	}

	return 0; /* TODO: Replace with actual exit code of command. */
}
