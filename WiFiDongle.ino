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
#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>
#include <driver/uart.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "FreeSans8pt7b.h"
#define FONT &FreeSans8pt7b

// FreeSans8pt7b: yAdvance=17, ascent=11, '0' is 9px wide, '.' is 4px wide
// zero-filled IP (000.000.000.000) = 12*9 + 3*4 = 120px, fits 128px
#define FONT_HEIGHT 11          // glyph ascent in pixels
#define LINE_SPACING 5          // gap between bottom of one line and top of next
#define LINE_HEIGHT (FONT_HEIGHT + LINE_SPACING)  // 16px per line
// baseline Y for line N (0-based): first baseline at FONT_HEIGHT + 1
#define LINE_Y(n) (FONT_HEIGHT + 1 + (n) * LINE_HEIGHT)

#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 *display;

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
 * "ssid","password",dataport1:baud1:flowctl:rx,tx,rts,cts:displayht
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
#define DEFAULT_FLOW 0
#define DEFAULT_RX 16
#define DEFAULT_TX 17
#define DEFAULT_RTS 5
#define DEFAULT_CTS 4
#define DEFAULT_DISPLAYHT 32

WiFiServer server;
WiFiClient client;

int port;
int baud;
int flowctl;
int rxpin, txpin, rtspin, ctspin;
int displayht;
int needswrite;
unsigned long led_off_time = 0;
#define LED_ON_MS 50
uint8_t last_tx = 0;
uint8_t last_rx = 0;
unsigned long last_display_update = 0;
#define DISPLAY_UPDATE_MS 250

// telnet protocol bytes
#define IAC   0xff
#define DONT  0xfe
#define DO    0xfd
#define WONT  0xfc
#define WILL  0xfb
#define SB    0xfa
#define SE    0xf0
#define BRK   0xf3
#define IP    0xf4  // interrupt process
#define AO    0xf5  // abort output
#define AYT   0xf6  // are you there
#define TNOP  0xf1  // telnet NOP (avoid conflict with ESP32 NOP macro)

#define OPT_ECHO 1
#define OPT_SGA  3

// connection mode: detected from first byte of client traffic
#define MODE_DETECT 0   // waiting for first byte
#define MODE_TELNET 1   // telnet client: IAC parsing + 0xFF escaping active
#define MODE_RAW    2   // raw client: fully transparent, no protocol handling
int conn_mode = MODE_DETECT;

// IAC state machine (only active in MODE_TELNET)
int iac_state = 0;     // 0=data, 1=got IAC, 2=got cmd, 3=subneg, 4=subneg got IAC
uint8_t iac_cmd = 0;   // WILL/WONT/DO/DONT byte, saved for option reply

static const char *iac_cmd_name(uint8_t cmd) {
  switch (cmd) {
    case WILL: return "WILL";
    case WONT: return "WONT";
    case DO:   return "DO";
    case DONT: return "DONT";
    case SB:   return "SB";
    default:   return "??";
  }
}

// send our initial telnet negotiation: character mode + server echo
void telnet_negotiate() {
  uint8_t charmode[] = { IAC, WILL, OPT_SGA, IAC, DO, OPT_SGA, IAC, WILL, OPT_ECHO };
  client.write(charmode, sizeof(charmode));
  Serial.println("telnet: sent WILL SGA, DO SGA, WILL ECHO");
}

// send a telnet option reply
void telnet_reply(uint8_t cmd, uint8_t opt) {
  uint8_t buf[] = { IAC, cmd, opt };
  client.write(buf, 3);
  Serial.printf("telnet: sent %s %d\n", iac_cmd_name(cmd), opt);
}

