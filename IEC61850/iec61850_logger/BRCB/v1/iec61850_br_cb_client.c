/*
 * iec61850_br_cb_client.c
 *
 * Example IEC61850 client using libiec61850 v1.6
 * - Connects to an IED (Schneider P3U30)
 * - Subscribes to a Buffered Report Control Block (BRCB)
 * - Decodes and prints received report data
 *
 * Compile (example):
 *   gcc -o iec61850_br_cb_client iec61850_br_cb_client.c -liec61850 -lpthread
 *
 * Notes:
 * - Adjust `rcbReference` (and optionally dataset name) to match the P3U30 ICD/SCL.
 * - Typical MMS port = 102.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>         // sleep()
#include <signal.h>

#include "iec61850_client.h" // libiec61850 main client header
#include "mms_values.h"

static volatile bool running = true;

static void sigint_handler(int sig)
{
    (void)sig;
    running = false;
}

/* Helper to print an MmsValue (dataset element) in a readable form */
static void printMmsValue(MmsValue* v)
{
    if (v == NULL) {
        printf("(null)\n");
        return;
    }

    MmsValueType type = MmsValue_getType(v);

    switch (type) {
    case MMS_VISIBLE_STRING:
    {
        const char* s = MmsValue_getVisibleString(v);
        printf("VISIBLE_STRING: '%s'\n", s ? s : "");
        break;
    }
    case MMS_BOOLEAN:
    {
        bool b = MmsValue_toBoolean(v);
        printf("BOOLEAN: %s\n", b ? "TRUE" : "FALSE");
        break;
    }
    case MMS_INTEGER:
    {
        int32_t i = MmsValue_toInt32(v);
        printf("INTEGER: %d\n", (int)i);
        break;
    }
    case MMS_UNSIGNED:
    {
        uint32_t u = MmsValue_toUint32(v);
        printf("UNSIGNED: %u\n", (unsigned)u);
        break;
    }
    case MMS_FLOAT32:
    {
        float f = MmsValue_toFloat(v);
        printf("FLOAT32: %f\n", f);
        break;
    }
    case MMS_FLOAT64:
    {
        double d = MmsValue_toDouble(v);
        printf("FLOAT64: %f\n", d);
        break;
    }
    case MMS_STRUCTURE:
    {
        int size = MmsValue_getElementCount(v);
        printf("STRUCTURE (elements=%d) {\n", size);
        for (int i = 0; i < size; i++) {
            MmsValue* e = MmsValue_getElement(v, i);
            printf("  [%d] ", i);
            printMmsValue(e);
        }
        printf("}\n");
        break;
    }
    case MMS_ARRAY:
    {
        int size = MmsValue_getArraySize(v);
        printf("ARRAY (elements=%d) [\n", size);
        for (int i = 0; i < size; i++) {
            MmsValue* e = MmsValue_getElement(v, i);
            printf("  [%d] ", i);
            printMmsValue(e);
        }
        printf("]\n");
        break;
    }
    default:
    {
        /* fallback to MmsValue_print which gives a textual representation */
        char* s = MmsValue_print(v);
        if (s) {
            printf("%s\n", s);
            free(s);
        } else {
            printf("Unknown MMS type %d\n", type);
        }
        break;
    }
    }
}

/*
 * Report callback invoked by libIEC61850 when a report arrives.
 * parameter: user parameter given at install time (NULL here)
 * report: ClientReport object representing the received report
 */
