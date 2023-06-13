/*
  WiFiTelnetToSerial - Example Transparent UART to Telnet Server for ESP32

  Copyright (c) 2017 Hristo Gochkov. All rights reserved.
  This file is part of the ESP32 WiFi library for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  completely rewritten june 12, 2023 by curt mayer, curt@zen-room.org
  to have configuration in EEPROM with an editor.

*/

/*
 * modified to store baud rate and initial port in EEPROM,
 * and also have a control server to change and read settings
 */
#include <WiFi.h>
#include <WiFiServer.h>
#include <EEPROM.h>

//how many clients should be able to telnet to this ESP32
#define MAX_SRV_CLIENTS 1
String Config;

char ssid[30];
char password[30];

/*
 *
 * modified to store baud rate and initial port in EEPROM,
 * and also allow the UART connection to be used to initialize
 * and edit the EEPROM contents
 * 
 * eeprom contents are a formatted string
 * of the form:
 * "ssid","password",dataport1:baud1
 * where the port and baud rate are integers
 * if the contents are not well formed, we default
 */
#define EEPROM_SIZE 128

//how many clients should be able to telnet to this ESP32
#define MAX_SRV_CLIENTS 1

#define DEFAULT_SSID "yourssid"
#define DEFAULT_PASS "yourpassword"
#define DEFAULT_PORT1 23
#define DEFAULT_BAUD1 38400

WiFiServer server;
WiFiClient client;

int port;
int baud;

/*
 * this is a somewhat robust state machine parser for the Config string.
 * it reads the eeprom, tries to parse it, and if that fails, parses the
 * default configuration from the above definitions.
 */
void parseeeprom() {
  const char *cbuf;
  char *s, c;
  int p;
  int num;
  int failed = 0;

  EEPROM.begin(EEPROM_SIZE);
  String data = EEPROM.readString(0);
  EEPROM.end();

again:
  if (failed || (data.length() == 0)) {
    Config = "\"";
    Config += DEFAULT_SSID;
    Config += "\",\"";
    Config += DEFAULT_PASS;
    Config += "\",";
    Config += DEFAULT_PORT1;
    Config += ":";
    Config += DEFAULT_BAUD1;
    Config += "\n";
  } else {
    Config = data;
  }
  Serial.println("using configuration: " + Config);
  cbuf = Config.c_str();

  p = 0;
  while ((c = *cbuf++)) {
    // Serial.printf("state: %d char: 0x%x num: %d\n", p, c, num);
    switch (p) {
      case 0:
        if (c != '\"') goto fail;
        p++;
        s = ssid;
        break;
      case 1:
        if (c == '"') {
          *s = '\0';
          p++;
        } else {
          *s++ = c;
        }
        break;
      case 2:
        if (c != ',') goto fail;
        p++;
        break;
      case 3:
        if (c != '\"') goto fail;
        p++;
        s = password;
        break;
      case 4:
        if (c == '"') {
          *s = '\0';
          p++;
        } else {
          *s++ = c;
        }
        break;
      case 5:
        if (c != ',') goto fail;
        p++;
        num = 0;
        break;
      case 6:
        if (c == ':') {
          port = num;
          p++;
          num = 0;
        } else {
          if ((c < '0') || (c > '9')) goto fail;
          num = (num * 10) + (c - '0');
        }
        break;
      case 7:
        if (c == '\n') {
          baud = num;
          p++;
          num = 0;
        } else {
          if ((c < '0') || (c > '9')) goto fail;
          num = (num * 10) + (c - '0');
        }
        break;
      default:
        Serial.println("long parse error");
        goto fail;
    }
  }
  return;

fail:
  if (failed++) {
    Serial.println("parse of default failed");
    return;
  }
  goto again;
}

void editstr(char *prompt, char *buf) {
  unsigned char c;
  int cursor = strlen(buf);

  Serial.printf("%s: %s", prompt, buf);
  while (1) {
    if (Serial.available() <= 0) continue;
    c = Serial.read();

    if (c == '\r') {
      Serial.printf("\r\n");
      return;
    }
    switch (c) {
      case 0x7f:
      case 0x8:
        if (cursor != 0) {
          Serial.printf("\b \b");
          buf[--cursor] = '\0';
        }
        break;
      case 0x0d:
        return;
      default:
        if ((c < ' ') || (c > 0x80)) {
          break;
        }
        buf[cursor++] = c;
        Serial.printf("%c", c);
    }
  }
}

void editnum(char *prompt, int *valp) {
  char buf[20];
  sprintf(buf, "%d", *valp);
  editstr(prompt, buf);
  *valp = atoi(buf);
}

int editeeprom() {
  String Newconfig;

  editstr("ssid", ssid);
  editstr("password", password);
  editnum("port1", &port);
  editnum("baud1", &baud);

  Newconfig = "\"";
  Newconfig += ssid;
  Newconfig += "\",\"";
  Newconfig += password;
  Newconfig += "\",";
  Newconfig += port;
  Newconfig += ":";
  Newconfig += baud;
  Newconfig += "\n";

  if (Config.equals(Newconfig)) {
    return 0;
  } else {
    Serial.printf("EEPROM updated\n\r");
    EEPROM.begin(EEPROM_SIZE);
    EEPROM.writeString(0, Newconfig);
    EEPROM.commit();
    EEPROM.end();
    return 1;
  }
}

#define LED 2

void setup() {

  pinMode(LED, OUTPUT);

  Serial.begin(115200);
  Serial.println("\nConnecting");

  parseeeprom();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println("Connecting Wifi ");
  for (int loops = 3; loops > 0; loops--) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("");
      Serial.print("WiFi connected ");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      break;
    } else {
      Serial.println(loops);
      delay(1000);
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connect failed");
    delay(1000);
    (void)editeeprom();
    ESP.restart();
  }

  //start UART and the server
  Serial2.begin(baud);

  server.begin(port);
  server.setNoDelay(true);

  Serial.print("Ready! Use ");
  Serial.print(WiFi.localIP());
  Serial.println(" to connect on port " + String(port));
}

void loop() {
    if (Serial.available() > 0) {
    Serial.println("Edit configuration");
    (void)Serial.read();
    if (editeeprom()) {
      ESP.restart();    
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    if (server.hasClient()) {
      client = server.available();
      if (!client) Serial.println("available broken");
      Serial.print("client connected from ");
      Serial.println(client.remoteIP());
    }

    if (client) {
      if (!client.connected()) {
        Serial.println("client disconnected");
        client.stop();
        client = 0;
      } else {
        if (client.available()) {
          digitalWrite(LED,HIGH);
          Serial2.write(client.read());
        }

        if (Serial2.available()) {
          digitalWrite(LED,HIGH);
          size_t len = Serial2.available();
          uint8_t sbuf[len];
          Serial2.readBytes(sbuf, len);
          client.write(sbuf, len);
        }
      }
    }
  } else {
    Serial.println("WiFi not connected!");
    if (client) {
      client.stop();
      client = 0;
    }
    delay(1000);
  }
  digitalWrite(LED, LOW);
}
