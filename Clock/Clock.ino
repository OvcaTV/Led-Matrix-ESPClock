#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

#include <PxMatrix.h>
#include <Ticker.h>
#include <Fonts/FreeMono9pt7b.h>


#include <ESP8266WebServer.h>

ESP8266WebServer server(80);

const char* SSID     = "11.6. 1989 se nic nestalo";
const char* PASSWORD = "11111111";

//const char* SSID     = "Moravkovi";
//const char* PASSWORD = "sit4morav";

WiFiUDP     ntpUDP;
NTPClient   timeClient(ntpUDP, "pool.ntp.org", 0, 1000); 
int tzOffsetHours = 2;  // default UTC+2 (CEST)

#define P_LAT 16 //nodemcu pin D0
#define P_A 5    //nodemcu pin D1
#define P_B 4    //nodemcu pin D2
#define P_C 15   //nodemcu pin D8
#define P_OE 2   //nodemcu pin D4
#define P_D 12   //nodemcu pin D6
#define P_E 0    //nodemcu ground

const uint8_t DIGIT_SP = 8;   // mezera po cislech
const uint8_t COLON_SP = 6;   // mezera po dvojtecce
const uint8_t FONT_H = 11;    // velikost fontu

PxMATRIX display(64, 32, P_LAT, P_OE, P_A, P_B, P_C, P_D);
Ticker display_ticker;
Ticker blink_ticker;

bool blinkOn = true;

bool alarmEnabled = false;
int alarmHour   = 12;
int alarmMinute = 0;
#define MAX_MSG_LEN 32
char alarmMessage[MAX_MSG_LEN+1] = "Budicek";

// ———— FORWARD DECLS ————
void display_updater();
void toggleBlink();
void showTime();
void checkAlarm();
void triggerAlarm();
void handleRoot();
void handleSetTZ();
void handleSetAlarm();
void handleToggleAlarm();
void handleSetMessage();


void display_updater() {
  display.display(70);
}

void toggleBlink() {
  blinkOn = !blinkOn;
  showTime();
}

void showTime() {
  int hh = timeClient.getHours();
  int mm = timeClient.getMinutes();
  int ss = timeClient.getSeconds();

  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", hh, mm, ss);

  int totalW = 0;
  for (char* p = buf; *p; ++p)
    totalW += (*p==':')? COLON_SP : DIGIT_SP;

  int x = (64 - totalW)/2 - 2; if (x<0) x=0;
  int y = (32 - FONT_H)/2 + FONT_H;

  display.clearDisplay();
  display.setTextColor(display.color565(255,0,0));
  display.setFont(&FreeMono9pt7b);

  for (char* p = buf; *p; ++p) {
    if (*p == ':') {
      if (blinkOn)
        display.drawChar(x, y, ':', display.color565(255,0,0),0,1);
      x += COLON_SP;
    } else {
      display.drawChar(x, y, *p, display.color565(255,0,0),0,1);
      x += DIGIT_SP;
    }
  }
  display.showBuffer();
}

// ———— ALARM ————

void checkAlarm() {
  static int lastSec=-1;
  int ss = timeClient.getSeconds();
  if (ss==lastSec) return;
  lastSec=ss;
  if (alarmEnabled &&
      timeClient.getHours()==alarmHour &&
      timeClient.getMinutes()==alarmMinute &&
      ss==0) {
    triggerAlarm();
  }
}

void triggerAlarm() {
  Serial.println("🚨 ALARM!");

  // pause blink only
  blink_ticker.detach();

  // scroll message
  int charW = 6;
  int textW = strlen(alarmMessage)*charW;
  int y = (32 - FONT_H)/2 + FONT_H;
  for (int xpos=64; xpos>=-textW; --xpos) {
    display.clearDisplay();
    display.setTextColor(display.color565(255,0,0));
    display.setFont(&FreeMono9pt7b);
    display.setCursor(xpos, y);
    display.print(alarmMessage);
    display.showBuffer();
    delay(50);
  }

  // resume
  showTime();
  blink_ticker.attach_ms(500, toggleBlink);
}

