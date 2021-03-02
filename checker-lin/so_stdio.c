#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include "so_stdio.h"

int decode(const char *mode)
{
	if (strcmp(mode, "r") == 0)
		return 1;
	if (strcmp(mode, "r+") == 0)
		return 2;
	if (strcmp(mode, "w") == 0)
		return 3;
	if (strcmp(mode, "w+") == 0)
		return 4;
	if (strcmp(mode, "a") == 0)
		return 5;
	if (strcmp(mode, "a+") == 0)
		return 6;
	return 7;
}

static int child_loop(int readfd)
{
	char output[LEN_BUFFER];
	int rc;

	while (1) {
		rc = read(readfd, output, LEN_BUFFER);
		if (rc < 0)
			return -1;
		if (rc == 0) {
			close(readfd);
			break;
		}

		//printf("[Child received]: %s\n", output);
		fflush(stdout);
		printf("raman");
	}

	return 0;
}

SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	SO_FILE *f = NULL;
	int fd;
	int value;
	int f_mode;

	f = (SO_FILE *)calloc(1, sizeof(SO_FILE));
	if (!f)
		return NULL;

	value = decode(mode);

	switch (value) {
	case 1:
		f_mode = O_RDONLY;
		break;
	case 2:
		f_mode = O_RDWR;
		break;
	case 3:
		f_mode = O_CREAT | O_TRUNC | O_WRONLY;
		break;
	case 4:
		f_mode = O_CREAT | O_TRUNC | O_RDWR;
		break;
	case 5:
		f_mode = O_CREAT | O_APPEND;
		break;
	case 6:
		f_mode = O_CREAT | O_APPEND | O_RDONLY;
		break;
	case 7:
		free(f);
		return NULL;
	}

	fd = open(pathname, f_mode, 0644);
	if (fd < 0) {
		free(f);
		return NULL;
	}

	f->fd = fd;
	f->mode = f_mode;
	f->empty = 1;
	f->cursor = 0;
	f->counter = 0;
	f->error = 0;
	f->feof = 1;
	f->last_op = 0;

	return f;
}

int so_fclose(SO_FILE *stream)
{
	int result;
	int result2;
	int counter = 0;

	if (stream->empty == 0 && stream->last_op == 1) {
		result2 = write(stream->fd, stream->buffer, stream->counter);
		if (result2 < 0) {
			free(stream);
			return SO_EOF;
		}
		if (result2 != stream->counter && result2 > 0) {
			counter += result2;
			stream->counter -= result2;
			while (1) {
				result2 = write(stream->fd, stream->buffer +
					counter, stream->counter);
				if (result2 < 0) {
					free(stream);
					return SO_EOF;
				}
				stream->counter -= result2;
				counter += result2;
				if (stream->counter == 0)
					break;
			}
		}
	}

	result = close(so_fileno(stream));
	if (result < 0) {
		free(stream);
		return SO_EOF;
	}
	free(stream);

	return 0;
}

int so_fileno(SO_FILE *stream)
{
	return stream->fd;
}

int so_fflush(SO_FILE *stream)
{
	int result;
	int counter = 0;

	if (stream->last_op != 1)
		return SO_EOF;
	stream->last_op = 0;

	result = write(stream->fd, stream->buffer, stream->counter);
	if (result < 0)
		return SO_EOF;

	if (result != stream->counter && result > 0) {
		counter += result;
		stream->counter -= result;
		while (1) {
			result = write(stream->fd, stream->buffer +
				counter, stream->counter);
			stream->counter -= result;
			counter += result;
			if (stream->counter == 0)
				break;
		}
	}
	if (result < 0)
		return SO_EOF;
	return 0;
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
	int result;
	int result2;
	int counter = 0;

	if (stream->last_op == 0) {
		stream->counter = 0;
		stream->empty = 1;
	}

	if (stream->last_op == 1) {
		result2 = write(stream->fd, stream->buffer, stream->counter);

		if (result2 < 0)
			return -1;

		if (result2 != stream->counter && result2 > 0) {
			counter += result2;
			stream->counter -= result2;
			while (1) {
				result2 = write(stream->fd, stream->buffer +
					counter, stream->counter);
				stream->counter -= result2;
				counter += result2;
				if (stream->counter == 0)
					break;
			}
		}
	}
	stream->last_op = 0;

	result = lseek(so_fileno(stream), offset, whence);
	if (result < 0)
		return -1;

	stream->cursor = result;
	return 0;
}

