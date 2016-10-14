#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

const char* HOSTNAME = "aquaponics";
const char* OTA_PASSWORD = "password";
const char* WIFI_SSID = "ssid";
const char* WIFI_PASSWORD = "password";
const char* NTP_SERVER_NAME = "time.nist.gov";
const int NTP_PACKET_SIZE = 48;
const unsigned int UDP_PORT = 2390;
const int GMT_OFFSET = 2;
const int REDPIN = 13;
const int GREENPIN = 14;
const int BLUEPIN = 15;

WiFiUDP udp;
IPAddress ntpServerIP;
byte packetBuffer[NTP_PACKET_SIZE];

int hour;
int minute;
int second;

char requestedTime = 0;
int checkTimeRetries = 0;

unsigned long epoch = 0;
unsigned long lastNTP = 0;
unsigned long lastLoop = 0;

MDNSResponder mdns;
ESP8266WebServer server(80);

void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection failed! Restarting...");
    delay(5000);
    ESP.restart();
  }

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("Starting OTA update");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nDone");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  //Get the time
  Serial.println("Starting UDP");
  udp.begin(UDP_PORT);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
  requestTime();
  delay(2000);
  while (!checkTime()) {
    delay(2000);
    checkTimeRetries++;
    if (checkTimeRetries > 5) {
      requestTime();
    }
  }

  pinMode(REDPIN, OUTPUT);
  pinMode(GREENPIN, OUTPUT);
  pinMode(BLUEPIN, OUTPUT);
  digitalWrite(REDPIN, LOW);
  digitalWrite(GREENPIN, LOW);
  digitalWrite(BLUEPIN, LOW);

  if (mdns.begin("esp8266", WiFi.localIP())) {
    Serial.println("MDNS responder started");
  }

  server.on("/", []() {
    char str[10];
    sprintf(str, "%d", hour);
    server.send(200, "text/plain", str);
    // server.send(200, "text/plain", "hello world");
  });

  server.onNotFound([]() {
    server.send(404, "text/plain", "404!");
  });

  server.begin();
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();

  if (millis() - lastLoop > 500) {
    lastLoop = millis();
    // Serial.print("Epoch is: ");
    // Serial.println(epoch);
    int secsSinceLastNTP = (millis() - lastNTP) / 1000;
    // Serial.print("Seconds since last NTP: ");
    // Serial.println(secsSinceLastNTP);

    // Check time with NTP server every 5min
    if (secsSinceLastNTP > 300 and requestedTime == 0) {
      Serial.println("NTP requested");
      requestTime();
      requestedTime = 1;
    }

    if (requestedTime == 1) {
      checkTime();
      checkTimeRetries++;
      if (checkTimeRetries > 5) {
        requestedTime = 0;
      }
    }

    decodeEpoch(epoch + secsSinceLastNTP);

    if (hour >= 9 && hour < 19) {
        // analogWrite(REDPIN, 255);
        // analogWrite(GREENPIN, 255);
        // analogWrite(BLUEPIN, 255);
        digitalWrite(REDPIN, HIGH);
        digitalWrite(GREENPIN, HIGH);
        digitalWrite(BLUEPIN, HIGH);
    } else {
        // analogWrite(REDPIN, 0);
        // analogWrite(GREENPIN, 0);
        // analogWrite(BLUEPIN, 0);
        digitalWrite(REDPIN, LOW);
        digitalWrite(GREENPIN, LOW);
        digitalWrite(BLUEPIN, LOW);
    }
  }
}

void requestTime() {
  Serial.println("Getting Time");
  WiFi.hostByName(NTP_SERVER_NAME, ntpServerIP);
  sendNTPpacket(ntpServerIP); // send an NTP packet to a time server
}

bool checkTime() {
  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
    return false;
  }
  Serial.print("packet received, length=");
  Serial.println(cb);
  // We've received a packet, read the data from it
  udp.read(packetBuffer, NTP_PACKET_SIZE);

  // the timestamp starts at byte 40 of the received packet and is
  // four bytes, or two words, long. First, extract the two words:
  unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
  unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
  // combine the four bytes (two words) into a long integer
  // this is NTP time (seconds since Jan 1 1900):
  unsigned long secsSince1900 = highWord << 16 | lowWord;
  // Serial.print("Seconds since Jan 1 1900 = " );
  // Serial.println(secsSince1900);

  // now convert NTP time into everyday time:
  // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
  const unsigned long seventyYears = 2208988800UL;
  // subtract seventy years:
  epoch = secsSince1900 - seventyYears;
  // Serial.print("Unix time = ");
  // Serial.println(epoch);

  lastNTP = millis();
  requestedTime = 0;
  checkTimeRetries = 0;
  return true;
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address) {
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

void decodeEpoch(unsigned long currentTime) {
  // Serial.print("The UTC time is ");
  // printTime(epoch);
  currentTime = currentTime + (GMT_OFFSET * 60 * 60);
  // Serial.print("The local time is ");
  // printTime(currentTime);

  int hourTmp = (currentTime % 86400L) / 3600;
  if (hourTmp > 12) {
    hourTmp -= 12;
  }
  hour = hourTmp;
  minute = (currentTime % 3600) / 60;
  second = currentTime % 60;
}

void printTime(unsigned long i) {
  // Print the hour (86400 equals secs per day)
  Serial.print((i  % 86400L) / 3600);
  Serial.print(':');
  if ((i % 3600) / 60 < 10 ) {
    // In the first 10 minutes of each hour, we'll want a leading '0'
    Serial.print('0');
  }
  // Print the minute (3600 equals secs per minute)
  Serial.print((i  % 3600) / 60);
  Serial.print(':');
  if (i % 60 < 10 ) {
    // In the first 10 seconds of each minute, we'll want a leading '0'
    Serial.print('0');
  }
  // Print the second
  Serial.println(i % 60);
}
