#include<stdio.h>
#define MAX_LIMIT 200 
int main() {
	char ls[MAX_LIMIT] = "ls -al";
	char result[MAX_LIMIT];
	char test[MAX_LIMIT] = "Test";
	FILE *process;
	process = popen(ls, "r");
	if(process == NULL) {
		printf("Some error occured\n");
		return 0;
	}
	while(fgets(result, MAX_LIMIT, process) != NULL) {
		strcat(result, test);
		printf("%s", result);
	} 
	pclose(process);
}