static void reportHandler(void* parameter, ClientReport report)
{
    (void)parameter;

    if (report == NULL) {
        printf("[reportHandler] Null report\n");
        return;
    }

    const char* rcbRef = ClientReport_getRcbReference(report);
    const char* datasetName = ClientReport_getDataSetName(report);

    printf("=== Report received ===\n");
    if (rcbRef) printf("RCB reference: %s\n", rcbRef);
    if (datasetName) printf("Dataset name: %s\n", datasetName);

    /* timestamp (if present) */
    if (ClientReport_hasTimestamp(report)) {
        /* timestamp is returned in milliseconds since epoch */
        long long tsMs = ClientReport_getTimestamp(report);
        time_t seconds = (time_t)(tsMs / 1000);
        struct tm tm_val;
        localtime_r(&seconds, &tm_val);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_val);
        printf("Timestamp: %s.%03lld\n", buf, tsMs % 1000);
    }

    /* DataSet values are returned as an MmsValue array (where each element corresponds
       to the DataSet element order configured in the ICD/SCL on the server). */
    MmsValue* values = ClientReport_getDataSetValues(report);
    if (values == NULL) {
        printf("No dataset values in report\n");
        return;
    }

    int elements = MmsValue_getArraySize(values);
    printf("DataSet element count: %d\n", elements);

    for (int i = 0; i < elements; i++) {
        MmsValue* val = MmsValue_getElement(values, i);
        printf("Element[%d]: ", i);
        printMmsValue(val);
    }

    printf("=======================\n\n");
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        printf("Usage: %s <hostname-or-ip> <rcbReference> [port]\n", argv[0]);
        printf("Example: %s 192.168.1.100 \"P3U30LD/LLN0.BRCB1\" 102\n", argv[0]);
        return 1;
    }

    const char* hostname = argv[1];
    const char* rcbReference = argv[2]; /* full reference to the BRCB (from ICD/SCL). */
    int port = 102;
    if (argc >= 4) port = atoi(argv[3]);

    signal(SIGINT, sigint_handler);

    IedClientError error;
    IedConnection con = IedConnection_create();

    printf("Connecting to %s:%d...\n", hostname, port);
    IedConnection_connect(con, &error, hostname, port);

    if (error != IED_ERROR_OK) {
        fprintf(stderr, "Connect failed, error %d\n", error);
        IedConnection_destroy(con);
        return 1;
    }

    printf("Connected. Getting RCB values for '%s'...\n", rcbReference);

    /* Fetch RCB values and info from server */
    ClientReportControlBlock rcb = IedConnection_getRCBValues(con, &error, rcbReference, NULL);
    if (error != IED_ERROR_OK || rcb == NULL) {
        fprintf(stderr, "IedConnection_getRCBValues failed (error=%d)\n", error);
        IedConnection_close(con);
        IedConnection_destroy(con);
        return 1;
    }

    /* Print some RCB info (if available) */
    const char* rptId = ClientReportControlBlock_getRptId(rcb);
    printf("Got RCB, rptId=%s\n", rptId ? rptId : "(null)");

    /* Enable reporting on the RCB (RptEna = true) and set trigger options as needed.
       TRG_OPS constants: TRG_OPT_DATA_CHANGED, TRG_OPT_INTEGRITY, TRG_OPT_GI, ... */
    ClientReportControlBlock_setRptEna(rcb, true);
    ClientReportControlBlock_setTrgOps(rcb,
        TRG_OPT_DATA_CHANGED | TRG_OPT_INTEGRITY | TRG_OPT_GI);

    /* Apply RCB settings on server */
    IedConnection_setRCBValues(con, &error, rcb,
        RCB_ELEMENT_RPT_ENA | RCB_ELEMENT_TRG_OPS, true);

    if (error != IED_ERROR_OK) {
        fprintf(stderr, "IedConnection_setRCBValues failed (error=%d)\n", error);
        IedConnection_close(con);
        IedConnection_destroy(con);
        return 1;
    }

    /* Install report handler. Use rptId from the RCB or NULL if not available. */
    IedConnection_installReportHandler(con, rcbReference, rptId, reportHandler, NULL);
    printf("Report handler installed for RCB '%s' (rptId='%s'). Waiting for reports...\n",
           rcbReference, rptId ? rptId : "(null)");

    /* Main loop - keep process alive to receive reports.
       Real applications should integrate the client event loop or use their own event handling. */
    while (running) {
        sleep(1);
    }

    printf("Shutting down...\n");

    /* Cleanup: uninstalling report handler is optional before close */
    IedConnection_uninstallReportHandler(con, rcbReference);

    IedConnection_close(con);
    IedConnection_destroy(con);

    return 0;
}