// process a byte from the client in telnet mode.
// returns 1 if it's a data byte to forward to UART, 0 if consumed by protocol.
int telnet_input(uint8_t c) {
  switch (iac_state) {
  case 0: // normal data
    if (c == IAC) {
      iac_state = 1;
      return 0;
    }
    return 1;

  case 1: // got IAC
    if (c == IAC) {           // escaped 0xFF data byte
      iac_state = 0;
      return 1;
    }
    if (c == SB) {
      Serial.print("telnet: SB ");
      iac_state = 3;
      return 0;
    }
    if (c >= WILL && c <= DONT) {
      iac_cmd = c;
      iac_state = 2;
      return 0;
    }
    // 2-byte commands
    switch (c) {
    case BRK: {
      Serial.println("telnet: BRK");
      // send break: 100 bit-times ensures framing error at any baud rate
      uint8_t brk = 0;
      uart_write_bytes_with_break(2, &brk, 1, 100);
      break;
    }
    case AYT:
      Serial.println("telnet: AYT");
      client.write("\r\n[Yes]\r\n", 9);
      break;
    case IP:  Serial.println("telnet: IP");  break;
    case AO:  Serial.println("telnet: AO");  break;
    case TNOP: break;
    default:
      Serial.printf("telnet: cmd %02x\n", c);
      break;
    }
    iac_state = 0;
    return 0;

  case 2: // option byte following WILL/WONT/DO/DONT
    Serial.printf("telnet: %s %d\n", iac_cmd_name(iac_cmd), c);
    if (c == OPT_SGA || c == OPT_ECHO) {
      // accept SGA and ECHO for character mode
      if (iac_cmd == WILL) telnet_reply(DO, c);
      else if (iac_cmd == DO) telnet_reply(WILL, c);
    } else {
      // refuse everything else
      if (iac_cmd == WILL) telnet_reply(DONT, c);
      else if (iac_cmd == DO) telnet_reply(WONT, c);
    }
    iac_state = 0;
    return 0;

  case 3: // subnegotiation data: consume until IAC SE
    if (c == IAC) {
      iac_state = 4;
    } else {
      Serial.printf("%02x ", c);
    }
    return 0;

  case 4: // subneg: got IAC, expecting SE
    if (c == SE) {
      Serial.println("SE");
      iac_state = 0;
    } else {
      Serial.printf("%02x ", c);
      iac_state = 3;
    }
    return 0;
  }
  iac_state = 0;
  return 1;
}

// write UART data to telnet client, escaping 0xFF as IAC IAC
void telnet_write(uint8_t *buf, size_t len) {
  size_t start = 0;
  for (size_t i = 0; i < len; i++) {
    if (buf[i] == 0xff) {
      // flush data before this byte
      if (i > start) client.write(buf + start, i - start);
      uint8_t iac_iac[] = { IAC, IAC };
      client.write(iac_iac, 2);
      start = i + 1;
    }
  }
  // flush remainder
  if (start < len) client.write(buf + start, len - start);
}

