#include <stdio.h>
int main(int argc, char **argv) {
	int counter;
	printf("The program name is: %s\n", argv[0]);
	if(argc == 1)
		printf("No arguments passed.\n");
	if(argc >= 2) {
		printf("Number of arguments passed is: %d\n", argc-1);
		printf("Following are the arguments:\n");
		for(counter=0; counter < argc; counter++)
			printf("argv[%d] : %s\n", counter, argv[counter]);
	}
	return 0;
}	    
