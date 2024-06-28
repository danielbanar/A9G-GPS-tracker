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
#include <api_gps.h>
#include "buffer.h"
#include "gps_parse.h"
#include "math.h"
#include "gps.h"
#include "api_hal_pm.h"
#include "api_info.h"
#include "assert.h"
#include "api_socket.h"
#include "api_network.h"
#include "api_hal_gpio.h"
#include "api_call.h"
#include "api_audio.h"

// Configuration

#define MAIN_TASK_STACK_SIZE (2048 * 2)
#define MAIN_TASK_PRIORITY 0
#define MAIN_TASK_NAME "SMS GPS Tracker"
#define SYSTEM_STATUS_LED GPIO_PIN27
#define UPLOAD_DATA_LED GPIO_PIN28
HANDLE mainTaskHandle = NULL;
uint8_t flag = 0;
bool isGpsOn = true;
uint8_t buffer2[400] = "NO GPS", buffer3[80];
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
void ParsePhoneNumber(const char *input, char *phoneNumber)
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
void EventDispatch(API_Event_t *pEvent)
{
    SMS_Storage_Info_t storageInfo;
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
    case API_EVENT_ID_CALL_INCOMING:
        Trace(1, "Receiving a call from: %s, number type:%d", pEvent->pParam1, pEvent->param1);
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
        GPIO_Set(UPLOAD_DATA_LED, GPIO_LEVEL_HIGH);
        Trace(2, "message header: %s", header);
        Trace(2, "message content length: %d", contentLength);
        if (encodeType == SMS_ENCODE_TYPE_ASCII)
        {
            Trace(2, "message content: %s", content);
            char phoneNumber[20];
            ParsePhoneNumber(header, phoneNumber);
            if (!strcmp(content, "GPS") || !strcmp(content, "gps") || !strcmp(content, "Gps"))
                SendSMS(phoneNumber, buffer2);
        }
        GPIO_Set(UPLOAD_DATA_LED, GPIO_LEVEL_LOW);

        // SMS_GetStorageInfo(&storageInfo, SMS_STORAGE_SIM_CARD);
        // Trace(1, "sms storage sim card info, used:%d,total:%d", storageInfo.used, storageInfo.total);
        // used = storageInfo.used;
        // total = storageInfo.total;
        
        //Delete all messages on sim, because if the sim card is full you will not receive any messages
        for (size_t i = 0; i < 10; i++)
        {
            SMS_DeleteMessage(i, SMS_STATUS_ALL, SMS_STORAGE_SIM_CARD);
        }
        break;
    case API_EVENT_ID_SMS_LIST_MESSAGE:
    {
        SMS_Message_Info_t *messageInfo = (SMS_Message_Info_t *)pEvent->pParam1;
        OS_Free(messageInfo->data);
        break;
    }
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
    const long secondsInDay = 86400;
    long totalDays = timestamp / secondsInDay;
    int seconds = timestamp % 60;
    int minutes = (timestamp / 60) % 60;
    int hours = (timestamp / 3600) % 24;
    int year = 1970;
    while (1)
    {
        int daysInYear = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
        if (totalDays < daysInYear)
            break;
        totalDays -= daysInYear;
        year++;
    }
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
    sprintf(buffer3, "%02d:%02d:%02d %02d.%02d.%04d", hours, minutes, seconds, day, month, year);
}
void gpsTask(void *pData)
{
    GPS_Info_t *gpsInfo = Gps_GetInfo();

    UART_Write(UART1, "Init now\r\n", strlen("Init now\r\n"));
    // wait for gprs register complete
    // The process of GSM registration network may cause the power supply voltage of GPS to drop,
    // which resulting in GPS restart.
    while (flag < 3)
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

    // if (!GPS_SetLpMode(GPS_LP_MODE_SUPPER_LP))
    //     Trace(1, "set gps lp mode fail");

    if (!GPS_SetOutputInterval(1000))
        Trace(1, "set nmea output interval fail");

    UART_Write(UART1, "Init ok\r\n", strlen("Init ok\r\n"));

    while (1)
    {
        if (isGpsOn)
        {
            // show fix info, no idea how it works but
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

            uint8_t percent;
            uint16_t v = PM_Voltage(&percent);
            Trace(1, "power: %d %d", v, percent);

            FormatTime(time(NULL) + 7200);
            snprintf(buffer2, sizeof(buffer2), "%s\n%d m %dkm/h\nFix=%s\nSats=%d\nBatt=%d%% %.3fV\nhttps://www.google.com/maps/search/%f,%f %d/%d\r\n", buffer3, gpsInfo->gga.altitude.value / gpsInfo->gga.altitude.scale, gpsInfo->vtg.speed_kph.value / gpsInfo->vtg.speed_kph.scale, isFixedStr, gpsInfo->gga.satellites_tracked, percent, v * 0.001f, latitude, longitude, used, total);
            // SendSMS(buffer2);
            Trace(1, "payload: %s", buffer2);
        }
        PM_SetSysMinFreq(PM_SYS_FREQ_32K);
        OS_Sleep(5000);
        PM_SetSysMinFreq(PM_SYS_FREQ_178M);
    }
}
void LED_Blink(void *param)
{
    static int count = 0;
    if (++count == 5)
    {
        GPIO_Set(SYSTEM_STATUS_LED, GPIO_LEVEL_HIGH);
    }
    else if (count == 6)
    {
        GPIO_Set(SYSTEM_STATUS_LED, GPIO_LEVEL_LOW);
        count = 0;
    }
    OS_StartCallbackTimer(mainTaskHandle, 1000, LED_Blink, NULL);
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

    GPIO_config_t gpioLedBlue = {
        .mode = GPIO_MODE_OUTPUT,
        .pin = SYSTEM_STATUS_LED,
        .defaultLevel = GPIO_LEVEL_LOW};

    GPIO_config_t gpioLedUpload = {
        .mode = GPIO_MODE_OUTPUT,
        .pin = UPLOAD_DATA_LED,
        .defaultLevel = GPIO_LEVEL_LOW};
    PM_PowerEnable(POWER_TYPE_VPAD, true);
    GPIO_Init(gpioLedBlue);
    GPIO_Init(gpioLedUpload);
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
    OS_StartCallbackTimer(mainTaskHandle, 1000, LED_Blink, NULL);
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
