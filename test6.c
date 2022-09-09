#include <stdio.h>
#define MAX_LIMIT 10
int main() {
	char str[MAX_LIMIT];
	printf("Please enter your string (max. 10 character):\n");
	fgets(str, MAX_LIMIT, stdin);
	printf("%s", str);
	return 0;
}
