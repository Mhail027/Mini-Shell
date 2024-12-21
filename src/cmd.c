// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <unistd.h>

#include "cmd.h"
#include "utils.h"
#include "my_string.h"
#include "my_stdio.h"

#include <stdio.h>
#include <stdlib.h>

#define READ		0
#define WRITE		1

static char *get_env_var(const char *name)
{
	extern char **environ;

	for (u_int i = 0; environ[i]; ++i) {
		char match = 1;
		u_int j = 0;
		for (; environ[i][j] != '=' && name[j]; j++) {
			if (environ[i][j] != name[j]) {
				match = 0;
				break;
			}
		}
		if (environ[i][j] != '=' || name[j])
			match = 0;

		if (match)
			return environ[i] + j + 1;
	}
	return "";
}

static int set_env_var(word_t *verb)
{
	if (!verb || !verb->next_part || !verb->next_part->next_part)
		return -1;

	char *name = (char*) verb->string;
	char *val;
	if (verb->next_part->next_part->expand == false)
		val = (char*) verb->next_part->next_part->string;
	else
		val = get_env_var(verb->next_part->next_part->string);
	size_t length = my_strlen(name) + my_strlen(val) + 2;
	char *expresion = (char*)mmap(0, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	my_strcat(expresion, name);
	my_strcat(expresion, "=");
	my_strcat(expresion, val);
	expresion[length - 1] = '\0';

	extern char **environ;
	u_int i = 0;
	for (; environ[i]; ++i) {
		char match = 1;
		u_int j = 0;
		for (; environ[i][j] != '=' && name[j]; j++) {
			if (environ[i][j] != name[j]) {
				match = 0;
				break;
			}
		}
		if (environ[i][j] != '=' || !name[j])
			match = 0;

		if (match) {
			environ[i] = expresion;
			return 0;
		}	
	}

	char **new_environ = (char**)mmap(0, 2 * i * sizeof(char*), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	for (int j = 0; j < i; ++j)
		new_environ[j] = environ[j];
	new_environ[i++] = expresion;
	new_environ[i]=NULL;
	environ = new_environ;
	return 0;
}

static int redirect_input(word_t *in, int *old_in) {
	if (!in || !in->string) {
		*old_in = 0;
		return 0;
	}

	*old_in = dup(0);
	if (*old_in == -1)
		return -1;

	int in_fd = open(in->string, O_RDONLY | O_CREAT, 0644);
	if (dup2(in_fd, 0) == -1)
		return -1;
	close(in_fd);

	return 0;
}

static int redirect_output(word_t *out, int flags, int *old_out) {
	if (!out || !out->string) {
		*old_out = 1;
		return 0;
	}

	*old_out = dup(1);
	if (*old_out == -1)
		return -1;

	int out_fd;
	if (flags & IO_OUT_APPEND)
		out_fd = open(out->string, O_WRONLY | O_CREAT | O_APPEND, 0644);
	else
		out_fd = open(out->string, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (dup2(out_fd, 1) == -1)
		return -1;
	close(out_fd);

	return 0;
}

/* 
 *Before must call redirect_output().
 */
static int redirect_error(word_t *err, int flags, int *old_err, word_t *out) {
	if (!err || !err->string) {
		*old_err = 2;
		return 0;
	}

	*old_err = dup(2);
	if (*old_err == -1)
		return -1;

	if (out && !my_strcmp(err->string, out->string)) {
		if (dup2(1, 2) == -1)
			return -1;
	} else {
		int err_fd;
		if (flags & IO_ERR_APPEND)
			err_fd = open(err->string, O_WRONLY | O_CREAT | O_APPEND, 0644);
		else
			err_fd = open(err->string, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (dup2(err_fd, 2) == -1)
				return -1;
		close(err_fd);
	}

	return 0;
}

static int solve_redirections(simple_command_t *s, int *old_in, int *old_out, int *old_err) {
	if (redirect_input(s->in, old_in) == -1)
		return -1;
	if (redirect_output(s->out, s->io_flags, old_out) == -1)
		return -1;
	if (redirect_error(s->err, s->io_flags, old_err, s->out) == -1)
		return -1;
	return 0;
}

static int cancel_redirections(int old_in, int old_out, int old_err) {
	if (old_in != 0) {
		if (dup2(old_in, 0) == -1)
			return -1;
		close(old_in);
	}

	if (old_out != 1) {
		if (dup2(old_out, 1) == -1)
			return -1;
		close(old_out);
	}

	if (old_err != 2) {
		if (dup2(old_err, 2) == -1)
			return -1;
		close(old_err);
	}

	return 0;
}

static int run_external_command(simple_command_t *s) {
	char *params[1001];
	int pos = 0;
	params[pos++] = (char*) s->verb->string;
	word_t *param = s->params;
	
	while (param) {
		if (param->expand == false)
			params[pos] = (char*)param->string;
		else
			params[pos] = get_env_var(param->string);
		pos++;
		param = param->next_word;
	}
	params[pos] = NULL;

	pid_t pid = fork();
	if (pid == 0) {
		int old_in, old_out, old_err;
		if (solve_redirections(s, &old_in, &old_out, &old_err) == -1)
			return -1;
		return execvp(params[0], params);
	} else {
		int status;
		waitpid(pid, &status, 0);
		return status;
	}
}

/**
 * Internal change-directory command.
 */
static int shell_cd(word_t *dir)
{
	if (!dir || !dir->string || dir->next_word)
		return -1;

	int err = chdir(dir->string);
	if (err)
		return -1;

	return 0;
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
		my_fwrite(message, 1, my_strlen(message), 1);
	} else if (!s->verb) {
		char *message = "s->message can't be null\n";
		my_fwrite(message, 1, my_strlen(message), 1);
	}

	// Built in command.
	if (!my_strcmp(s->verb->string, "exit"))
		return shell_exit();
	else if (!my_strcmp(s->verb->string, "cd")) {
		int old_in, old_out, old_err;
		if (solve_redirections(s, &old_in, &old_out, &old_err) == -1)
			return -1;

		int status = shell_cd(s->params);
		if (cancel_redirections(old_in, old_out, old_err) == -1)
			return -1;
		return status;
	}

	//Change env variable.
	if (s->verb->next_part && !my_strcmp(s->verb->next_part->string, "=")) {
		set_env_var(s->verb);
	}

	// Command which have executable.
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
		my_fwrite(message, 1, my_strlen(message), 1);
	}

	if (c->op == OP_NONE) {
		return parse_simple(c->scmd, level, father);
	}

	switch (c->op) {
	case OP_SEQUENTIAL:
		/* Execute the commands one after the other. */
		parse_command(c->cmd1, level + 1, c);
		parse_command(c->cmd2, level + 1, c);
		return 0;

	case OP_PARALLEL:
		/* TODO: Execute the commands simultaneously. */
		break;

	case OP_CONDITIONAL_NZERO:
		/* Execute the second command only if the first one
		 * returns non zero.
		 */
		if (parse_command(c->cmd1, level + 1, c) == 0)
			return 0;
		if (parse_command(c->cmd2, level + 1, c) == 0)
			return 0;
		return -1;

	case OP_CONDITIONAL_ZERO:
		/* Execute the second command only if the first one
		 * returns zero.
		 */
		if (parse_command(c->cmd1, level + 1, c) != 0)
			return -1;
		if (parse_command(c->cmd2, level + 1, c) != 0)
			return -1;
		return 0;

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
