#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

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


/* ============================
   Utility: print MMS value
   ============================ */

static void
printMmsValue(MmsValue* v)
{
    if (v == NULL) {
        printf("(null)\n");
        return;
    }

    switch (MmsValue_getType(v)) {

    case MMS_BOOLEAN:
        printf(MmsValue_getBoolean(v) ? "TRUE" : "FALSE");
        break;

    case MMS_INTEGER:
        printf("%lld", (long long) MmsValue_toInt64(v));
        break;

    case MMS_UNSIGNED:
        printf("%llu", (unsigned long long) MmsValue_toUint32(v));
        break;

    case MMS_FLOAT:
        printf("%f", MmsValue_toFloat(v));
        break;

    case MMS_STRING:
	case MMS_OCTET_STRING:
    case MMS_VISIBLE_STRING:
        printf("%s", MmsValue_toString(v));
        break;
	

    case MMS_UTC_TIME: {
        uint64_t t = MmsValue_getUtcTimeInMs(v);
		printf("UTC(ms): %llu", (unsigned long long) t);
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
        printf("MMS type %d (not decoded)", MmsValue_getType(v));
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

    for (int i = 0; i < count; i++) {
        MmsValue* val = MmsValue_getElement(values, i);
        printf("  [%d] ", i);
        printMmsValue(val);
        printf("\n");
    }

    printf("----\n");
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

    /* ---- Enable reporting ---- */
    ClientReportControlBlock_setRptEna(rcb, true);

    /* ---- Write RCB values (v1.6 signature!) ---- */
    IedConnection_setRCBValues(
        con,
        &error,
        rcb,
        RCB_ELEMENT_RPT_ENA,
        true   /* singleRequest */
    );

    if (error != IED_ERROR_OK) {
        printf("Failed to enable reporting: %d\n", error);
        ClientReportControlBlock_destroy(rcb);
        IedConnection_destroy(con);
        return -1;
    }

    printf("Reporting enabled\n");

    /* ---- Install report handler ---- */
    IedConnection_installReportHandler(
        con,
        rcbRef,
		"BRCB1",
        reportCallback,
        NULL
    );
    printf("Report handler installed. Waiting for reports (Ctrl-C to stop)...\n");

    /* ---- Run forever ---- */
    while (1)
        Sleep(1000);

    /* ---- Cleanup (never reached) ---- */
    ClientReportControlBlock_destroy(rcb);
    IedConnection_destroy(con);
    return 0;
}
