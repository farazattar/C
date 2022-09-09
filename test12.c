#include <stdio.h>
#define MAX_LIMIT 10
int main(int argc, char **argv) {
	char str[MAX_LIMIT] = "mod";
	if(argc == 1) {
		printf("\tError: You didn't specify a mod number.\n");
		printf("\tPlease use this instruction:\n");
		printf("\t./test11.exe [mod]number\n");
		printf("\tFor example: ./test11.exe 850\n");
		return 0;
	}
	if(argc > 2) {
		printf("\tError: More than 1 mod number entered.\n");
		printf("\tYou could only enter 1 mod number.\n");
		return 0;
	}
	strcat(str, argv[1]);
	printf("Concatenated string is: %s\n", str);
	return 0;
} 
