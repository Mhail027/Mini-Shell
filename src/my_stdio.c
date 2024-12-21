#include <sys/types.h>
#include <unistd.h>

#include "my_stdio.h"

int my_fwrite(const void *buff, size_t size, size_t nitems,int fd) {
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