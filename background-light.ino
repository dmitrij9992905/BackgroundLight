
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <FS.h>   //Include File System Headers
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <stdio.h>

#define DOOR_PIN 14 // D5 WeMos (пин открывания двери)
#define IGN_PIN 12 // D6 WeMos (пин включения зажигания или габаритов)
#define ARM_PIN 13 // D7 WeMos (вход статуса сигнализации)


const int iterations = 30; // Количество итераций (чем больше - тем плавнее)
boolean fast_fade = false; // Скорость переливания цвета

extern "C" {
    #include "gpio.h"
    #include "user_interface.h"
    #include <ets_sys.h>
}

const int leds = 2; // Количество светодиодов
const int led_pin = 5; // D1 WeMos (выход на WS2812)
const int reset_pin = 4; // D2 WeMos (вход кнопки Reset)

// Конфигурация WiFi соединения
//char *ap_ssid = "bl_server_777";
//char *ap_pass = "89076123";
char ap_ssid[16];
char ap_pass[16];
const char *mdns_name = "background-light";

//Запуск веб-сервера на порту 80 (стандартный HTTP-порт)
ESP8266WebServer server(80);

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(leds, led_pin, NEO_GRB + NEO_KHZ800);

DNSServer dnsServer;
const byte DNS_PORT = 53;

boolean wifi_up;

uint32_t color, old_color;

unsigned long previousMillis;
unsigned long currentMillis;

uint8_t iter = 0;
unsigned long int fadeMillis = 0;
unsigned int r;
unsigned int g;
unsigned int b;

unsigned int r_old = 0;
unsigned int g_old = 0;
unsigned int b_old = 0;

unsigned int greet_r;
unsigned int greet_g;
unsigned int greet_b;

unsigned int bg_r;
unsigned int bg_g;
unsigned int bg_b;

unsigned int warn_r;
unsigned int warn_g;
unsigned int warn_b;

unsigned int r_fbg = 0;
unsigned int g_fbg = 0;
unsigned int b_fbg = 0;

unsigned int event = 0;

int8_t fbg_min = 0;
int8_t fbg_sec = 0;
int8_t fbg_min_set = 0;


boolean fbg_state = false;
boolean warn_light = false;
boolean bg_light = false;
boolean greet_light = false;

boolean fbg_event = false;
boolean warn_event = false;
boolean bg_event = false;
boolean greet_event = false;
boolean reboot_required = false;
boolean fading = false;
boolean faded = false;

void resetAllEvents() {
  fbg_event = false;
  warn_event = false;
  bg_event = false;
  greet_event = false;
  event = 0;
}

void returnFactory() {
  greet_r = 255;
  greet_g = 255;
  greet_b = 255;
  bg_r = 0;
  bg_g = 255;
  bg_b = 48;
  warn_r = 255;
  warn_g = 0;
  warn_b = 0;
  warn_light = true;
  bg_light = true;
  greet_light = true;
  strcpy(ap_ssid, "bl_server_777");
  strcpy(ap_pass, "89076123");
}



void writeSettings() {
  StaticJsonBuffer<480> settingsJson;
  JsonObject& sets = settingsJson.createObject();
  sets["greet_r"] = greet_r;
  sets["greet_g"] = greet_g;
  sets["greet_b"] = greet_b;
  sets["bg_r"] = bg_r;
  sets["bg_g"] = bg_g;
  sets["bg_b"] = bg_b;
  sets["warn_r"] = warn_r;
  sets["warn_g"] = warn_g;
  sets["warn_b"] = warn_b;

  sets["ap_ssid"] = ap_ssid;
  sets["ap_pass"] = ap_pass;
  
  if (warn_light) sets["warn_light"] = true;
  else sets["warn_light"] = false;
  
  if (bg_light) sets["bg_light"] = true;
  else sets["bg_light"] = false;
  
  if(greet_light) sets["greet_light"] = true;
  else sets["greet_light"] = false;
  File setsFile = SPIFFS.open("/settings.json", "w+");
  String setsOut;
  sets.prettyPrintTo(Serial);
  sets.printTo(setsOut);
  setsFile.print(setsOut);
  setsFile.close();
  Serial.println("Settings written!");
}

