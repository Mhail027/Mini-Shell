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

#define READ		0
#define WRITE		1

/****
 * Verify if the given environment variable, which has the format:
 *		name=value,
 * has the given name.
 *
 * @param env_var string of type name=value
 * @param name string with the name to which we make the match
 * @return -1, if the names does not match
 *		   position from where the value starts in first string, else
 *****/
static int env_var_matches(const char *env_var, const char *name)
{
	if (!env_var || !name)
		return -1;

	size_t i = 0;

	while (env_var[i] != '=' && name[i]) {
		if (env_var[i] != name[i])
			return -1;
		i++;
	}

	if (env_var[i] != '=' || name[i])
		return -1;
	return i + 1;
}

/*****
 * Get the value of an environment variable.
 *
 * @param name environment variable's name
 * @return value of variable with the given name, if exist such a variable
 *		   "", else
 *****/
static char *get_env_value(const char *name)
{
	if (!name)
		return NULL;

	extern char **environ;

	for (size_t i = 0; environ[i]; ++i) {
		int pos = env_var_matches(environ[i], name);

		if (pos > 0)
			return environ[i] + pos;
	}
	return "";
}

/*****
 * Get the string from a word, after expands it, if it's case.
 *
 * @param word which contains the string
 * @return the string, after expansion
 *		   NULL, if something bad happened
 *****/
static char *get_string(word_t *word)
{
	if (!word)
		return NULL;

	if (word->expand == false)
		return (char *) word->string;
	return get_env_value(word->string);
}

/*****
 * @param verb a list which contains the environment variable
 * @return the environment variable as string
 *		   NULL, if something bad happened
 *****/
static char *get_env_var(word_t *verb)
{
	if (!verb)
		return NULL;

	size_t total_length = 0;
	char *env_var = NULL;

	while (verb) {
		char *string = get_string(verb);
		size_t new_total_length = total_length + my_strlen(string) + 1;
		char *new_env_var = (char *)mmap(0, new_total_length, PROT_READ | PROT_WRITE,
							MAP_PRIVATE | MAP_ANON, -1, 0);

		if (new_env_var == (char *) -1)
			return NULL;

		if (env_var) {
			my_strcpy(new_env_var, env_var);
			munmap(env_var, total_length);
		}
		my_strcat(new_env_var, string);

		env_var = new_env_var;
		total_length = new_total_length;
		verb = verb->next_part;
	}
	return env_var;
}

/*****
 * @return the number of environment variables
 *****/
static int get_env_var_number(void)
{
	extern char **environ;
	size_t i = 0;

	while (environ[i])
		i++;
	return i;
}
/*****
 * Set or add a environment variable. If the variable already exists,
 * we need just to change its value. Contrary, we need to add a new variable.
 *
 * @param verb list which contains the environment variable
 * @return 0, if the function finished successfully
 *		  -1, else
 */
static int set_env_var(word_t *verb)
{
	if (!verb)
		return -1;

	char *env_var = get_env_var(verb);
	char *name = get_string(verb);
	extern char **environ;

	for (u_int i = 0; environ[i]; ++i)
		if (env_var_matches(environ[i], name) > 0) {
			environ[i] = env_var;
			return 0;
		}


	size_t size = get_env_var_number();
	char **new_environ = (char **)mmap(0, (size + 2) * sizeof(char *), PROT_READ | PROT_WRITE,
						 MAP_PRIVATE | MAP_ANON, -1, 0);

	if (new_environ == (char **)-1)
		return -1;

	for (int i = 0; i < size; ++i)
		new_environ[i] = environ[i];
	new_environ[size++] = env_var;
	new_environ[size] = NULL;

	environ = new_environ;
	return 0;
}

/*****
 * @param word a special list which contains the complete string in pieces
 * @return the complete string as a string
 *		   NULL, if something bad happens
 *****/
