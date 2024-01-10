#if ARDUINO_USB_MODE
#warning This sketch should be used when USB is in OTG mode
void setup() {}
void loop() {}
#else
#include "USB.h"
#include "USBMSC.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include "FS.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define HWSerial Serial0
#define USBSerial Serial
#else
#define HWSerial Serial
USBCDC USBSerial;
#endif

struct SettingsST
{
    const char ssid[10] = "UCU_Guest";
    const char password[1] = "";
    const char server_ip[13] = "10.10.246.14";
    const int remote_port = 12345;
    const int remote_port2 = 12346;
};

SettingsST settings;

struct remote_request
{
    uint64_t position;
    uint32_t length;
} __attribute__((packed));

struct init_answer
{
    unsigned int sectors_count;
    unsigned int sector_size;
    unsigned int reserved[6];
};


USBMSC MSC;
WiFiClient client;
WiFiClient client2;

uint32_t DISK_SECTOR_COUNT = 0;
uint16_t DISK_SECTOR_SIZE = 512;
static const uint16_t DISC_SECTORS_PER_TABLE = 1; //each table sector can fit 170KB (340 sectors)
static const byte LED_PIN = 2;
const int BUFFER_SIZE = 32768;


enum BUFFER_STATUSES
{
    BUFFER_STATUS_NONE = 0,
    BUFFER_STATUS_NEED_REQUEST,
    BUFFER_STATUS_IN_REQUEST,
    BUFFER_STATUS_ERROR,
    BUFFER_STATUS_DONE
};

struct sectors_buffer
{
    BUFFER_STATUSES status = BUFFER_STATUS_NONE;
    int64_t lba = 0;
    uint32_t offset = 0;
    uint32_t bufsize = BUFFER_SIZE;
    uint32_t recv_offset = 0;
    uint32_t recv_len = 0;
    byte buffer[BUFFER_SIZE];
} __attribute__((packed));

init_answer answer = {};
sectors_buffer g_buffer = {};
sectors_buffer g_buffer2 = {};

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    if (bufsize > BUFFER_SIZE)
    {
        Serial.printf("Error write: bufsize: %u\n", bufsize);
    }
    else if (g_buffer2.status == BUFFER_STATUS_NONE || g_buffer2.status == BUFFER_STATUS_DONE){
        g_buffer2.lba = lba;
        g_buffer2.status = BUFFER_STATUS_NEED_REQUEST;
        g_buffer2.offset = offset;
        g_buffer2.bufsize = bufsize;
        memcpy(g_buffer2.buffer, buffer, bufsize);
    }

    while (g_buffer2.status == BUFFER_STATUS_NEED_REQUEST)
    {
        delay(1);
    }
    if (g_buffer2.status == BUFFER_STATUS_DONE)
    {
        return bufsize;
    }
    return 0;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    uint64_t pos = (uint64_t)lba * DISK_SECTOR_SIZE + offset;
    uint64_t buffer_pos = (uint64_t)g_buffer.lba * DISK_SECTOR_SIZE + g_buffer.offset;
    if (buffer_pos <= pos && buffer_pos + g_buffer.bufsize >= pos + bufsize)
    {
        if (g_buffer.status == BUFFER_STATUS_IN_REQUEST || g_buffer.status == BUFFER_STATUS_NEED_REQUEST)
        {
            delay(1);
        }
        if (g_buffer.status == BUFFER_STATUS_DONE)
        {
            memcpy(buffer, g_buffer.buffer + (pos - buffer_pos), bufsize);
            return bufsize;
        }
    }
    while (g_buffer.status == BUFFER_STATUS_IN_REQUEST || g_buffer.status == BUFFER_STATUS_NEED_REQUEST)
    {
        delay(1);
    }
    if (g_buffer.status == BUFFER_STATUS_NONE || g_buffer.status == BUFFER_STATUS_ERROR || g_buffer.status == BUFFER_STATUS_DONE)
    {
        g_buffer.lba = lba;
        g_buffer.status = BUFFER_STATUS_NEED_REQUEST;
        g_buffer.bufsize = BUFFER_SIZE;
    }
    while (g_buffer.status == BUFFER_STATUS_IN_REQUEST || g_buffer.status == BUFFER_STATUS_NEED_REQUEST)
    {
        delay(1);
    }
    if (g_buffer.status == BUFFER_STATUS_DONE)
    {
        memcpy(buffer, g_buffer.buffer, bufsize);

        return bufsize;
    }


    return 0;
}


static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
    HWSerial.printf("MSC START/STOP: power: %u, start: %u, eject: %u\r\n", power_condition, start, load_eject);
    return true;
}

static void usbEventCallback(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == ARDUINO_USB_EVENTS) {
        arduino_usb_event_data_t* data = (arduino_usb_event_data_t*)event_data;
        switch (event_id) {
            case ARDUINO_USB_STARTED_EVENT:
                HWSerial.println(F("USB PLUGGED"));
                break;
            case ARDUINO_USB_STOPPED_EVENT:
                HWSerial.println("USB UNPLUGGED");
                break;
            case ARDUINO_USB_SUSPEND_EVENT:
                HWSerial.printf("USB SUSPENDED: remote_wakeup_en: %u\r\n", data->suspend.remote_wakeup_en);
                break;
            case ARDUINO_USB_RESUME_EVENT:
                HWSerial.println(F("USB RESUMED"));
                break;

            default:
                break;
        }
    }
}


