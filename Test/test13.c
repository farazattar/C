#include <stdio.h>
#define MAX_LIMIT 10
#define BUF_LIMIT 20
int main() {
	char str_a[MAX_LIMIT] = "file";
	char str_b[MAX_LIMIT] = "125";
	char buf[MAX_LIMIT];
	strcat(str_a, str_b);
	snprintf(buf,BUF_LIMIT, "touch %s", str_a); 
	system(buf);
	return 0;
} 
