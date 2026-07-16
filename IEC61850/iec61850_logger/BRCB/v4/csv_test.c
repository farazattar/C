#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#define MAX_LIMIT 1024

int main() {
    char time[MAX_LIMIT] = "2026-03-19";
    char quality[MAX_LIMIT] = "00000000";
    char value[MAX_LIMIT] = "Hi there!";
    FILE* csv = fopen("test.csv", "w");
    fprintf(csv, "time,quality,value\n");
    fprintf(csv, "%s,%s,%s\n", time, quality, value);

}