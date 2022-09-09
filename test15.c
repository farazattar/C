#include<stdio.h>
#define MAX_LIMIT 200 
int main() {
	char id[MAX_LIMIT] = "id -un";
	char result[MAX_LIMIT];
	FILE *process;
	system(id);
	process = popen(id, "r");
	if(process == NULL) {
		printf("Some error occured\n");
		return 0;
	}
	while(fgets(result, MAX_LIMIT, process) != NULL)
		printf("%s", result); 
	pclose(process);
}