static char *get_complete_string(word_t *word)
{
	if (!word)
		return NULL;

	char *string_tail = get_complete_string(word->next_part);
	size_t size = my_strlen(word->string) + my_strlen(string_tail);
	char *string = (char *)mmap(0, size * sizeof(char), PROT_READ | PROT_WRITE,
						 MAP_PRIVATE | MAP_ANON, -1, 0);

	if (string == (char *)-1)
		return NULL;

	string[0] = '\0';
	my_strcpy(string, get_string(word));
	if (string_tail) {
		my_strcat(string, string_tail);
		munmap(string_tail, my_strlen(string_tail));
	}

	return string;
}

/*****
 * Redirect the standard input to other file. (Other file will have the fd 0.)
 * The initial standard input will be saved at other fd.
 * If the name of the file is NULL, than *old_in = 0.
 *
 * @param in list which contains the name of the file that will be the new stdin
 * @param old_in (*)fd at which will be saved the old stdin
 * @return 0, if the function finished successfully
 *		  -1, else
 *****/
static int redirect_input(word_t *in, int *old_in)
{
	if (!in || !in->string) {
		*old_in = 0;
		return 0;
	}

	*old_in = dup(0);
	if (*old_in == -1)
		return -1;

	char *string = get_complete_string(in);
	int in_fd = open(string, O_RDONLY | O_CREAT, 0744);

	if (dup2(in_fd, 0) == -1)
		return -1;
	close(in_fd);

	return 0;
}

/*****
 * Redirect the standard output to other file. (Other file will have the fd 1.)
 * The initial standard output will be saved at other fd.
 * If the name of the file is NULL, than *old_out = 1.
 *
 * @param out list which contains the name of the file that will be the new stdout
 * @param flags int which determinate in what mode we open the file
 * @param old_out (*)fd at which will be saved the old stdout
 * @return 0, if the function finished successfully
 *		  -1, else
 *****/
static int redirect_output(word_t *out, int flags, int *old_out)
{
	if (!out || !out->string) {
		*old_out = 1;
		return 0;
	}

	*old_out = dup(1);
	if (*old_out == -1)
		return -1;

	char *string = get_complete_string(out);
	int out_fd;

	if (flags & IO_OUT_APPEND)
		out_fd = open(string, O_WRONLY | O_CREAT | O_APPEND, 0744);
	else
		out_fd = open(string, O_WRONLY | O_CREAT | O_TRUNC, 0744);

	if (dup2(out_fd, 1) == -1)
		return -1;
	close(out_fd);

	return 0;
}

/*****
 * Redirect the standard error to other file. (Other file will have the fd 2.)
 * The initial standard error will be saved at other fd.
 * If the name of the file is NULL, than *old_err = 2.
 * If the out and err have the same filename, than they will share the same
 * open file structure.
 *
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * Before, we must call redirect_output().
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 * @param err list which contains the name of the file that will be the new stderr
 * @param flags int which determinate in what mode we open the file
 * @param old_err (*)fd at which will be saved the old stderr
 * @param out list which contains the name of the file that will be the new stdout
 * @return 0, if the function finished successfully
 *		  -1, else
 *****/
static int redirect_error(word_t *err, int flags, int *old_err, word_t *out)
{
	if (!err || !err->string) {
		*old_err = 2;
		return 0;
	}

	*old_err = dup(2);
	if (*old_err == -1)
		return -1;

	char *err_string = get_complete_string(err);
	char *out_string = get_complete_string(out);

	if (out && !my_strcmp(err_string, out_string)) {
		if (dup2(1, 2) == -1)
			return -1;
	} else {
		int err_fd;

		if (flags & IO_ERR_APPEND)
			err_fd = open(err_string, O_WRONLY | O_CREAT | O_APPEND, 0744);
		else
			err_fd = open(err_string, O_WRONLY | O_CREAT | O_TRUNC, 0744);

		if (dup2(err_fd, 2) == -1)
			return -1;
		close(err_fd);
	}

	return 0;
}

/*****
 * Redirect stdin, stdout and stderr to other files.
 *
 * @param s command which tells the names of the new files which
 *		  will represent stdin, stdout and stderr
 * @param old_in (*)fd at which we save the old stdin
 * @param old_out (*)fd at which we save the old stdout
 * @param old_err (*)fd at which we save the old stderr
 * @return 0, if the function finished successfully
 *		  -1, else
 *****/
