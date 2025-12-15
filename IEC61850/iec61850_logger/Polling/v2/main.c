#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "iec61850_client.h"
#include "mms_value.h"

#define RELAY_IP   "10.10.6.100"
#define RELAY_PORT 102

#define TARGETS_FILE "targets.txt"
#define LOG_FILE     "mms_log.jsonl"   /* JSON Lines: one JSON object per line */
#define POLL_INTERVAL_MS 1000         /* adjust polling frequency */

static void log_json(const char* tag, const char* value)
{
    FILE* f = fopen(LOG_FILE, "a");
    if (!f) return;

    time_t now = time(NULL);
    /* write a single-line JSON object; keep it simple for downstream parsing */
    fprintf(f,
        "{\"time\": %lld, \"tag\": \"%s\", \"value\": \"%s\"}\n",
        (long long)now, tag, value ? value : ""
    );

    fclose(f);
}

/* Trim helper */
static char* trim(char* s) {
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (*s == 0) return s;
    char* end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) { *end = 0; end--; }
    return s;
}

typedef struct {
    char ref[256];
    char fc[8];
} Target;

static Target* load_targets(const char* filename, int* outCount)
{
    FILE* f = fopen(filename, "r");
    if (!f) {
        *outCount = 0;
        return NULL;
    }

    Target* arr = NULL;
    int capacity = 0;
    int count = 0;
    char line[512];

    while (fgets(line, sizeof(line), f)) {
        char* p = trim(line);
        if (!p || p[0] == '#' || p[0] == '\0') continue;

        char ref[256] = {0}, fc[8] = {0};
        if (sscanf(p, "%255s %7s", ref, fc) >= 1) {
            if (count >= capacity) {
                capacity = capacity ? capacity * 2 : 16;
                arr = (Target*)realloc(arr, capacity * sizeof(Target));
            }
            strncpy(arr[count].ref, ref, sizeof(arr[count].ref)-1);
            if (strlen(fc) > 0)
                strncpy(arr[count].fc, fc, sizeof(arr[count].fc)-1);
            else
                strcpy(arr[count].fc, "ST"); /* default */
            count++;
        }
    }

    fclose(f);
    *outCount = count;
    return arr;
}

static void mms_value_to_string(MmsValue* v, char* buf, int bufLen)
{
    if (!v) {
        strncpy(buf, "NULL", bufLen-1);
        return;
    }
    /* use library helper if available; fallback minimal formatting */
    if (MmsValue_printToBuffer(v, buf, bufLen) != 0) {
        /* fallback */
        strncpy(buf, "UNPRINTABLE", bufLen-1);
    }
}

int main(void)
{
    int targetCount = 0;
    Target* targets = load_targets(TARGETS_FILE, &targetCount);
    if (!targets || targetCount == 0) {
        printf("No targets loaded from %s. Please edit the file.\n", TARGETS_FILE);
        return 1;
    }

    printf("Loaded %d targets\n", targetCount);
    for (int i=0;i<targetCount;i++) {
        printf("  %s (%s)\n", targets[i].ref, targets[i].fc);
    }

    /* create connection */
    IedClientError err;
    IedConnection con = IedConnection_create();
    IedConnection_connect(con, &err, RELAY_IP, RELAY_PORT);

    if (err != IED_ERROR_OK) {
        printf("Connection failed: %d\n", err);
        free(targets);
        IedConnection_destroy(con);
        return 2;
    }

    printf("Connected to %s:%d\n", RELAY_IP, RELAY_PORT);
    log_json("connection", "OK");

    while (1) {
        for (int i=0;i<targetCount;i++) {
            const char* ref = targets[i].ref;

            /* read object - choose Functional Constraint constant if library has named constants:
               we will map simple textual FCs to lib constants - adapt to your lib version if needed */
            int fc = IEC61850_FC_ST; /* default */
            if (strcasecmp(targets[i].fc, "MX") == 0) fc = IEC61850_FC_MX;
            else if (strcasecmp(targets[i].fc, "SP") == 0) fc = IEC61850_FC_ST; /* SP often handled as ST */
            else if (strcasecmp(targets[i].fc, "CF") == 0) fc = IEC61850_FC_CF;

            MmsValue* value = IedConnection_readObject(
				con, 
				&err, 
				ref, 
				fc
			);
            if (err != IED_ERROR_OK || value == NULL) {
				printf("Read failed: %d\n", err);
				return 0;
			}
			
			MmsType type = MmsValue_getType(value);
				
			if (type == MMS_STRUCTURE) {
				int elementCount = MmsValue_getArraySize(value);
				printf("STRUCT with %d elements\n", elementCount);
				
				for (int i = 0; i < elementCount; i++) {
					MmsValue* element = MmsValue_getElement(value, i);
					MmsType etype = MmsValue_getType(element);
					if (etype == MMS_BOOLEAN) {
						bool b = MmsValue_getBoolean(element);
						printf("%s = %s\n", ref, b ? "TRUE" : "FALSE");
					}
					else if (etype == MMS_INTEGER) {
						int i = MmsValue_toInt32(element);
						printf("%s = %d\n", ref, i);
					}
					else if (etype == MMS_FLOAT) {
						float f = MmsValue_toFloat(element);
						printf("%s = %f\n", ref, f);
					}
					else if (etype == MMS_UNSIGNED) {
						unsigned u = MmsValue_toUint32(element);
						printf("%s = %u\n", ref, u);
					}
					else if (etype == MMS_UTC_TIME) {
						uint64_t ts = MmsValue_getUtcTimeInMs(element);
						printf("%s = %llu\n", ref, ts);
					}						
					else {
						printf("%s = [UNSUPPORTED TYPE %d]\n", ref, etype);
					}
				}	
			}
			else if (type == MMS_BOOLEAN) {
				bool b = MmsValue_getBoolean(value);
				printf("%s = %s\n", ref, b ? "TRUE" : "FALSE");
			}
			else {
				printf("Unexcepted MMS type: %d\n", type);
			}
			MmsValue_delete(value);
        }

        /* sleep */
        Sleep(POLL_INTERVAL_MS);
    }

    /* cleanup (never reached in this simple example) */
    log_json("connection", "CLOSED");
    IedConnection_close(con);
    IedConnection_destroy(con);
    free(targets);
    return 0;
}
