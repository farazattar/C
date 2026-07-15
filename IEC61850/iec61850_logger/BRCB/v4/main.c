#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "iec61850_client.h"
#include "mms_value.h"

#define IED_IP   "10.10.6.100"
#define IED_PORT 102

#define RCB_REFERENCE "DCSRelay/LLN0.BR.brcbEV101"
#define RPT_ID "BRCB1"


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

/* ============================
   Utility: print MMS value
   ============================
   Convert an MmsValue element into text (safe fallback if unknown) */
static void mmsValueToString(MmsValue *v, char *out, int outLen)
{
    if (!v) {
        strncpy(out, "<null>", outLen - 1);
        out[outLen-1] = '\0';
        return;
    }

    MmsType type = MmsValue_getType(v);

    switch (type) {
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
		case MMS_BIT_STRING:
			MmsValue_printToBuffer(v, out, (int)outLen);
			break;

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
                snprintf(out, outLen, "UNSUPPORTED(%d)", type);
            }
            break;
    }
}

/* ============================
   Report callback (v1.6 SAFE)
   ============================ */

static void
reportCallback(void* parameter, ClientReport report)
{
    (void) parameter;

    MmsValue* values = ClientReport_getDataSetValues(report);

    if (values == NULL) {
        printf("Report received with no dataset values\n");
        return;
    }

    if (MmsValue_getType(values) != MMS_ARRAY) {
        printf("Unexpected dataset type: %d\n", MmsValue_getType(values));
        return;
    }

    int count = MmsValue_getArraySize(values);

    printf("Report received (%d entries):\n", count);
     
    FILE* csv = fopen("BRCB-LOG.csv", "a");
    FILE* json = fopen("BRCB-LOG.json", "a");

    for (int i = 0; i < count; i++) {
        MmsValue* val = MmsValue_getElement(values, i);
        printf("  [%d] ", i);
		char valbuf[1024];
        mmsValueToString(val, valbuf, sizeof(valbuf));
        printf("%s\n", valbuf);
        fprintf(csv, "%s\n", valbuf);
        fprintf(json, "{\"value\":\"%s\"}\n", valbuf);
    }

    printf("----\n");
    
}

/* ============================
   GI (General Interrogation) Trigger
   ============================ */

static void triggerGI(IedConnection con, ClientReportControlBlock rcb)
{
    IedClientError err = IED_ERROR_OK;

    printf("Triggering GI (General Interrogation)...\n");

    /* set GI flag */
    ClientReportControlBlock_setGI(rcb, true);

    /* write only GI element */
    IedConnection_setRCBValues(
        con,
        &err,
        rcb,
        RCB_ELEMENT_GI,
        true
    );

    if (err != IED_ERROR_OK)
        printf("GI failed: %i\n", err);
    else
        printf("GI request sent successfully\n");
}

/* ============================
   Main
   ============================ */

int
main(int argc, char** argv)
{
    (void) argc;
    (void) argv;

    IedClientError error;
    IedConnection con = IedConnection_create();

    IedConnection_connect(con, &error, IED_IP, IED_PORT);

    if (error != IED_ERROR_OK) {
        printf("Connection failed: %d\n", error);
        IedConnection_destroy(con);
        return -1;
    }

    printf("Connected to server\n");
	
    FILE* csv = fopen("BRCB-LOG.csv", "w");
    FILE* json = fopen("BRCB-LOG.json", "w");

    fprintf(csv, "time,tag,value\n");

    /* ---- BRCB path (v1.6 canonical form) ---- */
    const char* rcbRef = RCB_REFERENCE;

    ClientReportControlBlock rcb =
        IedConnection_getRCBValues(con, &error, rcbRef, NULL);

    if (error != IED_ERROR_OK || rcb == NULL) {
        printf("Failed to read RCB: %d\n", error);
        IedConnection_destroy(con);
        return -1;
    }

    printf("RCB acquired\n");
    printf("\tRptID  : %s\n", ClientReportControlBlock_getRptId(rcb));
	printf("\tRptEna  : %i\n", ClientReportControlBlock_getRptEna(rcb));
	printf("\tResv  : %i\n", ClientReportControlBlock_getResv(rcb));
	printf("\tDataSet  : %s\n", ClientReportControlBlock_getDataSetReference(rcb));
	printf("\tObjectReference  : %s\n", ClientReportControlBlock_getObjectReference(rcb));
	
	/* Enable data-change and integrity reports */
	ClientReportControlBlock_setTrgOps(
		rcb,
		TRG_OPT_DATA_CHANGED |
		TRG_OPT_INTEGRITY
	);

	/* Optional fields: timestamp + reason */
	ClientReportControlBlock_setOptFlds(
		rcb,
		RPT_OPT_TIME_STAMP |
		RPT_OPT_REASON_FOR_INCLUSION
	);
	

    /* ---- Enable reporting ---- */
    ClientReportControlBlock_setRptEna(rcb, true);

    /* ---- Write RCB values (v1.6 signature!) ---- */
    IedConnection_setRCBValues(
        con,
        &error,
        rcb,
        RCB_ELEMENT_RPT_ENA |
		RCB_ELEMENT_TRG_OPS |
		RCB_ELEMENT_OPT_FLDS,
        true   /* singleRequest */
    );

    if (error != IED_ERROR_OK) {
        printf("Failed to enable reporting: %d\n", error);
        ClientReportControlBlock_destroy(rcb);
        IedConnection_destroy(con);
        return -1;
    }

    printf("Reporting enabled\n");

    /* ---- Add GI trigger ---- */
    triggerGI(con, rcb);


    /* ---- Install report handler ---- */
    IedConnection_installReportHandler(
        con,
        rcbRef,
		RPT_ID,
        reportCallback,
        NULL
    );
    printf("Report handler installed. Waiting for reports (Ctrl-C to stop)...\n");

    /* ---- Run forever ---- */
    while (1)
        Sleep(1000);

    /* ---- Cleanup (never reached) ---- */
    ClientReportControlBlock_destroy(rcb);
    
	fclose(csv);
    fclose(json);    

    IedConnection_close(con);
    IedConnection_destroy(con);
    return 0;
}
