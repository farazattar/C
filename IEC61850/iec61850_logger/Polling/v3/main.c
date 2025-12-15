#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "iec61850_client.h"
#include "mms_value.h"

#define RELAY_IP   "10.10.6.100"
#define RELAY_PORT 102

#define TARGET_FILE "targets.txt"
#define CSV_FILE  "mms_log.csv"
#define JSON_FILE "mms_log.jsonl"

#define POLL_MS 1000

/* ================= TIME ================= */

static void iso_time(char* buf, size_t sz)
{
    time_t now = time(NULL);
    struct tm t;
#ifdef _WIN32
    gmtime_s(&t, &now);
#else
    gmtime_r(&now, &t);
#endif
    snprintf(buf, sz, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             t.tm_year+1900, t.tm_mon+1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec);
}

/* ================= LOGGING ================= */

static void log_csv(const char* tag, const char* val)
{
    static int hdr = 0;
    FILE* f = fopen(CSV_FILE, "a");
    if (!f) return;

    if (!hdr) {
        fprintf(f, "time,tag,value\n");
        hdr = 1;
    }

    char ts[64];
    iso_time(ts, sizeof(ts));
    fprintf(f, "%s,%s,%s\n", ts, tag, val);
    fclose(f);
}

static void log_json(const char* tag, const char* val)
{
    FILE* f = fopen(JSON_FILE, "a");
    if (!f) return;

    char ts[64];
    iso_time(ts, sizeof(ts));

    fprintf(f, "{\"time\":\"%s\",\"tag\":\"%s\",\"value\":\"%s\"}\n",
            ts, tag, val);

    fclose(f);
}

/* ================= MMS DECODER ================= */

static void mms_to_text(MmsValue* v, char* buf, size_t sz)
{
    MmsType t = MmsValue_getType(v);

    switch (t) {

    case MMS_BOOLEAN:
        snprintf(buf, sz, "%s",
            MmsValue_getBoolean(v) ? "TRUE" : "FALSE");
        break;

    case MMS_INTEGER:
        snprintf(buf, sz, "%d", MmsValue_toInt32(v));
        break;

    case MMS_UNSIGNED:
        snprintf(buf, sz, "%u", MmsValue_toUint32(v));
        break;

    case MMS_FLOAT:
        snprintf(buf, sz, "%f", MmsValue_toFloat(v));
        break;

    case MMS_UTC_TIME: {
        uint64_t ms = MmsValue_getUtcTimeInMs(v);
        time_t s = (time_t)(ms / 1000);
        struct tm t;
#ifdef _WIN32
        gmtime_s(&t, &s);
#else
        gmtime_r(&s, &t);
#endif
        snprintf(buf, sz,
            "%04d-%02d-%02dT%02d:%02d:%02dZ",
            t.tm_year+1900, t.tm_mon+1, t.tm_mday,
            t.tm_hour, t.tm_min, t.tm_sec);
        break;
    }

    case MMS_VISIBLE_STRING:
    case MMS_OCTET_STRING:
    case MMS_BIT_STRING:
        MmsValue_printToBuffer(v, buf, (int)sz);
        break;

    case MMS_STRUCTURE:
    case MMS_ARRAY: {
        strcat(buf, "{");
        int n = MmsValue_getArraySize(v);
        for (int i = 0; i < n; i++) {
            char tmp[256] = {0};
            mms_to_text(
                MmsValue_getElement(v, i),
                tmp, sizeof(tmp));
            strcat(buf, tmp);
            if (i+n>1 && i!=n-1) strcat(buf,",");
        }
        strcat(buf, "}");
        break;
    }

    default:
        snprintf(buf, sz, "UNSUPPORTED_TYPE_%d", t);
        break;
    }
}

/* ================= MAIN ================= */

int main()
{
    FILE* f = fopen(TARGET_FILE, "r");
    if (!f) {
        printf("Missing %s\n", TARGET_FILE);
        return 1;
    }

    char targets[64][256];
    int count = 0;

    while (fgets(targets[count], 256, f)) {
        char* p = strchr(targets[count], '\n');
        if (p) *p = 0;
        if (strlen(targets[count]) > 3)
            count++;
    }
    fclose(f);

    printf("Loaded %d targets\n", count);

    IedClientError err;
    IedConnection con = IedConnection_create();

    IedConnection_connect(con, &err, RELAY_IP, RELAY_PORT);
    if (err != IED_ERROR_OK) {
        printf("Connect failed\n");
        return 2;
    }

    printf("Connected to %s:%d\n", RELAY_IP, RELAY_PORT);

    while (1) {

        for (int i = 0; i < count; i++) {

            MmsValue* v = IedConnection_readObject(
                con, &err, targets[i], IEC61850_FC_ST);

            if (err || !v) {
                printf("%s -> READ_ERROR\n", targets[i]);
                log_csv(targets[i], "READ_ERROR");
                log_json(targets[i], "READ_ERROR");
                continue;
            }

            char out[512] = {0};
            mms_to_text(v, out, sizeof(out));
			
            printf("%s = %s\n", targets[i], out);
            log_csv(targets[i], out);
            log_json(targets[i], out);

            MmsValue_delete(v);
        }

#ifdef _WIN32
        Sleep(POLL_MS);
#else
        usleep(POLL_MS * 1000);
#endif
    }

    return 0;
}
