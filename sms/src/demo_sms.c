#include "stdbool.h"
#include "stdint.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "api_os.h"
#include "api_debug.h"
#include "api_event.h"
#include "api_sms.h"
#include "api_hal_uart.h"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <api_os.h>
#include <api_gps.h>
#include <api_event.h>
#include <api_hal_uart.h>
#include <api_debug.h>
#include "buffer.h"
#include "gps_parse.h"
#include "math.h"
#include "gps.h"
#include "api_hal_pm.h"
#include "time.h"
#include "api_info.h"
#include "assert.h"
#include "api_socket.h"
#include "api_network.h"
#include "api_hal_gpio.h"
#include "api_os.h"
#include "api_debug.h"
#include "api_event.h"
#include "api_call.h"
#include "api_audio.h"
// Configuration

#define MAIN_TASK_STACK_SIZE (2048 * 2)
#define MAIN_TASK_PRIORITY 0
#define MAIN_TASK_NAME "SMS Test Task"

static HANDLE mainTaskHandle = NULL;
static uint8_t flag = 0;

bool isGpsOn = true;
uint8_t buffer[1024], buffer2[400] = "EMPTY", buffer3[80];
void SendSMS(const char *number, const uint8_t *utf8Msg)
{
    uint8_t *unicode = NULL;
    uint32_t unicodeLen;

    Trace(1, "sms start send UTF-8 message");

    if (!SMS_LocalLanguage2Unicode(utf8Msg, strlen(utf8Msg), CHARSET_UTF_8, &unicode, &unicodeLen))
    {
        Trace(1, "local to unicode fail!");
        return;
    }
    if (!SMS_SendMessage(number, unicode, unicodeLen, SIM0))
    {
        Trace(1, "sms send message fail");
    }
    OS_Free(unicode);
}
void parsePhoneNumber(const char *input, char *phoneNumber)
{
    const char *start = strchr(input, '+');
    if (start)
    {
        const char *end = strchr(start, ',');
        if (end)
        {
            size_t length = end - start;
            if (*(end - 1) == '"')
            {
                length--;
            }

            strncpy(phoneNumber, start, length);
            phoneNumber[length] = '\0';
        }
    }
}
void ServerCenterTest()
{
    uint8_t addr[32];
    uint8_t temp;
    SMS_Server_Center_Info_t sca;
    sca.addr = addr;

    SMS_GetServerCenterInfo(&sca);
    Trace(1, "server center address: %s, type: %d", sca.addr, sca.addrType);
    temp = sca.addr[strlen(sca.addr) - 1];
    sca.addr[strlen(sca.addr) - 1] = '0';
    if (!SMS_SetServerCenterInfo(&sca))
        Trace(1, "SMS_SetServerCenterInfo fail");
    else
        Trace(1, "SMS_SetServerCenterInfo success");

    SMS_GetServerCenterInfo(&sca);
    Trace(1, "server center address: %s, type: %d", sca.addr, sca.addrType);
    sca.addr[strlen(sca.addr) - 1] = temp;
    if (!SMS_SetServerCenterInfo(&sca))
        Trace(1, "SMS_SetServerCenterInfo fail");

    else
        Trace(1, "SMS_SetServerCenterInfo success");
}