static int solve_redirections(simple_command_t *s, int *old_in, int *old_out, int *old_err)
{
	if (redirect_input(s->in, old_in) == -1)
		return -1;
	if (redirect_output(s->out, s->io_flags, old_out) == -1)
		return -1;
	if (redirect_error(s->err, s->io_flags, old_err, s->out) == -1)
		return -1;
	return 0;
}

/*****
 * @param old_in fd at which we saved the original stdin
 * @param old_out fd at which we saved the original stdout
 * @param old_err fd at which we saved the original stderr
 * @return 0, if the function finished successfully
 *		  -1, else
 *****/
static int cancel_redirections(int old_in, int old_out, int old_err)
{
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

/*****
 * @param head of a special list
 * @return number of words from list
 *****/
size_t get_words_number(word_t *word)
{
	if (!word)
		return 0;

	size_t cnt = 0;

	while (word) {
		cnt++;
		while (word->next_part)
			word = word->next_part;
		word = word->next_word;
	}
	return cnt;
}

/*****
 * Get the length of a string which represent a parameter.
 *
 * @param param head of the list in which is saved the string in peices
 * @return the length of the string
 *****/
static size_t get_param_size(const word_t *param)
{
	size_t size = 0;

	while (param) {
		if (param->expand == false)
			size += my_strlen(param->string);
		else
			size += my_strlen(get_env_value(param->string));
		param = param->next_part;
	}
	return size;
}

/*****
 * @param verb special list which conatains the verb of the commnad
 * @param verb special list which conatains the params of the verb
 * @return an arrays of strings which represent the parameters of a command
 *		   NULL, if something bad happened
 *****/
static char **get_params(const word_t *verb, word_t *param)
{
	if (!verb)
		return NULL;

	size_t nr_params = get_words_number(param) + 1;
	char **params = (char **)mmap(0, (nr_params + 1) * sizeof(char *), PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANON, -1, 0);
	size_t pos = 0;

	if (params == (char **) -1)
		return NULL;

	params[pos++] =  (char *) verb->string;
	while (param) {
		size_t size = get_param_size(param);

		params[pos] = (char *)mmap(0, (size + 1) * sizeof(char), PROT_READ | PROT_WRITE,
					  MAP_PRIVATE | MAP_ANON, -1, 0);
		if (params[pos] == (char *) -1)
			return NULL;
		params[pos][0] = '\0';

		while (1) {
			char *string = get_string(param);

			my_strcat(params[pos], string);

			if (param->next_part)
				param = param->next_part;
			else
				break;
		}

		pos++;
		param = param->next_word;
	}
	params[pos] = NULL;

	return params;
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
static int shell_exit(int status)
{
	_exit(status);
	return status;
}

/**
 * Run a command which has an executable.
 *
 * @param s structure which saves the details of command
 * @return 0 ==> command executed succesfully
 *		  -1 ==> something bad happend during command
 *		  -2 ==> invalid command (command not found)
 */
static int run_external_command(simple_command_t *s)
{
	char **params = get_params(s->verb, s->params);
	pid_t pid = fork();

	if (pid == 0) {
		int old_in, old_out, old_err;

		if (solve_redirections(s, &old_in, &old_out, &old_err) == -1)
			return -1;
		execvp(params[0], params);
		return shell_exit(-2);
	}

	int status;

	waitpid(pid, &status, 0);
	return status;
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	if (!s || !s->verb)
		return -1;

	// Built in command.
	if (!my_strcmp(s->verb->string, "exit") || !my_strcmp(s->verb->string, "quit")) {
		return shell_exit(0);
	} else if (!my_strcmp(s->verb->string, "cd")) {
		int old_in, old_out, old_err, status;

		if (solve_redirections(s, &old_in, &old_out, &old_err) == -1)
			return -1;

		status = shell_cd(s->params);
		if (cancel_redirections(old_in, old_out, old_err) == -1)
			return -1;

		return status;
	}

	//Change env variable.
	if (s->verb->next_part && !my_strcmp(s->verb->next_part->string, "=")) {
		set_env_var(s->verb);
		return 0;
	}

	// Command which have executable.
	return run_external_command(s);
}

/**
 * Process two commands in parallel, by creating two children.
 */
static int run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	if (!cmd1 || !cmd2)
		return -1;

	pid_t pid[2];

	pid[0] = fork();
	if (!pid[0])
		shell_exit(parse_command(cmd1, level, father));

	pid[1] = fork();
	if (!pid[1])
		shell_exit(parse_command(cmd2, level, father));

	int status;

	for (int i = 0; i < 2; ++i) {
		waitpid(pid[i], &status, 0);
		if (!WIFEXITED(status) &&  WEXITSTATUS(status))
			return -1;
	}
	return 0;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static int run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	if (!cmd1 || !cmd2)
		return -1;

	int fd[2];

	if (pipe(fd) == -1)
		return -1;

	int status = -1;
	pid_t pid = fork();

	if (pid == 0) {
		close(fd[0]);
		if (dup2(fd[1], 1) == -1)
			return -1;
		close(fd[1]);

		shell_exit(parse_command(cmd1, level, father));
	} else {
		close(fd[1]);

		int old_in = dup(0);

		if (old_in == -1)
			return -1;

		if (dup2(fd[0], 0) == -1)
			return -1;
		close(fd[0]);

		status = parse_command(cmd2, level, father);

		if (dup2(old_in, 0) == -1)
			return -1;
		close(old_in);
	}

	return status;
}

char *get_invalid_command_message(simple_command_t *s)
{
	if (!s)
		return NULL;

	size_t size = my_strlen("Execution failed for ''\n") + get_param_size(s->verb);
	char *message = (char *)mmap(0, (size + 1) * sizeof(char), PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANON, -1, 0);

	if (!message)
		return NULL;
	message[0] = '\0';
	my_strcat(message, "Execution failed for '");

	word_t *verb = s->verb;

	while (verb) {
		my_strcat(message, get_string(verb));
		verb = verb->next_part;
	}

	my_strcat(message, "'\n");
	return message;
}

/**
 * Parse and execute a command.
 *
 * When execut a simple command:
 *		status == 0 ==> command executed succesfully
 *		status == -1 (255) ==> something bad happend during command
 *		status == -2 (254) ==> invalid command (command not found)
 */
int parse_command(command_t *c, int level, command_t *father)
{
	if (!c)
		return -1;

	if (c->op == OP_NONE) {
		int status = parse_simple(c->scmd, level, father);

		/* Processes return a u_int8 number, so -2 becomes 254. */
		if (WEXITSTATUS(status) == 254) {
			char *message = get_invalid_command_message(c->scmd);

			my_fwrite(message, my_strlen(message), 1, 1);
		}

		return status;
	}

	switch (c->op) {
	case OP_SEQUENTIAL:
		/* Execute the commands one after the other. */
		parse_command(c->cmd1, level + 1, c);
		parse_command(c->cmd2, level + 1, c);
		return 0;

	case OP_PARALLEL:
		/* Execute the commands simultaneously. */
		return run_in_parallel(c->cmd1, c->cmd2, level + 1, c);

	case OP_CONDITIONAL_NZERO:
		/* Execute the second command only if the first one
		 * returns non zero.
		 */
		if (parse_command(c->cmd1, level + 1, c) == 0)
			return 0;
		return parse_command(c->cmd2, level + 1, c);

	case OP_CONDITIONAL_ZERO:
		/* Execute the second command only if the first one
		 * returns zero.
		 */
		if (parse_command(c->cmd1, level + 1, c))
			return -1;
		return parse_command(c->cmd2, level + 1, c);

	case OP_PIPE:
		/* Redirect the output of the first command to the
		 * input of the second.
		 */
		return run_on_pipe(c->cmd1, c->cmd2, level + 1, c);

	default:
		return shell_exit(-1);
	}

	return 0;
}