void receiveInit()
{
    remote_request request = { 0, 0 };
    if (client.write((byte*)&request, sizeof(request)) != sizeof(request))
    {
        client.stop();
    }
    client.flush();
    while (client.connected())
    {
        if (client.available())
        {
            client.setTimeout(0);
            if (client.readBytes((byte*)&answer, sizeof(answer)) == sizeof(answer))
            {
                DISK_SECTOR_COUNT = answer.sectors_count;
                DISK_SECTOR_SIZE = answer.sector_size;
                if (client.readBytes((byte*)&answer, sizeof(answer)) == sizeof(answer))
                    Serial.printf("Received drive sectors count: %u\n", (uint32_t)DISK_SECTOR_COUNT);
                Serial.printf("Received drive sector size: %u\n", (uint32_t)DISK_SECTOR_SIZE);
                break;
            }
            client.stop();
            break;
        }

    }
}

void setup() {
    Serial.println(F("Starting..."));
    HWSerial.begin(115200);
    HWSerial.setDebugOutput(true);
    Serial.begin(115200);

    while (!Serial) {;}
    uint32_t timeStart = millis();
    Serial.print(F("Attempting to connect to SSID: "));
    Serial.println(settings.ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.begin(settings.ssid, settings.password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(100);
        Serial.print(".");
        if (millis() - timeStart > 10000)
        {
            break;
        }
    }
    Serial.println(F("Connected to WiFi"));
    Serial.print(F("SSID: "));
    Serial.println(WiFi.SSID());
    IPAddress ip = WiFi.localIP();
    Serial.print(F("IP Address: "));
    Serial.println(ip);
    long rssi = WiFi.RSSI();
    Serial.print(F("signal strength (RSSI):"));
    Serial.print(rssi);
    Serial.println(F(" dBm"));
    client.setNoDelay(true);
    while (!client.connected())
    {
        if (client.connect(settings.server_ip, settings.remote_port))
        {
            if (client.connected())
            {
                Serial.println(F("Connected to server"));
                receiveInit();
            }
        }
        if (client.connected() && DISK_SECTOR_COUNT != 0)
        {
            break;
        }
    }
    client.setNoDelay(true);
    client2.setNoDelay(true);
    while (!client2.connected())
    {
        if (client2.connect(settings.server_ip, settings.remote_port2))
        {
            if (client2.connected())
            {
                Serial.println(F("Connected to server2"));
            }
        }
        if (millis() - timeStart > 20000)
        {
            break;
        }
    }
    client2.setNoDelay(true);
    USB.onEvent(usbEventCallback);
    MSC.vendorID("ESP32");//max 8 chars
    MSC.productID("USB_MSC_MPZ");//max 16 chars
    MSC.productRevision("1.0");//max 4 chars
    MSC.onStartStop(onStartStop);
    MSC.onRead(onRead);
    MSC.onWrite(onWrite);
    MSC.mediaPresent(true);
    MSC.begin(DISK_SECTOR_COUNT, DISK_SECTOR_SIZE);
    USBSerial.begin(115200);
    USB.begin();
}

bool connected = true;
bool connected2 = true;

void loop()
{
    while (!client.connected())
    {
        if (connected)
        {
            connected = false;
            Serial.println(F("Disconnected from server"));
        }
        if (client.connect(settings.server_ip, settings.remote_port))
        {
            if (client.connected())
            {
                Serial.println(F("Connected to server"));
                g_buffer.status = BUFFER_STATUS_NONE;
                receiveInit();
                connected = true;
                break;
            }
        }

    }
    while (!client2.connected())
    {
        if (connected2)
        {
            connected2 = false;
            Serial.println(F("Disconnected from server2"));
        }
        if (client2.connect(settings.server_ip, settings.remote_port2))
        {
            if (client2.connected())
            {
                Serial.println(F("Connected to server2"));
                g_buffer2.status = BUFFER_STATUS_NONE;
                connected2 = true;
                break;
            }
        }

    }
    if (g_buffer.status == BUFFER_STATUS_NEED_REQUEST)
    {
        remote_request request = { g_buffer.lba * DISK_SECTOR_SIZE + g_buffer.offset, g_buffer.bufsize };
        if (client.write((byte*)&request, sizeof(request)) != sizeof(request))
        {
            client.stop();
        }
        else
        {
            client.flush();
            g_buffer.status = BUFFER_STATUS_IN_REQUEST;
            g_buffer.recv_offset = 0;
            g_buffer.recv_len = g_buffer.bufsize;

        }
    }
    if (g_buffer2.status == BUFFER_STATUS_NEED_REQUEST)
    {
        Serial.println(F("Sending data to server2"));
        if (client2.write((byte*)&g_buffer2, sizeof(g_buffer2)) != sizeof(g_buffer2))
        {
            client2.stop();
        }
        else
        {
            Serial.println(F("Data sent to server2"));
            client2.flush();
            g_buffer2.status = BUFFER_STATUS_DONE;
        }
    }
    if (g_buffer.status == BUFFER_STATUS_IN_REQUEST)
    {
        uint32_t recv = client.readBytes(g_buffer.buffer + g_buffer.recv_offset, g_buffer.recv_len);
        if (recv == g_buffer.recv_len)
        {
            g_buffer.status = BUFFER_STATUS_DONE;
        }
        else if (recv < g_buffer.recv_len)
        {
            g_buffer.recv_len -= recv;
            g_buffer.recv_offset += recv;
        }
        else
        {
            g_buffer.status = BUFFER_STATUS_ERROR;
            Serial.println(F("Error read"));
        }
    }
}
#endif /* ARDUINO_USB_MODE */