void EventDispatch(API_Event_t *pEvent)
{
    switch (pEvent->id)
    {
    case API_EVENT_ID_NO_SIMCARD:
        Trace(10, "!!NO SIM CARD%d!!!!", pEvent->param1);
        break;
    case API_EVENT_ID_GPS_UART_RECEIVED:
        GPS_Update(pEvent->pParam1, pEvent->param1);
        break;
    case API_EVENT_ID_SYSTEM_READY:
        Trace(1, "api event ready");
        flag |= 1;
        break;
    case API_EVENT_ID_NETWORK_REGISTERED_HOME:
        Trace(2, "registered home");
    case API_EVENT_ID_NETWORK_REGISTERED_ROAMING:
        Trace(2, "registered roaming");
        flag |= 2;
        break;
    case API_EVENT_ID_CALL_INCOMING: // param1: number type, pParam1:number
        Trace(1, "Receive a call, number:%s, number type:%d", pEvent->pParam1, pEvent->param1);
        OS_Sleep(5000);
        if (!CALL_Answer())
            Trace(1, "answer fail");
        break;
    case API_EVENT_ID_SMS_SENT:
        Trace(2, "Send Message Success");
        break;
    case API_EVENT_ID_SMS_RECEIVED:
        Trace(2, "received message");
        SMS_Encode_Type_t encodeType = pEvent->param1;
        uint32_t contentLength = pEvent->param2;
        uint8_t *header = pEvent->pParam1;
        uint8_t *content = pEvent->pParam2;

        Trace(2, "message header: %s", header);
        Trace(2, "message content length: %d", contentLength);
        if (encodeType == SMS_ENCODE_TYPE_ASCII)
        {
            Trace(2, "message content: %s", content);
            char phoneNumber[20];
            parsePhoneNumber(header, phoneNumber);
            if (!strcmp(content, "GPS") || !strcmp(content, "gps") || !strcmp(content, "Gps"))
                SendSMS(phoneNumber, buffer2);
        }
        else
        {
            uint8_t tmp[500];
            memset(tmp, 0, 500);
            for (int i = 0; i < contentLength; i += 2)
            {
                sprintf(tmp + strlen(tmp), "\\u%02x%02x", content[i], content[i + 1]);
            }
            Trace(2, "message content (unicode): %s", tmp);
            uint8_t *gbk = NULL;
            uint32_t gbkLen = 0;
            if (!SMS_Unicode2LocalLanguage(content, contentLength, CHARSET_CP936, &gbk, &gbkLen))
            {
                Trace(10, "convert unicode to GBK fail!");
            }
            else
            {
                memset(tmp, 0, 500);
                for (int i = 0; i < gbkLen; i += 2)
                {
                    sprintf(tmp + strlen(tmp), "%02x%02x ", gbk[i], gbk[i + 1]);
                }
                Trace(2, "message content (GBK): %s", tmp);
            }
            OS_Free(gbk);
        }
        break;
    case API_EVENT_ID_SMS_LIST_MESSAGE:
    {
        SMS_Message_Info_t *messageInfo = (SMS_Message_Info_t *)pEvent->pParam1;
        Trace(1, "message header index: %d, status: %d, number type: %d, number: %s, time: \"%u/%02u/%02u, %02u:%02u:%02u+%02d\"",
              messageInfo->index, messageInfo->status, messageInfo->phoneNumberType, messageInfo->phoneNumber,
              messageInfo->time.year, messageInfo->time.month, messageInfo->time.day,
              messageInfo->time.hour, messageInfo->time.minute, messageInfo->time.second,
              messageInfo->time.timeZone);
        Trace(1, "message content len: %d, data: %s", messageInfo->dataLen, messageInfo->data);
        OS_Free(messageInfo->data);
        break;
    }
    case API_EVENT_ID_SMS_ERROR:
        Trace(10, "SMS error occurred! cause: %d", pEvent->param1);
        break;
    case API_EVENT_ID_UART_RECEIVED:
        if (pEvent->param1 == UART1)
        {
            uint8_t data[pEvent->param2 + 1];
            data[pEvent->param2] = 0;
            memcpy(data, pEvent->pParam1, pEvent->param2);
            Trace(1, "uart received data,length:%d,data:%s", pEvent->param2, data);
            if (strcmp(data, "close") == 0)
            {
                Trace(1, "close gps");
                GPS_Close();
                isGpsOn = false;
            }
            else if (strcmp(data, "open") == 0)
            {
                Trace(1, "open gps");
                GPS_Open(NULL);
                isGpsOn = true;
            }
        }
        break;
    default:
        break;
    }
}

