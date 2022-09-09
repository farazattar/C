#include <stdio.h>
#include <string.h>
#define MAX_LIMIT 10
int main() {
	char destination[MAX_LIMIT] = "Hello ";
	char source[MAX_LIMIT] = "World!";
	strcat(destination, source);
	printf("Concatenated string is: %s\n", destination);
	return 0;
}
