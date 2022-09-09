#include <stdio.h>
int main() {
	int status;
	int status2;
	int status3;
	int status4;
	int status5;
	status = system("/etc/fsstat /dev/mod00 1>/dev/null 2>/dev/null");
	status2 = system("date 1>/dev/null 2>/dev/null");
	status3 = system("ls -al 1>/dev/null 2>/dev/null");
	status4 = system("/etc/fsstat /dev/txpproj 1>/dev/null 2>/dev/null");
	status5 = system("/etc/umount /txpproz/lza/lza/ld/ld00"); 
	printf("%d\n", status);
	printf("%d\n", status2);
	printf("%d\n", status3);
	printf("%d\n", status4);
	printf("%d\n", status5);
	return 0;
}
