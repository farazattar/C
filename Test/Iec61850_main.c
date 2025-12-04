#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

#include "iec61850_client.h"
#include "mms_value.h"

#define MAX_TARGETS 256
#define LINE_SIZE 512
#define POLL_INTERVAL_MS 1000

typedef struct {
    int fc;
    char path[256];
} Target;

Target targets[MAX_TARGETS];
int targetCount = 0;

/* ===================== Time ===================== */

const char* nowISO8601()
{
    static char buf[32];
    time_t t = time(NULL);
    struct tm tm;
    gmtime_s(&tm, &t);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

/* ===================== FC Parsing ===================== */

int parseFC(const char* s)
{
    if (strcmp(s, "ST") == 0) return IEC61850_FC_ST;
    if (strcmp(s, "DC") == 0) return IEC61850_FC_DC;
    if (strcmp(s, "MX") == 0) return IEC61850_FC_MX;
    if (strcmp(s, "CO") == 0) return IEC61850_FC_CO;
    if (strcmp(s, "SP") == 0) return IEC61850_FC_SP;
    if (strcmp(s, "EX") == 0) return IEC61850_FC_EX;

    return IEC61850_FC_ST;
}

/* ===================== targets.txt Parsing ===================== */

void loadTargets(const char* filename)
{
    FILE* f = fopen(filename, "r");
    if (!f) {
        printf("Cannot open targets.txt\n");
        exit(1);
    }

    char line[LINE_SIZE];

    while (fgets(line, sizeof(line), f)) {

        char* p = strchr(line, '\n');
        if (p) *p = 0;

        if (strlen(line) < 5) continue;

        char* comma = strchr(line, ',');
        if (!comma) continue;

        *comma = 0;

        char* fcStr = line;
        char* pathStr = comma + 1;

        while (*pathStr == ' ') pathStr++;

        targets[targetCount].fc = parseFC(fcStr);
        strncpy(targets[targetCount].path, pathStr,
                sizeof(targets[targetCount].path) - 1);

        targetCount++;
        if (targetCount >= MAX_TARGETS) break;
    }

    fclose(f);
    printf("Loaded %d targets\n", targetCount);
}

/* ===================== MMS => TEXT ===================== */

void mmsToText(MmsValue* v, char* out, int outLen)
{
    if (!v) {
        snprintf(out, outLen, "<null>");
        return;
    }

    switch (MmsValue_getType(v)) {

    case MMS_BOOLEAN:
        snprintf(out, outLen, "%s",
                 MmsValue_getBoolean(v) ? "TRUE" : "FALSE");
        break;

    case MMS_INTEGER:
    case MMS_UNSIGNED:
        snprintf(out, outLen, "%lld",
                 (long long) MmsValue_toInt64(v));
        break;

    case MMS_FLOAT:
        snprintf(out, outLen, "%f", MmsValue_toFloat(v));
        break;

    case MMS_VISIBLE_STRING:
    case MMS_STRING:
    case MMS_OCTET_STRING: {
        char* s = MmsValue_toString(v);
        if (s) {
            strncpy(out, s, outLen - 1);
            out[outLen - 1] = 0;
        } else {
            snprintf(out, outLen, "<empty>");
        }
        break;
    }

    case MMS_UTC_TIME: {
        uint64_t t = MmsValue_getUtcTimeInMs(v);
        time_t sec = (time_t)(t / 1000);
        struct tm tm;
        gmtime_s(&tm, &sec);
        strftime(out, outLen,
                 "%Y-%m-%dT%H:%M:%SZ", &tm);
        break;
    }

    default:
        snprintf(out, outLen, "UNSUPPORTED(%d)",
                 MmsValue_getType(v));
        break;
    }
}

/* ===================== MAIN ===================== */

int main()
{
    IedClientError err;
    IedConnection con = IedConnection_create();

    loadTargets("targets.txt");

    IedConnection_connect(con, &err, "10.10.6.100", 102);

    if (err != IED_ERROR_OK) {
        printf("Connection failed\n");
        return 1;
    }

    FILE* csv = fopen("mms-log.csv", "w");
    FILE* json = fopen("mms-log.json", "w");

    fprintf(csv, "time,tag,value\n");

    while (1) {

        for (int i = 0; i < targetCount; i++) {

            MmsValue* v = IedConnection_readObject(
                con, &err,
                targets[i].path,
                targets[i].fc
            );

            if (v && err == IED_ERROR_OK) {

                char value[512];
                mmsToText(v, value, sizeof(value));

                const char* t = nowISO8601();

                fprintf(csv, "%s,%s,%s\n",
                        t, targets[i].path, value);

                fprintf(json,
                        "{\"time\":\"%s\",\"tag\":\"%s\",\"value\":\"%s\"}\n",
                        t, targets[i].path, value);

                printf("%s | %s = %s\n",
                       t, targets[i].path, value);
            }

            if (v)
                MmsValue_delete(v);
        }

        Sleep(POLL_INTERVAL_MS);
    }

    fclose(csv);
    fclose(json);

    IedConnection_close(con);
    IedConnection_destroy(con);
    return 0;
}
