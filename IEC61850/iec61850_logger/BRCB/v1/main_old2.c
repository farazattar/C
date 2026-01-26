/*
  rcb_autosub_main.c

  Robust RCB subscription for libiec61850 v1.6.

  - Tries several candidate RCB reference formats and selects the one the server accepts.
  - Reads the found ClientReportControlBlock (RCB).
  - Sets PurgeBuf = true, RptEna = true, GI = true and writes the RCB values back.
  - Installs a report handler (IedConnection_installReportHandler).
  - Logs incoming reports to JSONL and CSV, including the relay's report timestamp.
  - Reconnect logic: if connection lost, re-create and re-subscribe.
*/

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

/* Connection info */
#define IED_IP   "10.10.6.100"
#define IED_PORT 102

/* Logging outputs */
#define LOG_JSON_FILE "rcb_reports.jsonl"
#define LOG_CSV_FILE  "rcb_reports.csv"

/* Poll period for connection monitoring (ms) */
#define HEARTBEAT_MS 500

/* Candidate RCB refs to try (ordered) */
static const char* rcbCandidates[] = {
    /* common dot notation */
    "DCSRelay/LLN0.BR.brcbEV101",
    "DCSRelay/LLN0.BR.brcbEV101", /* duplicate intentionally safe */
    /* Siemens-style shown with $ separators */
    "DCSRelay/LLN0$BR$brcbEV101",
    /* Schneider variant with S in LN name as sometimes shown */
    "DCSRelay/LLN0SBR$brcbEV101",
    /* simpler variants */
    "DCSRelay/LLN0.brcbEV101",
    "DCSRelay/LLN0$BR.brcbEV101",
    "DCSRelay/LLN0$BR$brcbEV101",
    NULL
};

/* ----------------- Utilities ----------------- */

static void iso_utc_now(char *buf, size_t len)
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