void readSettings() {
  StaticJsonBuffer<480> settingsJson;
  //String apSSIDstr;
  //String apPassStr;
  //char buf[16];
  if(!SPIFFS.exists("/settings.json")) {
    Serial.println("Here is no settings file, resetting to factory...");
    returnFactory();
    writeSettings();
  }
  else {
    File setsFile = SPIFFS.open("/settings.json", "r");
    char fileBuf[480];
    for(int i=0;i<setsFile.size();i++) //Read upto complete file size
    {
      fileBuf[i] = (char)setsFile.read();
    }
    setsFile.close();
    JsonObject& sets = settingsJson.parseObject(fileBuf);
    greet_r = sets["greet_r"];
    greet_g = sets["greet_g"];
    greet_b = sets["greet_b"];
    
    bg_r = sets["bg_r"];
    bg_g = sets["bg_g"];
    bg_b = sets["bg_b"];
    
    warn_r = sets["warn_r"];
    warn_g = sets["warn_g"];
    warn_b = sets["warn_b"];
    
    warn_light = sets["warn_light"];
    bg_light = sets["bg_light"];
    greet_light = sets["greet_light"];

    strcpy(ap_ssid, sets["ap_ssid"]);
    strcpy(ap_pass, sets["ap_pass"]);

    //apSSIDstr.toCharArray(buf, 16);
    //memcpy(ap_ssid, buf, 16);
    sets.printTo(Serial);
    Serial.println("Settings read!");
  }
}

void sleep(){
    Serial.println("Going to sleep...");
    int i = 0;
    while(i < 20) {
      i++;
      delay(200);
    }
    ESP.deepSleep(20e6);
}

void handleFileList() {
  DynamicJsonBuffer  jsonBuffer(1000);
  JsonObject& file_list = jsonBuffer.createObject();
  FSInfo fs_info;
  SPIFFS.info(fs_info);
  IPAddress ip = WiFi.localIP();
  Serial.print("Used memory: ");
  Serial.println(fs_info.usedBytes);
  Serial.print("Total memory: ");
  Serial.println(fs_info.totalBytes);
  file_list["used_bytes"] = fs_info.usedBytes;
  file_list["total_bytes"] = fs_info.totalBytes;
  file_list["server_ip"] = String(ip[0])+'.'+String(ip[1])+'.'+String(ip[2])+'.'+String(ip[3]);
  JsonArray& files = file_list.createNestedArray("files");
  JsonArray& file_sizes = file_list.createNestedArray("file_sizes");
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
      files.add(dir.fileName());
      File f = dir.openFile("r");
      file_sizes.add(f.size());
  }
  String ans; 
  file_list.printTo(ans);  // Перевод объекта JSON в строку для выдачи ответа клиенту
  server.send(200, "application/json", ans); // Выдача ответа
}

void handleFiles() {
  server.sendHeader("Location", "/files.html",true); 
  server.send(302, "text/plane","");  // Выдача ответа
}

void handleChangeWifiCredentials() {
  if ((server.argName(0) != "ssid") || (server.argName(1) != "pwd")) {
    server.send(500, "text/plain", "BAD ARGS");
    return;  
  }
  String apSSIDstr = server.arg(0);
  String apPassStr = server.arg(1);
  char buf[16];
  apSSIDstr.toCharArray(buf, 16);
  for (int i = 0; i < sizeof(ap_ssid); i++) ap_ssid[i] = '\0';
  strcpy(ap_ssid, buf);
  Serial.println(ap_ssid);
  for (int i = 0; i < 16; i++) buf[i] = '\0';
  apPassStr.toCharArray(buf, 16);
  for (int i = 0; i < sizeof(ap_pass); i++) ap_pass[i] = '\0';
  strcpy(ap_pass, buf);
  for (int i = 0; i < 16; i++) buf[i] = '\0';
  Serial.println(ap_pass);
  writeSettings();
  server.send(200, "text/plain", "OK!");
  reboot_required = true;
  Serial.println("WiFi settings set: "+apSSIDstr+' '+apPassStr);
}

void getRebootReq() {
  if (reboot_required) server.send(200, "text/plain", "1");
  else server.send(200, "text/plain", "0");  
}

void getReboot() {
  server.send(200, "text/plain", "Rebooting...");
  int i = 0;
  while(i < 20) {
    i++;
    delay(200);
  }
  ESP.deepSleep(2e6);  
}

bool handleFileRead(String path) {
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) {
    path += "index.htm";
  }
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz)) {
      path += ".gz";
    }
    File file = SPIFFS.open(path, "r");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}
 
void handleRoot(){ // Выдача главной страницы HTML
  server.sendHeader("Location", "/index.html",true); 
  server.send(302, "text/plane","");  // Выдача ответа
}