const char *wifistatus(int status) {
  switch (status) {
    case WL_IDLE_STATUS:     return "idle";
    case WL_NO_SSID_AVAIL:   return "SSID not found";
    case WL_CONNECTED:       return "connected";
    case WL_CONNECT_FAILED:  return "connect failed";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED:    return "disconnected";
    default:                 return "unknown";
  }
}

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
  needswrite = 0;

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
    Config += ":";
    Config += DEFAULT_FLOW;
    Config += ":";
    Config += DEFAULT_RX;
    Config += ",";
    Config += DEFAULT_TX;
    Config += ",";
    Config += DEFAULT_RTS;
    Config += ",";
    Config += DEFAULT_CTS;
    Config += ":";
    Config += DEFAULT_DISPLAYHT;
    Config += "\n";
  } else {
    Config = data;
  }
  Serial.println("using configuration: " + Config);
  cbuf = Config.c_str();
  // Serial.printf("string form: %s\n", cbuf);
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
          flowctl = 0;
          rxpin = DEFAULT_RX; txpin = DEFAULT_TX;
          rtspin = DEFAULT_RTS; ctspin = DEFAULT_CTS;
          displayht = DEFAULT_DISPLAYHT;
          needswrite = 1;
          p = 14;
        } else if (c == ':') {
          baud = num;
          p++;
          num = 0;
        } else {
          if ((c < '0') || (c > '9')) goto fail;
          num = (num * 10) + (c - '0');
        }
        break;
      case 8:
        if (c == '\n') {
          flowctl = num;
          rxpin = DEFAULT_RX; txpin = DEFAULT_TX;
          rtspin = DEFAULT_RTS; ctspin = DEFAULT_CTS;
          displayht = DEFAULT_DISPLAYHT;
          needswrite = 1;
          p = 14;
        } else if (c == ':') {
          flowctl = num;
          p++;
          num = 0;
        } else {
          if ((c < '0') || (c > '9')) goto fail;
          num = (num * 10) + (c - '0');
        }
        break;
      case 9:
        if (c == ',') {
          rxpin = num;
          p++;
          num = 0;
        } else {
          if ((c < '0') || (c > '9')) goto fail;
          num = (num * 10) + (c - '0');
        }
        break;
      case 10:
        if (c == ',') {
          txpin = num;
          p++;
          num = 0;
        } else {
          if ((c < '0') || (c > '9')) goto fail;
          num = (num * 10) + (c - '0');
        }
        break;
      case 11:
        if (c == ',') {
          rtspin = num;
          p++;
          num = 0;
        } else {
          if ((c < '0') || (c > '9')) goto fail;
          num = (num * 10) + (c - '0');
        }
        break;
      case 12:
        if (c == '\n') {
          ctspin = num;
          displayht = DEFAULT_DISPLAYHT;
          needswrite = 1;
          p = 14;
        } else if (c == ':') {
          ctspin = num;
          p++;
          num = 0;
        } else {
          if ((c < '0') || (c > '9')) goto fail;
          num = (num * 10) + (c - '0');
        }
        break;
      case 13:
        if (c == '\n') {
          displayht = num;
          p++;
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

String buildconfig() {
  String c = "\"";
  c += ssid;
  c += "\",\"";
  c += password;
  c += "\",";
  c += String(port);
  c += ":";
  c += String(baud);
  c += ":";
  c += String(flowctl);
  c += ":";
  c += String(rxpin);
  c += ",";
  c += String(txpin);
  c += ",";
  c += String(rtspin);
  c += ",";
  c += String(ctspin);
  c += ":";
  c += String(displayht);
  c += "\n";
  return c;
}

void writeeeprom(String newconfig) {
  Serial.println("writing eeprom with " + newconfig);
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.writeString(0, newconfig);
  EEPROM.commit();
  EEPROM.end();
  Config = newconfig;
  Serial.println("done");
}

void editstr(const char *prompt, char *buf) {
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

void editnum(const char *prompt, int *valp) {
  char buf[20];
  sprintf(buf, "%d", *valp);
  editstr(prompt, buf);
  *valp = atoi(buf);
}

int editeeprom() {
  String Newconfig;

  Serial.println("Scanning for WiFi networks...");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("  No networks found");
  } else {
    Serial.printf("  %d network(s) found:\n", n);
    for (int i = 0; i < n; i++) {
      Serial.printf("  %2d: %-32s  %ddBm  %s\n", i + 1,
        WiFi.SSID(i).c_str(), WiFi.RSSI(i),
        WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "encrypted");
    }
  }
  WiFi.scanDelete();
  Serial.println();

  editstr("ssid", ssid);
  editstr("password", password);
  editnum("port1", &port);
  editnum("baud1", &baud);
  editnum("flowctl (0=off, 1=rtscts)", &flowctl);
  editnum("rx pin", &rxpin);
  editnum("tx pin", &txpin);
  editnum("rts pin", &rtspin);
  editnum("cts pin", &ctspin);
  editnum("display height (32 or 64)", &displayht);

  Newconfig = buildconfig();
  if (Config.equals(Newconfig)) {
    Serial.println("no difference");
    return 0;
  }
  writeeeprom(Newconfig);
  return 1;
}

#define LED 2

int display_present = 1;

void update_traffic_display() {
  if (!display_present || displayht <= 32) return;
  if (millis() - last_display_update < DISPLAY_UPDATE_MS) return;
  last_display_update = millis();
  // clear lines 2-3 (bottom half of 64px display) and redraw
  display->fillRect(0, LINE_Y(2) - FONT_HEIGHT, 128, 2 * LINE_HEIGHT, SSD1306_BLACK);
  display->setFont(FONT);
  display->setTextSize(1);
  display->setTextWrap(false);
  display->setTextColor(SSD1306_WHITE);
  char tc = (last_tx >= ' ' && last_tx < 0x7f) ? last_tx : '.';
  char rc = (last_rx >= ' ' && last_rx < 0x7f) ? last_rx : '.';
  display->setCursor(0, LINE_Y(2));
  display->printf("tx %c %02x", tc, last_tx);
  display->setCursor(0, LINE_Y(3));
  display->printf("rx %c %02x", rc, last_rx);
  display->display();
}

void setup() {
  
  pinMode(LED, OUTPUT);

  Serial.begin(115200);
  Serial.println("\nConnecting");

  parseeeprom();
  Serial.printf("  pins: rx=%d tx=%d rts=%d cts=%d\n", rxpin, txpin, rtspin, ctspin);
  if (needswrite) {
    writeeeprom(buildconfig());
    needswrite = 0;
  }

  display = new Adafruit_SSD1306(128, displayht, &Wire, -1);
  if(!display->begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    display_present = 0;
  } else {
    display->clearDisplay();
    display->setFont(FONT);
    display->setTextSize(1);
    display->setTextWrap(false);
    display->setTextColor(SSD1306_WHITE);
    display->setCursor(0, LINE_Y(0));
    display->println(ssid);
    display->setCursor(0, LINE_Y(1));
    display->printf("%d:%d", port, baud);
    display->display();
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println("Connecting Wifi ");
  for (int loops = 30; loops > 0; loops--) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("");
      Serial.print("WiFi connected ");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());

      if (display_present) {
        display->clearDisplay();
        display->setFont(FONT);
        display->setTextSize(1);
        display->setTextWrap(false);
        display->setTextColor(SSD1306_WHITE);
        display->setCursor(0, LINE_Y(0));
        display->println(WiFi.localIP());
        display->setCursor(0, LINE_Y(1));
        display->printf("%d:%d", port, baud);
        display->display();
      }
      break;
    } else {
      Serial.printf("%d (%s)\n", loops, wifistatus(WiFi.status()));
      delay(1000);
    }
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("WiFi connect failed: %s\n", wifistatus(WiFi.status()));
    delay(1000);
    (void)editeeprom();
    ESP.restart();
  }

  //start UART and the server
  Serial2.begin(baud);
  Serial2.setPins(rxpin, txpin, ctspin, rtspin);
  if (flowctl) {
    Serial2.setHwFlowCtrlMode(UART_HW_FLOWCTRL_CTS_RTS, 64);
    Serial.println("Hardware flow control enabled");
  }

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
      conn_mode = MODE_DETECT;
      iac_state = 0;
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
          uint8_t c = client.read();

          // first byte from client determines protocol
          if (conn_mode == MODE_DETECT) {
            if (c == IAC) {
              conn_mode = MODE_TELNET;
              Serial.println("telnet client detected");
              telnet_negotiate();
              iac_state = 1;  // we already consumed the IAC
              goto skip_data;
            } else {
              conn_mode = MODE_RAW;
              Serial.println("raw client detected");
            }
          }

          if (conn_mode == MODE_TELNET) {
            if (!telnet_input(c)) goto skip_data;
          }

          digitalWrite(LED, HIGH);
          led_off_time = millis() + LED_ON_MS;
          last_tx = c;
          Serial2.write(c);
          skip_data: ;
        }

        if (Serial2.available()) {
          digitalWrite(LED, HIGH);
          led_off_time = millis() + LED_ON_MS;
          size_t len = Serial2.available();
          uint8_t sbuf[len];
          Serial2.readBytes(sbuf, len);
          if (conn_mode == MODE_TELNET) {
            telnet_write(sbuf, len);
          } else {
            client.write(sbuf, len);
          }
          last_rx = sbuf[len - 1];
        }
        update_traffic_display();
      }
    }
  } else {
    Serial.printf("WiFi not connected: %s\n", wifistatus(WiFi.status()));
    if (client) {
      client.stop();
      client = 0;
    }
    delay(1000);
  }
  if (led_off_time && millis() >= led_off_time) {
    digitalWrite(LED, LOW);
    led_off_time = 0;
  }
}