long so_ftell(SO_FILE *stream)
{
	return stream->cursor;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int result;
	int counter = 0;
	char *c = (char *)ptr;

	for (int i = 0; i < size * nmemb; i++) {
		result = so_fgetc(stream);
		c[i] = result;
		if (result != SO_EOF)
			counter++;
	}

	stream->cursor += counter;

	return counter / size;
}

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int result;
	char *c = (char *)ptr;

	for (int i = 0; i < size * nmemb; i++)
		result = so_fputc(c[i], stream);

	stream->cursor += nmemb;

	return nmemb;
}

int so_fgetc(SO_FILE *stream)
{
	unsigned char c;
	static int result;

	if (stream->empty == 1) {

		result = read(stream->fd, stream->buffer, LEN_BUFFER);
		if (result <= 0) {
			stream->feof = 1;
			stream->error = 1;
			return SO_EOF;
		}
		stream->empty = 0;
	}

	if (stream->counter > result - 1) {

		stream->counter = 0;

		result = read(stream->fd, stream->buffer, LEN_BUFFER);
		if (result <= 0) {
			stream->feof = 1;
			return SO_EOF;
		}
	}

	c = (unsigned char)stream->buffer[stream->counter];
	stream->counter++;
	stream->last_op = 0;
	stream->feof = 0;

	return (int)c;
}

int so_fputc(int c, SO_FILE *stream)
{
	static int result;
	int counter = 0;

	stream->buffer[stream->counter] = c;
	stream->counter++;

	if (stream->counter > LEN_BUFFER - 1) {
		result = write(stream->fd, stream->buffer, stream->counter);
		if (result == 0)
			return SO_EOF;

		if (result < 0) {
			stream->error = 1;
			return SO_EOF;
		}

		if (result != stream->counter && result > 0) {
			counter += result;
			stream->counter -= result;
			while (1) {
				result = write(stream->fd, stream->buffer +
					counter, stream->counter);
				stream->counter -= result;
				counter += result;
				if (stream->counter == 0)
					break;
			}
		}

		stream->counter = 0;
		stream->empty = 1;
	} else
		stream->empty = 0;
	stream->last_op = 1;
	return c;
}

int so_feof(SO_FILE *stream)
{
	return stream->feof;
}

int so_ferror(SO_FILE *stream)
{
	return stream->error;
}

SO_FILE *so_popen(const char *command, const char *type)
{
	SO_FILE *f = NULL;
	pid_t pid, wait_ret;
	int status;
	int fd;
	int ret;
	int fds[2];
	int rc;
	int f_mode;
	char *c;
	int read_fd, write_fd;
	int result;

	c = calloc(strlen(command) + 1, sizeof(char));
	strcpy(c, command);
	char *const param[] = { "sh", "-c", c, NULL };

	rc = pipe(fds);
	if (rc != 0) {
		free(c);
		return NULL;
	}

	pid = fork();
	
	switch (pid) {
	case -1:
		close(fds[0]);
		close(fds[1]);
		return NULL;
		break;
	case 0:
		printf("0\n");
		close(fds[1]);
		//execvp("sh", param);
		result = child_loop(fds[0]);

		if (result < 0) {
			printf("eroare loop\n");
			free(c);
			return NULL;
		}

		//result = execvp("sh", param);
		/*if (result < 0) {
			printf("eroare execv\n");
			//free(ex[3]);
			return NULL;
		}*/
		printf("trec\n");
		break;
	default:
		printf("default\n");
		wait_ret = waitpid(pid, &status, 0);
		if (wait_ret < 0) {
			return NULL;
		}
		break;
	}

//	free(ex[3]);
	free(c);
	return f;
}

int so_pclose(SO_FILE *stream)
{
}


