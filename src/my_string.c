#include <sys/types.h>

#include "my_string.h"

size_t my_strlen(const char *s) {
	size_t cnt = 0;
	while (s[cnt])
		cnt++;
	return cnt;
}

int my_strcmp(const char *s1, const char *s2) {
	size_t i;
	for (i = 0; s1[i]; i++)
		if (s1[i] != s2[i])
			return s1[i] - s2[i];

	return 	s1[i] - s2[i];
}

void my_strcat(char *dst, const char *src) {
	size_t i = 0;
	while (dst[i])
		i++;
	size_t j = 0;
	while (src[j]) {
		dst[i + j] = src[j];
		j++;
	}
	dst[i + j] = '\0';
}

void my_strcpy(char *dst, const char *src) {
	for (size_t i = 0; src[i]; ++i)
		dst[i] = src[i];
}