/*
  rcb_logger_main.c
  Subscribe to a Buffered Report Control Block (BRCB) and log incoming reports.

  - Uses libiec61850 v1.6 client API.
  - Installs report handler for: DCSRelay/LLN0.BR.brcbEV101
  - Assumes RptEna is already true on the relay (you told me RptEna=1).
  - Logs JSON lines and CSV lines to disk (append).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "iec61850_client.h"   /* high-level client API */
#include "mms_value.h"

#define IED_IP   "10.10.6.100"
#define IED_PORT 102

/* The RCB reference you gave */
#define RCB_REFERENCE "DCSRelay/LLN0.BR.brcbEV101"

/* Output files */
#define LOG_JSON_FILE "rcb_reports.jsonl"
#define LOG_CSV_FILE  "rcb_reports.csv"

/* Simple helper to get ISO time for when we received the report (local log time) */
static void iso_now_utc(char *buf, size_t len)
{
    time_t t = time(NULL);
    struct tm tm;
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    strftime(buf, (int)len, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

/* Convert an MmsValue element into text (safe fallback if unknown) */
static void mmsValueToString(MmsValue *v, char *out, int outLen)
{
    if (!v) {
        strncpy(out, "<null>", outLen - 1);
        out[outLen-1] = '\0';
        return;
    }

    MmsType t = MmsValue_getType(v);

    switch (t) {
        case MMS_BOOLEAN:
            snprintf(out, outLen, "%s", MmsValue_getBoolean(v) ? "TRUE" : "FALSE");
            break;

        case MMS_INTEGER:
            snprintf(out, outLen, "%lld", (long long) MmsValue_toInt64(v));
            break;

        case MMS_UNSIGNED:
            /* older API uses toInt64 for unsigned too; print as unsigned-looking */
            snprintf(out, outLen, "%llu", (unsigned long long) MmsValue_toInt64(v));
            break;

        case MMS_FLOAT:
            snprintf(out, outLen, "%f", MmsValue_toFloat(v));
            break;

        case MMS_VISIBLE_STRING:
        case MMS_OCTET_STRING:
        case MMS_STRING: {
            /* Many lib versions expose MmsValue_toString(MmsValue*) that returns
               an allocated/internal string pointer; use it if available. */
            const char *s = MmsValue_toString(v);
            if (s && s[0] != '\0') {
                strncpy(out, s, outLen - 1);
                out[outLen-1] = '\0';
            } else {
                snprintf(out, outLen, "<empty>");
            }
            break;
        }

        case MMS_UTC_TIME: {
            uint64_t ms = MmsValue_getUtcTimeInMs(v);
            time_t sec = (time_t)(ms / 1000);
            struct tm tm;
#ifdef _WIN32
            gmtime_s(&tm, &sec);
#else
            gmtime_r(&sec, &tm);
#endif
            strftime(out, outLen, "%Y-%m-%dT%H:%M:%SZ", &tm);
            break;
        }

        case MMS_STRUCTURE:
        case MMS_ARRAY: {
            /* stringify members into a compact {...} representation */
            out[0] = '\0';
            strncat(out, "{", outLen-1);
            int n = MmsValue_getArraySize(v);
            for (int i = 0; i < n; ++i) {
                MmsValue *e = MmsValue_getElement(v, i);
                char tmp[256];
                mmsValueToString(e, tmp, sizeof(tmp));
                strncat(out, tmp, outLen - strlen(out) - 1);
                if (i != n-1) strncat(out, ",", outLen - strlen(out) - 1);
            }
            strncat(out, "}", outLen - strlen(out) - 1);
            break;
        }

        default:
            /* fallback: ask library to print if available */
            if (MmsValue_printToBuffer(v, out, outLen) != 0) {
                snprintf(out, outLen, "UNSUPPORTED(%d)", t);
            }
            break;
    }
}

/* Thread-safe-ish append to JSONL and CSV (the callback runs inside lib threads).
   Simpler approach: open/close on each write (safe, a bit slower). */
static void write_jsonl_record(const char *rcbRef, const char *dataRef, const char *valueStr, uint64_t rptTimestamp)
{
    FILE *f = fopen(LOG_JSON_FILE, "a");
    if (!f) return;

    /* Convert rptTimestamp (ms since epoch) to ISO8601 */
    char rptIso[64] = "<no_ts>";
    if (rptTimestamp != 0) {
        time_t sec = (time_t)(rptTimestamp / 1000);
        struct tm tm;
#ifdef _WIN32
        gmtime_s(&tm, &sec);
#else
        gmtime_r(&sec, &tm);
#endif
        strftime(rptIso, sizeof(rptIso), "%Y-%m-%dT%H:%M:%SZ", &tm);
    }

    /* local reception time */
    char recvIso[64];
    iso_now_utc(recvIso, sizeof(recvIso));

    /* escape simple quotes/newlines in valueStr (naive) */
    char valEsc[1024];
    int pos = 0;
    for (const char *p = valueStr; *p && pos < (int)sizeof(valEsc)-2; ++p) {
        char c = *p;
        if (c == '"' || c == '\\') {
            valEsc[pos++] = '\\';
            valEsc[pos++] = c;
        } else if (c == '\n' || c == '\r') {
            /* skip */
        } else {
            valEsc[pos++] = c;
        }
    }
    valEsc[pos] = '\0';

    fprintf(f, "{\"recv_time\":\"%s\",\"rcb\":\"%s\",\"data_ref\":\"%s\",\"value\":\"%s\",\"rpt_time\":\"%s\"}\n",
            recvIso, rcbRef ? rcbRef : "", dataRef ? dataRef : "", valEsc, rptIso);

    fclose(f);
}

static void write_csv_record(const char *rcbRef, const char *dataRef, const char *valueStr, uint64_t rptTimestamp)
{
    FILE *f = fopen(LOG_CSV_FILE, "a");
    if (!f) return;

    char rptIso[64] = "";
    if (rptTimestamp != 0) {
        time_t sec = (time_t)(rptTimestamp / 1000);
        struct tm tm;
#ifdef _WIN32
        gmtime_s(&tm, &sec);
#else
        gmtime_r(&sec, &tm);
#endif
        strftime(rptIso, sizeof(rptIso), "%Y-%m-%dT%H:%M:%SZ", &tm);
    }

    char recvIso[64];
    iso_now_utc(recvIso, sizeof(recvIso));

    /* CSV: recv_time,rcb,data_ref,rpt_time,value (value raw; beware commas) */
    /* Simple: wrap value in quotes, escape quotes by doubling */
    char valEsc[1024];
    int pos = 0;
    valEsc[pos++] = '"';
    for (const char *p = valueStr; *p && pos < (int)sizeof(valEsc)-3; ++p) {
        char c = *p;
        if (c == '"') {
            valEsc[pos++] = '"';
            valEsc[pos++] = '"';
        } else if (c == '\n' || c == '\r') {
            /* skip */
        } else {
            valEsc[pos++] = c;
        }
    }
    valEsc[pos++] = '"';
    valEsc[pos] = '\0';

    fprintf(f, "%s,%s,%s,%s,%s\n", recvIso, rcbRef ? rcbRef : "", dataRef ? dataRef : "", rptIso, valEsc);

    fclose(f);
}

/* Report callback invoked by libiec61850 whenever a report is received.
   Signature required by lib: void (*ReportCallbackFunction)(void *parameter, ClientReport report)
   (documented in the client API). See lib docs for details. */
static void reportCallback(void *parameter, ClientReport report)
{
    (void) parameter;

    if (!report) return;

    /* Get RCB reference name that produced this report */
    const char *rcbRef = ClientReport_getRcbReference(report);
    uint64_t rptTime = ClientReport_getTimestamp(report); /* ms since epoch, 0 if not present */

    /* Get dataset values (MmsValue array) */
    MmsValue *dataValues = ClientReport_getDataSetValues(report);
    if (!dataValues) {
        /* Some reports might carry only metadata */
        write_jsonl_record(rcbRef, "<no_data>", "<no_data>", rptTime);
        write_csv_record(rcbRef, "<no_data>", "<no_data>", rptTime);
        return;
    }

    /* For each element in the dataset write a record.
       If data references are present, use them; otherwise record numeric index. */
    int n = MmsValue_getArraySize(dataValues);
    for (int i = 0; i < n; ++i) {
        MmsValue *elem = MmsValue_getElement(dataValues, i);
        char valbuf[1024];
        mmsValueToString(elem, valbuf, sizeof(valbuf));

        char const *dataRef = NULL;
        if (ClientReport_hasDataReference(report)) {
            dataRef = ClientReport_getDataReference(report, i); /* library-managed string */
        }

        /* write both JSONL and CSV */
        write_jsonl_record(rcbRef ? rcbRef : RCB_REFERENCE, dataRef ? dataRef : "<idx>", valbuf, rptTime);
        write_csv_record(rcbRef ? rcbRef : RCB_REFERENCE, dataRef ? dataRef : "<idx>", valbuf, rptTime);
    }

    /* Mark report consumed (no explicit delete required for ClientReport in this API) */
}

/* Simple helper to create csv header if file empty */
static void ensure_csv_header(void)
{
    FILE *f = fopen(LOG_CSV_FILE, "r");
    if (f) {
        fclose(f);
        return;
    }
    f = fopen(LOG_CSV_FILE, "w");
    if (!f) return;
    fprintf(f, "recv_time,rcb,data_ref,rpt_time,value\n");
    fclose(f);
}

/* Main: connect, install handler, wait */
int main(void)
{
    printf("RCB logger starting - connecting to %s:%d\n", IED_IP, IED_PORT);

    ensure_csv_header();

    IedClientError error;
    IedConnection con = IedConnection_create();

    IedConnection_connect(con, &error, IED_IP, IED_PORT);

    if (error != IED_ERROR_OK) {
        printf("Connection failed: %d\n", error);
        IedConnection_destroy(con);
        return 1;
    }

    printf("Connected. Installing report handler for RCB '%s' ...\n", RCB_REFERENCE);

    /* Install report handler. rptId can be NULL or empty; pass NULL here.
       This function registers the callback that will be called for incoming reports. */
    IedConnection_installReportHandler(con, RCB_REFERENCE, NULL, reportCallback, NULL);

    printf("Report handler installed. Waiting for reports (Ctrl-C to stop)...\n");

    /* Keep the program alive. The reportCallback executes in lib threads. Poll connection state periodically. */
    while (1) {
        IedConnectionState state = IedConnection_getState(con);
        if (state != IED_STATE_CONNECTED) {
            printf("Connection lost (state=%d). Attempting reconnect...\n", state);
            /* try reconnect loop (blocking until reconnected) */
            IedConnection_close(con);
            IedConnection_destroy(con);
            /* create fresh connection */
            con = IedConnection_create();
            int retry = 0;
            do {
                Sleep(1000);
                IedConnection_connect(con, &error, IED_IP, IED_PORT);
                retry++;
            } while (error != IED_ERROR_OK && retry < 30);
            if (error == IED_ERROR_OK)
                printf("Reconnected.\n");
            else {
                printf("Reconnect failed after retries. Exiting.\n");
                break;
            }
            /* re-install handler after reconnection */
            IedConnection_installReportHandler(con, RCB_REFERENCE, NULL, reportCallback, NULL);
        }
#ifdef _WIN32
        Sleep(500);
#else
        usleep(500 * 1000);
#endif
    }

    /* Cleanup (never reached for normal operation) */
    IedConnection_uninstallReportHandler(con, RCB_REFERENCE);
    IedConnection_close(con);
    IedConnection_destroy(con);

    return 0;
}