void getFBGState() {
  StaticJsonBuffer<88> jsonBuffer;
  JsonObject& out = jsonBuffer.createObject();
  if (fbg_state) out["en"] = 1;
  else out["en"] = 0;
  out["mr"] = fbg_min;
  out["r"] = r_fbg;
  out["g"] = g_fbg;
  out["b"] = b_fbg;
  String ans;
  out.printTo(ans);
  out.prettyPrintTo(Serial);
  server.send(200, "application/json", ans);  
}

void setFBG() {
  String clr = server.arg(0);
  Serial.println(clr);
  //clr.replace('#', '\0');
  char clrBuf[8];
  clr.toCharArray(clrBuf, 8);
  sscanf(clrBuf, "#%2x%2x%2x", &r_fbg, &g_fbg, &b_fbg);
  if (server.arg(1) == "true") fbg_state = true;
  else fbg_state = false;
  String minsStr = server.arg(2);
  fbg_min_set = minsStr.toInt();
  Serial.println("Color: "+clr+" Mins: "+minsStr);
  server.send(200, "text/plain", "OK");
} 

void handleWebRequests(){ // При запросе других страниц
  if(loadFromSpiffs(server.uri())) return;
  String message = "File Not Detected\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " NAME:"+server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);  // Выдача ответа
  Serial.println(message);
}

void handleSetColor(uint8_t type) {
  String clr = server.arg(0);
  
  Serial.println(clr);
  //clr.replace('#', '\0');
  char clrBuf[8];
  clr.toCharArray(clrBuf, 8);
  switch(type) {
    case 0:
      if (server.arg(1) == "true") greet_light = true;
      else greet_light = false;
      sscanf(clrBuf, "#%2x%2x%2x", &greet_r, &greet_g, &greet_b);
      break;
    case 1:
      if (server.arg(1) == "true") bg_light = true;
      else bg_light = false;
      sscanf(clrBuf, "#%2x%2x%2x", &bg_r, &bg_g, &bg_b);
      break;
    case 2:
      if (server.arg(1) == "true") warn_light = true;
      else warn_light = false;
      sscanf(clrBuf, "#%2x%2x%2x", &warn_r, &warn_g, &warn_b);
      break;
    default:
      return;
  }
  //
  Serial.println("Color set: "+clr);
  Serial.println("Checkbox: "+server.arg(1));
  server.send(200, "text/plain", "OK");
  writeSettings();
}

void handleGetColor(uint8_t type) {
  StaticJsonBuffer<88> jsonBuffer;
  JsonObject& out = jsonBuffer.createObject();
  switch(type) {
    case 0:
      out["r"] = greet_r;
      out["g"] = greet_g;
      out["b"] = greet_b;
      if (greet_light) out["en"] = 1;
      else out["en"] = 0;
      break;

    case 1:
      out["r"] = bg_r;
      out["g"] = bg_g;
      out["b"] = bg_b;
      if (bg_light) out["en"] = 1;
      else out["en"] = 0;
      break;

    case 2:
      out["r"] = warn_r;
      out["g"] = warn_g;
      out["b"] = warn_b;
      if (warn_light) out["en"] = 1;
      else out["en"] = 0;
      break;

    default:
      return;
  }
   
  String ans;
  out.printTo(ans);
  server.send(200, "application/json", ans);  
}

void fileLoadForm() {
  server.sendHeader("Location", "/files.html",true); 
  server.send(302, "text/plane","");  // Выдача ответа
}

void wifiUp() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_pass); // Подключение к сети WIFI
  Serial.println(ap_ssid);
}

void wifiDown() {
  WiFi.mode(WIFI_OFF);
}

// Функция плавного изменения цвета без задержек (количество итераций - 15)
void ledFading(unsigned int r_old, unsigned int g_old, unsigned int b_old, unsigned int r, unsigned int g,unsigned int b, unsigned int n) {
  /*
   * r_old, g_old, b_old - старый цвет, из которого делается перелив
   * r,g,b - новый цвет
   * n - номер итерации
   * */
  if (n > iterations) return; // Функция не выполняется, если n больше 15
  unsigned int r_mid, g_mid, b_mid; // Промежуточный цвет
  r_mid = map(n, 0, iterations, r_old, r);  
  g_mid = map(n, 0, iterations, g_old, g); 
  b_mid = map(n, 0, iterations, b_old, b); 
  setLEDColorRGB(r_mid,g_mid,b_mid);
}
 