// No standard libraries :(
void FormatTime(long timestamp)
{
    // Days in each month for a non-leap year
    int daysInMonth[] = {31, 28, 31, 30, 31, 30,
                         31, 31, 30, 31, 30, 31};

    // Days in each month for a leap year
    int daysInMonthLeap[] = {31, 29, 31, 30, 31, 30,
                             31, 31, 30, 31, 30, 31};

    // Number of seconds in one day
    const long secondsInDay = 86400;

    // Initial values for calculations
    long totalDays = timestamp / secondsInDay;
    int seconds = timestamp % 60;
    int minutes = (timestamp / 60) % 60;
    int hours = (timestamp / 3600) % 24;

    // Calculate current year
    int year = 1970;
    while (1)
    {
        int daysInYear = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
        if (totalDays < daysInYear)
            break;
        totalDays -= daysInYear;
        year++;
    }

    // Calculate current month and day
    int month, day;
    int *daysInCurrentMonth = ((year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? daysInMonthLeap : daysInMonth);
    for (month = 0; month < 12; month++)
    {
        if (totalDays < daysInCurrentMonth[month])
            break;
        totalDays -= daysInCurrentMonth[month];
    }
    month++; // Adjust month to be 1-based

    day = totalDays + 1; // Adjust day to be 1-based

    // Print the formatted output
    sprintf(buffer3, "%02d:%02d:%02d %02d.%02d.%04d\n", hours, minutes, seconds, day, month, year);
}
void gpsTask(void *pData)
{
    GPS_Info_t *gpsInfo = Gps_GetInfo();

    UART_Write(UART1, "Init now\r\n", strlen("Init now\r\n"));
    // wait for gprs register complete
    // The process of GPRS registration network may cause the power supply voltage of GPS to drop,
    // which resulting in GPS restart.
    while (flag != 3)
    {
        Trace(1, "wait for gprs regiter complete");
        UART_Write(UART1, "wait for gprs regiter complete\r\n", strlen("wait for gprs regiter complete\r\n"));
        OS_Sleep(2000);
    }
    // open GPS hardware(UART2 open either)
    GPS_Init();
    GPS_Open(NULL);

    // wait for gps start up, or gps will not response command
    while (gpsInfo->rmc.latitude.value == 0)
        OS_Sleep(1000);

    // set gps nmea output interval
    for (uint8_t i = 0; i < 5; ++i)
    {
        bool ret = GPS_SetOutputInterval(10000);
        Trace(1, "set gps ret:%d", ret);
        if (ret)
            break;
        OS_Sleep(1000);
    }

    if (!GPS_GetVersion(buffer, 150))
        Trace(1, "get gps firmware version fail");
    else
        Trace(1, "gps firmware version:%s", buffer);

    // if(!GPS_SetFixMode(GPS_FIX_MODE_LOW_SPEED))
    // Trace(1,"set fix mode fail");

    // if (!GPS_SetLpMode(GPS_LP_MODE_SUPPER_LP))
    //     Trace(1, "set gps lp mode fail");

    if (!GPS_SetOutputInterval(1000))
        Trace(1, "set nmea output interval fail");

    Trace(1, "init ok");
    UART_Write(UART1, "Init ok\r\n", strlen("Init ok\r\n"));

    if (!SMS_DeleteMessage(5, SMS_STATUS_ALL, SMS_STORAGE_SIM_CARD))
        Trace(1, "delete sms fail");
    else
        Trace(1, "delete sms success");

    while (1)
    {
        if (isGpsOn)
        {
            // show fix info
            uint8_t isFixed = gpsInfo->gsa[0].fix_type > gpsInfo->gsa[1].fix_type ? gpsInfo->gsa[0].fix_type : gpsInfo->gsa[1].fix_type;
            char *isFixedStr;
            if (isFixed == 2)
                isFixedStr = "2D fix";
            else if (isFixed == 3)
            {
                if (gpsInfo->gga.fix_quality == 1)
                    isFixedStr = "3D fix";
                else if (gpsInfo->gga.fix_quality == 2)
                    isFixedStr = "3D/DGPS fix";
            }
            else
                isFixedStr = "no fix";

            // convert unit ddmm.mmmm to degree(°)
            int temp = (int)(gpsInfo->rmc.latitude.value / gpsInfo->rmc.latitude.scale / 100);
            double latitude = temp + (double)(gpsInfo->rmc.latitude.value - temp * gpsInfo->rmc.latitude.scale * 100) / gpsInfo->rmc.latitude.scale / 60.0;
            temp = (int)(gpsInfo->rmc.longitude.value / gpsInfo->rmc.longitude.scale / 100);
            double longitude = temp + (double)(gpsInfo->rmc.longitude.value - temp * gpsInfo->rmc.longitude.scale * 100) / gpsInfo->rmc.longitude.scale / 60.0;

            snprintf(buffer, sizeof(buffer), "GPS fix mode:%d, BDS fix mode:%d, fix quality:%d, satellites tracked:%d, gps sates total:%d, is fixed:%s, coordinate:WGS84, Latitude:%f, Longitude:%f, unit:degree,altitude:%f",
                     gpsInfo->gsa[0].fix_type, gpsInfo->gsa[1].fix_type, gpsInfo->gga.fix_quality, gpsInfo->gga.satellites_tracked, gpsInfo->gsv[0].total_sats, isFixedStr, latitude, longitude, gpsInfo->gga.altitude);
            // show in tracer
            Trace(1, buffer);
            // send to UART1
            UART_Write(UART1, buffer, strlen(buffer));
            UART_Write(UART1, "\r\n\r\n", 4);
            uint8_t percent;
            uint16_t v = PM_Voltage(&percent);
            Trace(1, "power: %d %d", v, percent);
            memset(buffer, 0, sizeof(buffer));
            if (!INFO_GetIMEI(buffer))
                Assert(false, "NO IMEI");
            Trace(1, "device name: %s", buffer);
            UART_Write(UART1, buffer, strlen(buffer));

            FormatTime(time(NULL) + 7200);
            snprintf(buffer2, sizeof(buffer2), "%s%d m %dkm/h\nFix=%s\nSats=%d\nBatt=%d%% %.3fV\nhttps://www.google.com/maps/search/%f,%f\r\n", buffer3,gpsInfo->gga.altitude.value/gpsInfo->gga.altitude.scale,gpsInfo->vtg.speed_kph.value/gpsInfo->vtg.speed_kph.scale, isFixedStr, gpsInfo->gga.satellites_tracked, percent, v * 0.001f, latitude, longitude);
            // SendSMS(buffer2);
            Trace(1, "payload: %s", buffer2);
        }
        PM_SetSysMinFreq(PM_SYS_FREQ_32K);
        OS_Sleep(5000);
        PM_SetSysMinFreq(PM_SYS_FREQ_178M);
    }
}

void SMSMainTask(void *pData)
{
    API_Event_t *event = NULL;
    TIME_SetIsAutoUpdateRtcTime(true);

    // UART INIT
    UART_Config_t config = {
        .baudRate = UART_BAUD_RATE_115200,
        .dataBits = UART_DATA_BITS_8,
        .stopBits = UART_STOP_BITS_1,
        .parity = UART_PARITY_NONE,
        .rxCallback = NULL,
        .useEvent = true};

    PM_PowerEnable(POWER_TYPE_VPAD, true);
    UART_Init(UART1, config);

    // SMS INIT
    if (!SMS_SetFormat(SMS_FORMAT_TEXT, SIM0))
    {
        Trace(1, "sms set format error");
        return;
    }
    SMS_Parameter_t smsParam = {
        .fo = 17,
        .vp = 167,
        .pid = 0,
        .dcs = 8,
    };
    if (!SMS_SetParameter(&smsParam, SIM0))
    {
        Trace(1, "sms set parameter error");
        return;
    }
    if (!SMS_SetNewMessageStorage(SMS_STORAGE_SIM_CARD))
    {
        Trace(1, "sms set message storage fail");
        return;
    }

    OS_CreateTask(gpsTask, NULL, NULL, MAIN_TASK_STACK_SIZE, MAIN_TASK_PRIORITY, 0, 0, MAIN_TASK_NAME);

    while (1)
    {
        if (OS_WaitEvent(mainTaskHandle, (void **)&event, OS_TIME_OUT_WAIT_FOREVER))
        {
            EventDispatch(event);
            OS_Free(event->pParam1);
            OS_Free(event->pParam2);
            OS_Free(event);
        }
    }
}

void sms_Main()
{
    mainTaskHandle = OS_CreateTask(SMSMainTask, NULL, NULL, MAIN_TASK_STACK_SIZE, MAIN_TASK_PRIORITY, 0, 0, MAIN_TASK_NAME);
    OS_SetUserMainHandle(&mainTaskHandle);
}
