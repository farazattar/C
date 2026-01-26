#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include "iec61850_client.h"
#include "mms_value.h"
#include "hal_thread.h"

static volatile int running = 1;

/* ========================================================= */
/* Graceful shutdown                                          */
/* ========================================================= */

static void
sigint_handler(int signal)
{
    running = 0;
}

/* ========================================================= */
/* MMS value to string                                        */
/* ========================================================= */

static void
printMmsValue(MmsValue* v)
{
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

    case MMS_VISIBLE_STRING:
        printf("%s", MmsValue_toString(v));
        break;

    case MMS_UTC_TIME: {
        uint64_t t = MmsValue_getUtcTimeInMs(v);
        printf("UTC(ms): %llu", (unsigned long long) t);
        break;
    }

    default:
        printf("MMS type %d (not decoded)", MmsValue_getType(v));
        break;
    }
}

/* ========================================================= */
/* Report callback                                            */
/* ========================================================= */

static void
reportCallback(void* parameter, ClientReport report)
{
    MmsValue* values = ClientReport_getDataSetValues(report);

    if (values == NULL) {
        printf("Report received with no dataset values\n");
        return;
    }

    if (MmsValue_getType(values) != MMS_ARRAY) {
        printf("Unexpected dataset type: %d\n", MmsValue_getType(values));
        return;
    }

    int elementCount = MmsValue_getArraySize(values);

    printf("Report received (%d dataset entries)\n", elementCount);

    for (int i = 0; i < elementCount; i++) {

        MmsValue* value = MmsValue_getElement(values, i);

        printf("  [%d] ", i);
        printMmsValue(value);
        printf("\n");
    }
}

/* ========================================================= */
/* BRCB setup (libiec61850 v1.6 compliant)                    */
/* ========================================================= */

static bool
setupBrcb(
    IedConnection connection,
    const char* rcbRef,
    const char* datasetRef
)
{
    IedClientError error = IED_ERROR_OK;

    printf("Reading RCB: %s\n", rcbRef);

    ClientReportControlBlock rcb =
        IedConnection_getRCBValues(
            connection,
            &error,
            rcbRef,
            NULL
        );

    if (rcb == NULL || error != IED_ERROR_OK) {
        printf("ERROR: getRCBValues failed (%d)\n", error);
        return false;
    }

    printf("RCB found\n");
    printf("  RptID  : %s\n", ClientReportControlBlock_getRptId(rcb));
    printf("  DatSet : %s\n", ClientReportControlBlock_getDataSet(rcb));

    /* Disable first */
    ClientReportControlBlock_setRptEna(rcb, false);

    /* Assign dataset */
    ClientReportControlBlock_setDataSet(rcb, datasetRef);

    /* Trigger options */
    uint8_t trgOps = 0;
    trgOps |= (1 << 0); /* data-change */
    trgOps |= (1 << 3); /* integrity */
    ClientReportControlBlock_setTrgOps(rcb, trgOps);

    /* Optional fields */
    uint8_t optFlds = 0;
    optFlds |= (1 << 0); /* seq num */
    optFlds |= (1 << 1); /* timestamp */
    optFlds |= (1 << 2); /* reason */
    optFlds |= (1 << 4); /* data ref */
    ClientReportControlBlock_setOptFlds(rcb, optFlds);

    ClientReportControlBlock_setIntgPd(rcb, 1000);

    uint32_t mask =
        RCB_ELEMENT_RPT_ENA |
        RCB_ELEMENT_DATSET |
        RCB_ELEMENT_TRG_OPS |
        RCB_ELEMENT_OPT_FLDS |
        RCB_ELEMENT_INTG_PD;

    IedConnection_setRCBValues(
        connection,
        &error,
        rcb,
        mask,
        true
    );
	
	if (error != IED_ERROR_OK) {
        printf("ERROR: setRCBValues failed (%d)\n", error);
        ClientReportControlBlock_destroy(rcb);
        return false;
    }

    /* Enable reporting */
    ClientReportControlBlock_setRptEna(rcb, true);

    mask = RCB_ELEMENT_RPT_ENA;

    IedConnection_setRCBValues(
        connection,
        &error,
        rcb,
        mask,
        true
    );
	
	if (error != IED_ERROR_OK) {
        printf("ERROR: setRCBValues failed (%d)\n", error);
        ClientReportControlBlock_destroy(rcb);
        return false;
    }
	
    printf("RCB enabled successfully\n");

    ClientReportControlBlock_destroy(rcb);
    return true;
}

/* ========================================================= */
/* MAIN                                                       */
/* ========================================================= */

int
main(int argc, char** argv)
{
    const char* hostname = "10.10.6.100";
    int tcpPort = 102;

    const char* rcbRef = "DCSRelay/LLN0.brcbEV101";
    const char* datasetRef = "DCSRelay/LLN0.DS1";

    signal(SIGINT, sigint_handler);

    IedClientError error = IED_ERROR_OK;
    IedConnection connection = IedConnection_create();

    IedConnection_connect(connection, &error, hostname, tcpPort);

    if (error != IED_ERROR_OK) {
        printf("ERROR: Connection failed (%d)\n", error);
        IedConnection_destroy(connection);
        return 1;
    }

    printf("Connected to %s:%d\n", hostname, tcpPort);

    IedConnection_installReportHandler(
        connection,
        rcbRef,
		NULL,
        reportCallback,
        NULL
    );

    if (!setupBrcb(connection, rcbRef, datasetRef)) {
        IedConnection_destroy(connection);
        return 1;
    }

    printf("Waiting for reports (Ctrl+C to exit)...\n");

    while (running) {
        Thread_sleep(1000);
    }

    printf("Disconnecting...\n");

    IedConnection_close(connection);
    IedConnection_destroy(connection);

    return 0;
}