void setup() {
  Serial.begin(115200); // Инициализация порта
  Serial.println();
  Serial.println("Starting...");

  pinMode(DOOR_PIN, INPUT_PULLUP);
  pinMode(IGN_PIN, INPUT_PULLUP);
  pinMode(ARM_PIN, INPUT_PULLUP);

  previousMillis = 0;

  if (digitalRead(ARM_PIN) != 0) sleep();
  
  SPIFFS.begin(); // Инициализация файловой системы SPIFFS
  Serial.println("File System Initialized");
  readSettings();
  wifiUp();
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  //Serial.print("Station MAC: ");  // MAC адрес точки доступа
  dnsServer.start(DNS_PORT, "http://bg-light.ru", myIP);
  wifi_up = true;
  //  Запуск веб-сервера
  server.on("/",handleRoot);  // Обработчик запроса домашней страницы
  server.on("/file_list", handleFileList);
  server.on("/edit", HTTP_GET, []() {
    server.send(404, "text/plain", "FileNotFound");
  });
  server.on("/set_greet_color", HTTP_POST, []() {
    handleSetColor(0);
  });
  server.on("/set_bg_color", HTTP_POST, []() {
    handleSetColor(1);
  });
  server.on("/set_warn_color", HTTP_POST, []() {
    handleSetColor(2);
  });
  server.on("/get_greet_color", []() {
    handleGetColor(0);
  });
  server.on("/get_bg_color", []() {
    handleGetColor(1);
  });
  server.on("/get_warn_color", []() {
    handleGetColor(2);
  });
  server.on("/get_fbg_state", getFBGState);
  server.on("/set_fbg", HTTP_POST, []() {
    setFBG();
  });
  server.on("/change_credentials", HTTP_POST, handleChangeWifiCredentials);
  server.on("/reboot_req", getRebootReq);
  server.on("/reboot", getReboot);

  server.onNotFound(handleWebRequests); //  Обработчик запроса несуществующей страницы
  server.begin(); // Запуск сервера

  pixels.begin();
  setLEDColorRGB(0,0,0);
  //sleep();
}



void loop() {
  
  dnsServer.processNextRequest();
  server.handleClient(); //  Обработка запросов от клиентов
  if ((digitalRead(ARM_PIN) != 0)) {
    setLEDColorRGB(0,0,0);
    //wifiDown();
    //wifi_up = false;
    sleep();
    //ESP.deepSleep(2e6);
    Serial.println("sleep...");
  }
  else {
    //if (!wifi_up) wifiUp();
    
    if ((digitalRead(DOOR_PIN) == 0) && !(digitalRead(IGN_PIN) == 0)) {
      //Serial.println("Greeting event");
      fast_fade = false;
      if (fbg_state) fbg_state = false;
      if (greet_light) event = 1;
      else event = 0;
      if (r != r_old || g != g_old || b != b_old) fading = true;
    }
    else if (!(digitalRead(DOOR_PIN) == 0) && (digitalRead(IGN_PIN) == 0)) {
      //Serial.println("Background event");
      fast_fade = false;
      if (fbg_state) fbg_state = false;
      if (bg_light) event = 2;
      else event = 0;
      if (r != r_old || g != g_old || b != b_old) fading = true;
    }
    else if ((digitalRead(DOOR_PIN) == 0) && (digitalRead(IGN_PIN) == 0)) {
      //Serial.println("Warning event");
      fast_fade = true;
      if (fbg_state) fbg_state = false;
      if (warn_light) event = 3;
      else event = 0;
      if (r != r_old || g != g_old || b != b_old) fading = true;
    }
    else if ((event != 4) && fbg_state) {
      //Serial.println("Force background ran");
      fast_fade = false;
      event = 4;  
      fbg_min = fbg_min_set;
      //if (r != r_old || g != g_old || b != b_old) fading = true;
    }
    else if ((event == 4) && !fbg_state) {
      //Serial.println("Force background off");
      fast_fade = false;
      fbg_min = 0;
      fbg_sec = 0;
      event = 0; 
      //if (iter != 0) iter = 0;
    }
    
    
    else if ((event != 4) && (!fbg_state)) {
      //Serial.println("Switching off");
      fast_fade = false;
      event = 0;
      /*
      //faded = false;
      if (fading) {
        r_old = r;
        g_old = g;
        b_old = b;
        iter = 0;
        fading = false;
        
      }
      */
      //if (iter != 0) iter = 0;
    }
    //if (!((digitalRead(DOOR_PIN) == 0) && (digitalRead(IGN_PIN) == 0))) if ((iter != 0) && (iter < iterations)) iter = 0;
  }

  if (fbg_state && fbg_min_set != 0) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= 1000) {
      previousMillis = currentMillis;
      fbg_sec--;
      if (fbg_sec < 0) {
        fbg_min--;
        fbg_sec = 59;
        if (fbg_min < 0) {
          fbg_min = 0;
          fbg_sec = 0;
          fbg_state = false;
        }
      }
      Serial.println("Force background time: "+String(fbg_min)+":"+String(fbg_sec));
    }
    
  }

  switch(event) {
    case 0:
      r = 0;
      g = 0;
      b = 0;
      break;
    case 1:
      r = greet_r;
      g = greet_g;
      b = greet_b;
      break;
    case 2:
      r = bg_r;
      g = bg_g;
      b = bg_b;
      break;
    case 3:
      r = warn_r;
      g = warn_g;
      b = warn_b;
      break;
    case 4:
      r = r_fbg;
      g = g_fbg;
      b = b_fbg;
      break;
    default:
      r = 0;
      g = 0;
      b = 0;
      break;
  }
  
  if (r != r_old || g != g_old || b != b_old) {
//      fadeMillis = millis();
//      if (millis())
    uint8_t iter_dur;
    if (fast_fade) iter_dur = 8;
    else iter_dur = 30;
    if (millis() > fadeMillis+iter_dur) {
      ledFading(r_old, g_old, b_old, r,g,b, iter);
      iter++;
      fadeMillis = millis();
      if (iter > iterations) {
        fadeMillis = 0;
        iter = 0;
        r_old = r;
        g_old = g;
        b_old = b;
        //faded = true;
      }
    }
  }
}

