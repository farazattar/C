#include<stdio.h>
#define MAX_LIMIT 200 
int main() {
	char id[MAX_LIMIT] = "id -un";
	char result[MAX_LIMIT];
	char id_root[MAX_LIMIT] = "root";
	FILE *process;
	system(id);
	process = popen(id, "r");
	if(process == NULL) {
		printf("Some error occured\n");
		return 0;
	}
	while(fgets(result, MAX_LIMIT, process) != NULL) {
		printf("%s\n", result);
		sscanf(result, "%s", result);
		printf("%d\n", strcmp(result, id_root));
		if(strcmp(result, "root") != 0) {
			printf("You are not root.\n");
		} else {
			printf("You are root.\n");
		}
	} 
	pclose(process);
	return 0;
}