// ———— HTTP HANDLERS ————

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>ESP Clock</title></head><body>"
    "<h1>ESP8266 Clock</h1>"
    "<p>Time: <strong>" + timeClient.getFormattedTime() + 
    "</strong> (UTC" + (tzOffsetHours>=0?"+":"") + String(tzOffsetHours) + ")</p>"

    // timezone
    "<form action='/set_timezone' method='POST'>"
      "UTC Offset: <input type='number' name='tz' value='" + String(tzOffsetHours) +
      "' step='1' min='-12' max='14'>"
      "<input type='submit' value='Set TZ'></form><br>"

    // alarm time
    "<form action='/set_alarm' method='POST'>"
      "Alarm HH:MM: <input type='time' name='alarm' value='" +
      String(alarmHour<10?"0":"")+alarmHour+":"+
      String(alarmMinute<10?"0":"")+alarmMinute + "'>"
      "<input type='submit' value='Set Alarm'></form>"
    "<form action='/toggle_alarm' method='POST'>"
      "<input type='submit' value='" + String(alarmEnabled?"Disable":"Enable")+
      " Alarm'></form>"
    "<p>Alarm is <strong>" + String(alarmEnabled?"ON":"OFF") +
    "</strong> at " + String(alarmHour<10?"0":"")+alarmHour+":"+
    String(alarmMinute<10?"0":"")+alarmMinute + "</p>"

    // alarm message
    "<form action='/set_message' method='POST'>"
      "Message: <input type='text' name='msg' maxlength='" + String(MAX_MSG_LEN) +
      "' value='" + String(alarmMessage) + "'>"
      "<input type='submit' value='Set Message'></form>"
    "<p>Current msg: <strong>" + String(alarmMessage) + "</strong></p>"

    "</body></html>";

  server.send(200, "text/html", html);
}

void handleSetTZ() {
  if (server.hasArg("tz")) {
    int v = server.arg("tz").toInt();
    v = constrain(v, -12, 14);
    tzOffsetHours = v;
    timeClient.setTimeOffset(v * 3600);
    Serial.printf("TZ -> UTC%+d\n", v);
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleSetAlarm() {
  if (server.hasArg("alarm")) {
    String t = server.arg("alarm");  // format "HH:MM"
    alarmHour   = t.substring(0, 2).toInt();
    alarmMinute = t.substring(3)   .toInt();  // take from index 3 to end
    Serial.printf("Alarm -> %02d:%02d\n", alarmHour, alarmMinute);
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleToggleAlarm() {
  alarmEnabled = !alarmEnabled;
  Serial.printf("Alarm %s\n", alarmEnabled?"ENABLED":"DISABLED");
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void handleSetMessage() {
  if (server.hasArg("msg")) {
    String m = server.arg("msg");
    m.replace("\n",""); 
    m.toCharArray(alarmMessage, MAX_MSG_LEN+1);
    Serial.printf("Msg -> %s\n", alarmMessage);
  }
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void displayIP() {
  // 1) Grab your IP as a C-string
  char buf[24];
  WiFi.localIP().toString().toCharArray(buf, sizeof(buf));

  // use default font, 5×7 px
  display.setFont();              // reset to default
  display.setTextSize(1);         // no scaling
  display.setTextColor(display.color565(0,255,0));

  // measure its real width
  int16_t x1, y1;
  uint16_t w, h;
  int16_t y0 = (32 - 7)/2 + 7;     // 7 px font-height
  display.getTextBounds(buf, 0, y0, &x1, &y1, &w, &h);

  // scroll from x=64 → x=–w
  for (int x = 64; x >= -int(w); --x) {
    display.clearDisplay();
    display.setCursor(x, y0);
    display.print(buf);
    display.showBuffer();
    delay(50);
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\nBooting…");

  // Matrix init (start dark)
  display.begin(16);
  display.setFastUpdate(true);
  display.setBrightness(0);
  display.clearDisplay();
  display.showBuffer();
  display_ticker.attach_ms(25, display_updater);

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  Serial.print("Connecting Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250); Serial.print('.');
  }
  Serial.println();
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  
  display.setBrightness(80);
  delay(500);

  displayIP();               // <<< scrolls the IP once on your matrix
  delay(500);
  

  // NTP init
  timeClient.begin();
  timeClient.setTimeOffset(tzOffsetHours * 3600);
  while (!timeClient.update()) {
    timeClient.forceUpdate();
    delay(200);
  }
  showTime();

  // Blink colon
  blink_ticker.attach_ms(500, toggleBlink);

  // Web routes
  server.on("/",        HTTP_GET,  handleRoot);
  server.on("/set_timezone", HTTP_POST, handleSetTZ);
  server.on("/set_alarm",    HTTP_POST, handleSetAlarm);
  server.on("/toggle_alarm", HTTP_POST, handleToggleAlarm);
  server.on("/set_message",  HTTP_POST, handleSetMessage);
  server.onNotFound([](){
    server.send(404, "text/plain", "Not Found");
  });
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  timeClient.update();
  checkAlarm();
}