// Выдача файлов из памяти
bool loadFromSpiffs(String path){
  String dataType = "text/plain";
  if(path.endsWith("/")) path += "index.htm";
  if(path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
  else if(path.endsWith(".html")) dataType = "text/html";
  else if(path.endsWith(".htm")) dataType = "text/html";
  else if(path.endsWith(".css")) dataType = "text/css";
  else if(path.endsWith(".js")) dataType = "application/javascript";
  else if(path.endsWith(".png")) dataType = "image/png";
  else if(path.endsWith(".gif")) dataType = "image/gif";
  else if(path.endsWith(".jpg")) dataType = "image/jpeg";
  else if(path.endsWith(".ico")) dataType = "image/x-icon";
  else if(path.endsWith(".xml")) dataType = "text/xml";
  else if(path.endsWith(".pdf")) dataType = "application/pdf";
  else if(path.endsWith(".zip")) dataType = "application/zip";
  else if (path.endsWith(".gz")) dataType = "application/x-gzip";
  File dataFile = SPIFFS.open(path.c_str(), "r");
  if (server.hasArg("download")) dataType = "application/octet-stream";
  if (server.streamFile(dataFile, dataType) != dataFile.size()) {
  }
  dataFile.close();
  return true;
}

String getContentType(String filename) {
  if (server.hasArg("download")) {
    return "application/octet-stream";
  } else if (filename.endsWith(".htm")) {
    return "text/html";
  } else if (filename.endsWith(".html")) {
    return "text/html";
  } else if (filename.endsWith(".css")) {
    return "text/css";
  } else if (filename.endsWith(".js")) {
    return "application/javascript";
  } else if (filename.endsWith(".png")) {
    return "image/png";
  } else if (filename.endsWith(".gif")) {
    return "image/gif";
  } else if (filename.endsWith(".jpg")) {
    return "image/jpeg";
  } else if (filename.endsWith(".ico")) {
    return "image/x-icon";
  } else if (filename.endsWith(".xml")) {
    return "text/xml";
  } else if (filename.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (filename.endsWith(".zip")) {
    return "application/x-zip";
  } else if (filename.endsWith(".gz")) {
    return "application/x-gzip";
  }
  return "text/plain";
}

void setLEDColor (uint32_t new_color) {
  if (leds == 1) pixels.setPixelColor(0, new_color);
  else {
    for (int i = 0; i < leds; i++) pixels.setPixelColor(i, new_color);
  }
  pixels.show();
}

void setLEDColorRGB (uint8_t r, uint8_t g, uint8_t b) {
  if (leds == 1) pixels.setPixelColor(0, pixels.Color(r,g,b));
  else {
    for (int i = 0; i < leds; i++) pixels.setPixelColor(i, pixels.Color(r,g,b));
  }
  pixels.show();
}
