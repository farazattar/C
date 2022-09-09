#include <stdio.h>
#include <stdlib.h>
void main() {
	int a;
	int b;
	int c;
	printf("Please enter your first number:\n");
	scanf("%d", &a);
	printf("Please enter your second number:\n");
	scanf("%d", &b);
	printf("Your first number is %d.\n", a);
	printf("Your second number is %d.\n", b);
 	c = a + b;
	printf("The sum is %d.\n", c);
}	
