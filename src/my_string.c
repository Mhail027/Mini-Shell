#include <sys/types.h>

#include "string.h"

size_t my_strlen(const char *s) {
	size_t cnt = 0;
	while (s[cnt])
		cnt++;
	return cnt;
}

int my_strcmp(const char *s1, const char *s2) {
	u_int i;
	for (i = 0; s1[i]; i++)
		if (s1[i] != s2[i])
			return s1[i] - s2[i];

	return 	s1[i] - s2[i];
}