/* Convert a single MmsValue to textual representation (safe fallback) */
static void mmsValueToText(MmsValue *v, char *out, int outLen)
{
    if (!v) {
        snprintf(out, outLen, "<null>");
        return;
    }

    MmsType t = MmsValue_getType(v);

    switch (t) {
        case MMS_BOOLEAN:
            snprintf(out, outLen, "%s", MmsValue_getBoolean(v) ? "TRUE" : "FALSE");
            break;

        case MMS_INTEGER:
        case MMS_UNSIGNED:
            /* lib v1.6: use toInt64 for integer values */
            snprintf(out, outLen, "%lld", (long long)MmsValue_toInt64(v));
            break;

        case MMS_FLOAT:
            snprintf(out, outLen, "%f", MmsValue_toFloat(v));
            break;

        case MMS_VISIBLE_STRING:
        case MMS_STRING:
        case MMS_OCTET_STRING: {
            const char *s = MmsValue_toString(v); /* returns const char* in headers */
            if (s && s[0] != '\0') {
                strncpy(out, s, outLen - 1);
                out[outLen - 1] = '\0';
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
            /* compact representation */
            out[0] = '\0';
            strncat(out, "{", outLen - strlen(out) - 1);
            int n = MmsValue_getArraySize(v);
            for (int i = 0; i < n; ++i) {
                MmsValue *e = MmsValue_getElement(v, i);
                char tmp[256];
                mmsValueToText(e, tmp, sizeof(tmp));
                strncat(out, tmp, outLen - strlen(out) - 1);
                if (i != n - 1) strncat(out, ",", outLen - strlen(out) - 1);
            }
            strncat(out, "}", outLen - strlen(out) - 1);
            break;
        }

        default:
            /* Try library print helper, otherwise a label */
            if (MmsValue_printToBuffer(v, out, outLen) != 0) {
                snprintf(out, outLen, "UNSUPPORTED_TYPE_%d", (int)t);
            }
            break;
    }
}

/* Safe append JSON + CSV (open/close each write for thread-safety / simplicity) */
static void write_json_record(const char *rcbRef, const char *dataRef, const char *val, uint64_t rptMs)
{
    FILE *f = fopen(LOG_JSON_FILE, "a");
    if (!f) return;

    char recvIso[64];
    iso_utc_now(recvIso, sizeof(recvIso));

    char rptIso[64] = "<no_ts>";
    if (rptMs) {
        time_t sec = (time_t)(rptMs / 1000);
        struct tm tm;
#ifdef _WIN32
        gmtime_s(&tm, &sec);
#else
        gmtime_r(&sec, &tm);
#endif
        strftime(rptIso, sizeof(rptIso), "%Y-%m-%dT%H:%M:%SZ", &tm);
    }

    /* escape val simple */
    char valEsc[1024]; valEsc[0]=0; int p=0;
    for (const char *s=val; *s && p<(int)sizeof(valEsc)-2; s++) {
        char c = *s;
        if (c == '"' || c == '\\') {
            valEsc[p++]='\\'; valEsc[p++]=c;
        } else if (c == '\n' || c == '\r') {
            /* skip */
        } else {
            valEsc[p++]=c;
        }
    }
    valEsc[p]=0;

    fprintf(f, "{\"recv_time\":\"%s\",\"rcb\":\"%s\",\"data_ref\":\"%s\",\"rpt_time\":\"%s\",\"value\":\"%s\"}\n",
            recvIso, rcbRef?rcbRef:"", dataRef?dataRef:"", rptIso, valEsc);
    fclose(f);
}

static void write_csv_record(const char *rcbRef, const char *dataRef, const char *val, uint64_t rptMs)
{
    FILE *f = fopen(LOG_CSV_FILE, "a");
    if (!f) return;

    char recvIso[64]; iso_utc_now(recvIso, sizeof(recvIso));
    char rptIso[64] = "";
    if (rptMs) {
        time_t sec = (time_t)(rptMs / 1000);
        struct tm tm;
#ifdef _WIN32
        gmtime_s(&tm, &sec);
#else
        gmtime_r(&sec, &tm);
#endif
        strftime(rptIso, sizeof(rptIso), "%Y-%m-%dT%H:%M:%SZ", &tm);
    }

    /* wrap value in quotes and double any quotes */
    char valEsc[1024]; int pos=0;
    valEsc[pos++]='"';
    for (const char *s=val; *s && pos<(int)sizeof(valEsc)-3; s++) {
        if (*s == '"') { valEsc[pos++]='"'; valEsc[pos++]='"'; }
        else if (*s == '\n' || *s == '\r') { /* skip */ }
        else valEsc[pos++]=*s;
    }
    valEsc[pos++]='"';
    valEsc[pos]=0;

    fprintf(f, "%s,%s,%s,%s,%s\n", recvIso, rcbRef?rcbRef:"", dataRef?dataRef:"", rptIso, valEsc);
    fclose(f);
}

/* ----------------- Report callback -----------------
   Called by libiec61850 when a report arrives.
   Signature: void (*ReportCallbackFunction)(void *parameter, ClientReport report)
*/
static void reportHandler(void *parameter, ClientReport report)
{
    (void)parameter;

    if (!report) return;

    const char *rcbRef = ClientReport_getRcbReference(report);
    uint64_t rptTimeMs = ClientReport_getTimestamp(report); /* ms since epoch or 0 */

    MmsValue *values = ClientReport_getDataSetValues(report);
    if (!values) {
        /* no values */
        write_json_record(rcbRef, "<no_data>", "<no_data>", rptTimeMs);
        write_csv_record(rcbRef, "<no_data>", "<no_data>", rptTimeMs);
        return;
    }

    int n = MmsValue_getArraySize(values);

    for (int i = 0; i < n; ++i) {
        MmsValue *elem = MmsValue_getElement(values, i);
        char text[1024]; mmsValueToText(elem, text, sizeof(text));

        const char *dataRef = NULL;
        if (ClientReport_hasDataReference(report)) {
            dataRef = ClientReport_getDataReference(report, i); /* may be NULL */
        }

        write_json_record(rcbRef?rcbRef:"", dataRef?dataRef:"<idx>", text, rptTimeMs);
        write_csv_record(rcbRef?rcbRef:"", dataRef?dataRef:"<idx>", text, rptTimeMs);
        printf("REPORT: %s | %s = %s\n", rcbRef?rcbRef:"", dataRef?dataRef:"<idx>", text);
    }
}

/* ----------------- Find and prepare RCB ----------------- */

static ClientReportControlBlock findRCB(IedConnection con, IedClientError *err, char *outRef, size_t outRefLen)
{
    ClientReportControlBlock rcb = NULL;
    for (const char **p = rcbCandidates; *p != NULL; ++p) {
        const char *candidate = *p;
        rcb = IedConnection_getRCBValues(con, err, candidate, NULL);
        if (rcb != NULL && *err == IED_ERROR_OK) {
            /* success */
            strncpy(outRef, candidate, outRefLen - 1);
            outRef[outRefLen - 1] = '\0';
            return rcb;
        } else {
            /* clear any error and try next */
            /* IedConnection_getRCBValues returns NULL on error or not found */
            rcb = NULL;
        }
    }
    return NULL;
}

/* ----------------- Main ----------------- */

int main(void)
{
    printf("RCB auto-subscribe starting (will try multiple path variants)...\n");

    /* ensure CSV header */
    FILE *fcsv = fopen(LOG_CSV_FILE, "r");
    if (!fcsv) {
        fcsv = fopen(LOG_CSV_FILE, "w");
        if (fcsv) {
            fprintf(fcsv, "recv_time,rcb,data_ref,rpt_time,value\n");
            fclose(fcsv);
        }
    } else fclose(fcsv);

    IedClientError err;
    IedConnection con = IedConnection_create();

retry_connect:
    IedConnection_connect(con, &err, IED_IP, IED_PORT);
    if (err != IED_ERROR_OK) {
        printf("Connect failed: %d. Retrying in 2s...\n", err);
        IedConnection_destroy(con);
#ifdef _WIN32
        Sleep(2000);
#else
        usleep(2000 * 1000);
#endif
        con = IedConnection_create();
        goto retry_connect;
    }

    printf("Connected to %s:%d\n", IED_IP, IED_PORT);

    /* Attempt to find RCB (try candidates) */
    char foundRef[256] = {0};
    ClientReportControlBlock rcb = findRCB(con, &err, foundRef, sizeof(foundRef));
    if (!rcb) {
        printf("Could not locate RCB with any candidate reference. Last error=%d\n", err);
        IedConnection_close(con);
        IedConnection_destroy(con);
        return 1;
    }

    printf("Found RCB: %s\n", foundRef);

    /* Print owner if present */
    MmsValue *owner = ClientReportControlBlock_getOwner(rcb);
    if (owner) {
        char ownbuf[256]; mmsValueToText(owner, ownbuf, sizeof(ownbuf));
        printf("RCB owner   : %s\n", ownbuf);
    } else {
        printf("RCB owner   : <none>\n");
    }

    /* Set PurgeBuf = true first to ensure a clean start (only relevant for BRCB) */
    ClientReportControlBlock_setPurgeBuf(rcb, true);
    /* Set RptEna = true so that relay will send reports */
    ClientReportControlBlock_setRptEna(rcb, true);
    /* Set GI = true to request general interrogation */
    ClientReportControlBlock_setGI(rcb, true);

    /* Prepare mask: include purge, rptEna and gi fields */
    uint32_t mask = RCB_ELEMENT_PURGE_BUF | RCB_ELEMENT_RPT_ENA | RCB_ELEMENT_GI;

    /* Write changes to server */
    IedConnection_setRCBValues(con, &err, rcb, mask, true);
    if (err != IED_ERROR_OK) {
        printf("IedConnection_setRCBValues failed: %d\n", err);
        /* maybe permission/owner problem */
        /* cleanup and exit */
        ClientReportControlBlock_destroy(rcb);
        IedConnection_close(con);
        IedConnection_destroy(con);
        return 2;
    }

    printf("RCB updated on server (PurgeBuf,RptEna,GI set). Installing report handler...\n");

    /* Install report handler using the same reference we found */
    IedConnection_installReportHandler(con, foundRef, NULL, reportHandler, NULL);

    printf("Report handler installed for '%s' - waiting for reports...\n", foundRef);

    /* Keep process alive and monitor connection state */
    while (1) {
        IedConnectionState state = IedConnection_getState(con);
        if (state != IED_STATE_CONNECTED) {
            printf("Connection lost (state=%d). Attempting reconnect...\n", state);
            /* cleanup and re-create connection and rcb */
            IedConnection_uninstallReportHandler(con, foundRef);
            IedConnection_close(con);
            IedConnection_destroy(con);
#ifdef _WIN32
            Sleep(1000);
#else
            usleep(1000 * 1000);
#endif
            con = IedConnection_create();
            goto retry_connect;
        }
#ifdef _WIN32
        Sleep(HEARTBEAT_MS);
#else
        usleep(HEARTBEAT_MS * 1000);
#endif
    }

    /* unreachable, but tidy */
    IedConnection_uninstallReportHandler(con, foundRef);
    ClientReportControlBlock_destroy(rcb);
    IedConnection_close(con);
    IedConnection_destroy(con);
    return 0;
}
