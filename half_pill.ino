#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <time.h>

// ========== BOOT BITMAP (separate file) ==========
#include "boot_img.h"

// ========== WiFi ==========
const char* ssid     = "";
const char* password = "";

// ========== Display & Touch ==========
#define SCREEN_WIDTH   240
#define SCREEN_HEIGHT  240
#define CHSC6X_I2C_ID  0x2e
#define CHSC6X_READ_POINT_LEN 5
#define TOUCH_INT      D7

TFT_eSPI tft = TFT_eSPI();

// ========== NTP & Timezone (IST = UTC+5:30) ==========
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
int timezoneMinutes = 330;   // UTC+5:30

// ========== EEPROM ==========
#define EEPROM_SIZE     512
#define EEPROM_PILL_COUNT_ADDR 0
#define EEPROM_PILL_DATA_ADDR  4
#define EEPROM_TZ_ADDR         200
#define MAX_PILLS       20

struct Pill {
  char name[24];
  int  hour;
  int  minute;
  bool active;
  bool takenToday;
};
Pill pills[MAX_PILLS];
int pillCount = 0;

// ========== Global State ==========
WebServer server(80);
String localIP = "";
bool showingIP = true;
unsigned long ipShowStart = 0;
bool reminderActive = false;
int reminderIndex = -1;

// Clock update – no flicker
String lastTimeStr = "";
int lastTimeX = 0, lastTimeY = 95;
bool clockInitialised = false;

// ========== Touch Functions ==========
void round_display_touch_init(void) {
  pinMode(TOUCH_INT, INPUT_PULLUP);
  Wire.begin();
}

bool chsc6x_is_pressed(void) {
  if(digitalRead(TOUCH_INT) != LOW) {
    delay(1);
    if(digitalRead(TOUCH_INT) != LOW) return false;
  }
  return true;
}

bool chsc6x_get_xy(int32_t &x, int32_t &y) {
  uint8_t temp[CHSC6X_READ_POINT_LEN] = {0};
  Wire.requestFrom(CHSC6X_I2C_ID, CHSC6X_READ_POINT_LEN);
  if (Wire.available() == CHSC6X_READ_POINT_LEN) {
    for(int i=0; i<CHSC6X_READ_POINT_LEN; i++) temp[i] = Wire.read();
    if (temp[0] == 0x01) {
      int32_t raw_x = temp[2];
      int32_t raw_y = temp[4];
      uint8_t rotation = tft.getRotation();
      switch(rotation) {
        case 0:  x = raw_x;       y = raw_y;                break;
        case 1:  x = raw_y;       y = SCREEN_WIDTH-raw_x;   break;
        case 2:  x = SCREEN_WIDTH-raw_x; y = SCREEN_HEIGHT-raw_y; break;
        case 3:  x = SCREEN_HEIGHT-raw_y; y = raw_x;        break;
      }
      return true;
    }
  }
  return false;
}

// ========== EEPROM Helpers ==========
void savePills() {
  EEPROM.writeInt(EEPROM_PILL_COUNT_ADDR, pillCount);
  for (int i = 0; i < pillCount; i++) {
    int addr = EEPROM_PILL_DATA_ADDR + i * sizeof(Pill);
    EEPROM.put(addr, pills[i]);
  }
  EEPROM.commit();
}

void loadPills() {
  pillCount = EEPROM.readInt(EEPROM_PILL_COUNT_ADDR);
  if (pillCount < 0 || pillCount > MAX_PILLS) pillCount = 0;
  for (int i = 0; i < pillCount; i++) {
    int addr = EEPROM_PILL_DATA_ADDR + i * sizeof(Pill);
    EEPROM.get(addr, pills[i]);
  }
}

void saveTimezone() {
  EEPROM.writeInt(EEPROM_TZ_ADDR, timezoneMinutes);
  EEPROM.commit();
}

void loadTimezone() {
  timezoneMinutes = EEPROM.readInt(EEPROM_TZ_ADDR);
  if (timezoneMinutes < -720 || timezoneMinutes > 840) timezoneMinutes = 330;
}

// ========== Time Formatting ==========
String get12HourTimeNoAmPm() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "--:--";
  char buffer[6];
  int hour = timeinfo.tm_hour % 12;
  if (hour == 0) hour = 12;
  sprintf(buffer, "%d:%02d", hour, timeinfo.tm_min);
  return String(buffer);
}

String format12HourAmPm(int hour, int minute) {
  int dispHour = hour % 12;
  if (dispHour == 0) dispHour = 12;
  char buffer[10];
  const char* ampm = hour >= 12 ? "pm" : "am";
  sprintf(buffer, "%d:%02d%s", dispHour, minute, ampm);
  return String(buffer);
}

void syncTime() {
  configTime(timezoneMinutes * 60, 0, ntpServer1, ntpServer2);
  struct tm timeinfo;
  int retry = 0;
  while (!getLocalTime(&timeinfo) && retry < 20) {
    delay(500);
    retry++;
  }
}

// ========== Pill Management ==========
int findNextReminderIndex() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return -1;
  int nowMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  
  int bestIdx = -1;
  int bestTime = 24*60;
  for (int i = 0; i < pillCount; i++) {
    if (!pills[i].active || pills[i].takenToday) continue;
    int pillMinutes = pills[i].hour * 60 + pills[i].minute;
    if (pillMinutes >= nowMinutes && pillMinutes < bestTime) {
      bestTime = pillMinutes;
      bestIdx = i;
    }
  }
  if (bestIdx == -1) {
    for (int i = 0; i < pillCount; i++) {
      if (pills[i].active && !pills[i].takenToday) {
        bestIdx = i;
        break;
      }
    }
  }
  return bestIdx;
}

void checkReminders() {
  if (reminderActive) return;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  int nowMinutes = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  
  for (int i = 0; i < pillCount; i++) {
    if (!pills[i].active || pills[i].takenToday) continue;
    int pillMinutes = pills[i].hour * 60 + pills[i].minute;
    if (pillMinutes == nowMinutes) {
      reminderActive = true;
      reminderIndex = i;
      drawReminderScreen(i);
      break;
    }
  }
}

// ========== CLOCK SCREEN ==========
void drawClockScreen() {
  tft.fillScreen(TFT_BLACK);
  
  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  String timeStr = get12HourTimeNoAmPm();
  int timeWidth = tft.textWidth(timeStr);
  lastTimeX = (SCREEN_WIDTH - timeWidth) / 2;
  lastTimeY = 95;
  tft.drawString(timeStr, lastTimeX, lastTimeY);
  lastTimeStr = timeStr;
  
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  int nextIdx = findNextReminderIndex();
  if (nextIdx >= 0) {
    String line1 = "Next : " + String(pills[nextIdx].name);
    int w1 = tft.textWidth(line1);
    tft.drawString(line1, (SCREEN_WIDTH - w1) / 2, 160);
    
    String timeAmPm = format12HourAmPm(pills[nextIdx].hour, pills[nextIdx].minute);
    int w2 = tft.textWidth(timeAmPm);
    tft.drawString(timeAmPm, (SCREEN_WIDTH - w2) / 2, 190);
  } else {
    String noRem = "No reminders";
    int w = tft.textWidth(noRem);
    tft.drawString(noRem, (SCREEN_WIDTH - w) / 2, 175);
  }
  
  clockInitialised = true;
}

void updateClockTime() {
  if (!clockInitialised) return;
  
  String newTime = get12HourTimeNoAmPm();
  if (newTime == lastTimeStr) return;
  
  tft.setFreeFont(&FreeSansBold24pt7b);
  int w = tft.textWidth(lastTimeStr);
  int h = tft.fontHeight();
  tft.fillRect(lastTimeX, lastTimeY - 5, w, h + 5, TFT_BLACK);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(newTime, lastTimeX, lastTimeY);
  
  lastTimeStr = newTime;
}

// ========== REMINDER SCREEN ==========
void drawReminderScreen(int idx) {
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  
  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.drawString("Pill Time", 22, 88);
  
  tft.setFreeFont(&FreeSans12pt7b);
  String pillName = String(pills[idx].name);
  int nameWidth = tft.textWidth(pillName);
  int nameX = (SCREEN_WIDTH - nameWidth) / 2;
  tft.drawString(pillName, nameX, 136);
  
  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString("Tap to take", 75, 204);
}

void drawPillTakenScreen() {
  tft.fillScreen(TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.setFreeFont(&FreeSans18pt7b);
  tft.drawString("Pill taken!", 30, 100);
  delay(1500);
}

// ========== BOOT BITMAP – YOUR FULL-SCREEN LOGO ==========
void drawBootBitmap() {
  tft.pushImage(0, 0, 240, 240, (uint16_t*)image_bitmap_pixels);
  delay(1500);
}

// ========== WEB UI ==========
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
  <title>Pill Reminder</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;
      background: linear-gradient(145deg, #1e2b3a 0%, #0f172a 100%);
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 16px;
    }
    .card {
      max-width: 480px;
      width: 100%;
      background: rgba(255,255,255,0.08);
      backdrop-filter: blur(20px);
      border-radius: 32px;
      padding: 24px;
      border: 1px solid rgba(255,255,255,0.1);
      box-shadow: 0 25px 50px -12px rgba(0,0,0,0.5);
    }
    h1 {
      font-size: 28px;
      font-weight: 600;
      color: #a5f3fc;
      margin-bottom: 8px;
      display: flex;
      align-items: center;
      gap: 8px;
    }
    .subtitle {
      color: #94a3b8;
      font-size: 14px;
      margin-bottom: 24px;
      border-bottom: 1px solid #334155;
      padding-bottom: 16px;
    }
    .pill-list {
      display: flex;
      flex-direction: column;
      gap: 12px;
      margin-bottom: 20px;
    }
    .pill-item {
      background: rgba(30, 41, 59, 0.7);
      border-radius: 20px;
      padding: 16px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      backdrop-filter: blur(5px);
      border: 1px solid rgba(255,255,255,0.05);
    }
    .pill-info {
      display: flex;
      flex-direction: column;
    }
    .pill-name {
      color: #f1f5f9;
      font-size: 18px;
      font-weight: 600;
    }
    .pill-time {
      color: #7dd3fc;
      font-size: 15px;
      margin-top: 4px;
    }
    .delete-btn {
      background: rgba(239, 68, 68, 0.2);
      border: 1px solid rgba(239, 68, 68, 0.5);
      color: #fecaca;
      width: 44px;
      height: 44px;
      border-radius: 30px;
      display: flex;
      align-items: center;
      justify-content: center;
      font-size: 24px;
      cursor: pointer;
      transition: 0.2s;
    }
    .delete-btn:hover {
      background: rgba(239, 68, 68, 0.4);
    }
    .add-button {
      background: linear-gradient(135deg, #2dd4bf, #06b6d4);
      border: none;
      color: white;
      font-size: 20px;
      font-weight: 600;
      padding: 18px;
      border-radius: 40px;
      width: 100%;
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 8px;
      cursor: pointer;
      margin: 16px 0 24px 0;
      border: 1px solid rgba(255,255,255,0.2);
      box-shadow: 0 10px 20px -5px rgba(6,182,212,0.3);
    }
    .section-title {
      color: #cbd5e1;
      font-size: 18px;
      margin-bottom: 16px;
      font-weight: 500;
    }
    .config-group {
      background: rgba(15, 23, 42, 0.6);
      border-radius: 24px;
      padding: 20px;
      margin-bottom: 16px;
    }
    label {
      color: #94a3b8;
      display: block;
      margin-bottom: 8px;
      font-size: 15px;
    }
    select {
      width: 100%;
      padding: 12px 16px;
      background: #1e293b;
      border: 1px solid #334155;
      border-radius: 20px;
      color: white;
      font-size: 16px;
      margin-bottom: 20px;
    }
    .form-popup {
      background: #0f172a;
      border-radius: 32px;
      padding: 24px;
      margin-top: 16px;
      border: 1px solid #334155;
      display: none;
    }
    .form-group {
      margin-bottom: 20px;
    }
    .form-group input {
      width: 100%;
      padding: 14px;
      background: #1e293b;
      border: 1px solid #334155;
      border-radius: 20px;
      color: white;
      font-size: 16px;
    }
    .btn {
      background: #2dd4bf;
      border: none;
      color: #0f172a;
      font-weight: 600;
      padding: 14px;
      border-radius: 30px;
      width: 48%;
      font-size: 16px;
      cursor: pointer;
    }
    .btn.cancel {
      background: #475569;
      color: white;
    }
    .empty-state {
      text-align: center;
      padding: 40px 20px;
      color: #64748b;
    }
  </style>
</head>
<body>
<div class="card">
  <h1>💊 Pill Reminder</h1>
  <div class="subtitle">Connected to )rawliteral" + localIP + R"rawliteral(</div>
  
  <div id="pillList" class="pill-list"></div>
  <button class="add-button" onclick="showForm()">➕ Add Medication</button>
  
  <div id="addForm" class="form-popup">
    <h3 style="color:#a5f3fc; margin-bottom:20px;">New Pill</h3>
    <div class="form-group">
      <input type="text" id="pillName" placeholder="Name (e.g. Vitamin D)">
    </div>
    <div class="form-group" style="display:flex; gap:10px;">
      <input type="number" id="hour" placeholder="Hour (0-23)" min="0" max="23">
      <input type="number" id="minute" placeholder="Minute (0-59)" min="0" max="59">
    </div>
    <div style="display:flex; gap:10px;">
      <button class="btn" onclick="savePill()">Save</button>
      <button class="btn cancel" onclick="hideForm()">Cancel</button>
    </div>
  </div>
  
  <div class="section-title">⚙️ Timezone</div>
  <div class="config-group">
    <label>UTC Offset</label>
    <select id="timezoneSelect">
      <option value="-720">UTC-12:00</option><option value="-660">UTC-11:00</option>
      <option value="-600">UTC-10:00</option><option value="-540">UTC-09:00</option>
      <option value="-480">UTC-08:00</option><option value="-420">UTC-07:00</option>
      <option value="-360">UTC-06:00</option><option value="-300">UTC-05:00</option>
      <option value="-240">UTC-04:00</option><option value="-180">UTC-03:00</option>
      <option value="-120">UTC-02:00</option><option value="-60">UTC-01:00</option>
      <option value="0">UTC±00:00</option><option value="60">UTC+01:00</option>
      <option value="120">UTC+02:00</option><option value="180">UTC+03:00</option>
      <option value="240">UTC+04:00</option><option value="300">UTC+05:00</option>
      <option value="330" selected>UTC+05:30 (IST)</option>
      <option value="360">UTC+06:00</option><option value="420">UTC+07:00</option>
      <option value="480">UTC+08:00</option><option value="540">UTC+09:00</option>
      <option value="600">UTC+10:00</option><option value="660">UTC+11:00</option>
      <option value="720">UTC+12:00</option>
    </select>
    <button class="add-button" style="margin-top:8px;" onclick="saveTimezone()">Save Timezone</button>
  </div>
</div>

<script>
  let pills = [];

  function loadPills() {
    fetch('/pills').then(r=>r.json()).then(data => {
      pills = data;
      renderPills();
    }).catch(err => console.error('Failed to load pills:', err));
  }

  function renderPills() {
    const container = document.getElementById('pillList');
    if (!pills || pills.length === 0) {
      container.innerHTML = '<div class="empty-state">✨ No medications yet.<br>Tap + to add.</div>';
      return;
    }
    let html = '';
    pills.forEach((pill, idx) => {
      html += `<div class="pill-item">
        <div class="pill-info">
          <span class="pill-name">${pill.name}</span>
          <span class="pill-time">🕒 ${String(pill.hour).padStart(2,'0')}:${String(pill.minute).padStart(2,'0')}</span>
        </div>
        <div class="delete-btn" onclick="deletePill(${idx})">🗑️</div>
      </div>`;
    });
    container.innerHTML = html;
  }

  function showForm() {
    document.getElementById('addForm').style.display = 'block';
  }
  function hideForm() {
    document.getElementById('addForm').style.display = 'none';
    document.getElementById('pillName').value = '';
    document.getElementById('hour').value = '';
    document.getElementById('minute').value = '';
  }

  function savePill() {
    const name = document.getElementById('pillName').value.trim();
    const hour = parseInt(document.getElementById('hour').value);
    const minute = parseInt(document.getElementById('minute').value);
    if (!name || isNaN(hour) || isNaN(minute) || hour<0 || hour>23 || minute<0 || minute>59) {
      alert('Please enter valid values');
      return;
    }
    fetch(`/add?name=${encodeURIComponent(name)}&hour=${hour}&minute=${minute}`)
      .then(response => {
        if (response.ok) {
          hideForm();
          loadPills();
        } else {
          alert('Failed to add pill');
        }
      });
  }

  function deletePill(index) {
    if (confirm('Delete this pill?')) {
      fetch(`/delete?index=${index}`).then(() => loadPills());
    }
  }

  function saveTimezone() {
    const tz = parseInt(document.getElementById('timezoneSelect').value);
    fetch(`/config?tz=${tz}`).then(() => alert('Timezone saved'));
  }

  fetch('/getconfig').then(r=>r.json()).then(data => {
    document.getElementById('timezoneSelect').value = data.tz;
  });

  loadPills();
  setInterval(loadPills, 5000);
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", html);
}

void handleGetPills() {
  String json = "[";
  for (int i = 0; i < pillCount; i++) {
    if (i > 0) json += ",";
    json += "{\"name\":\"" + String(pills[i].name) + "\",";
    json += "\"hour\":" + String(pills[i].hour) + ",";
    json += "\"minute\":" + String(pills[i].minute) + "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleAddPill() {
  if (server.hasArg("name") && server.hasArg("hour") && server.hasArg("minute") && pillCount < MAX_PILLS) {
    String name = server.arg("name");
    int hour = server.arg("hour").toInt();
    int minute = server.arg("minute").toInt();
    name.toCharArray(pills[pillCount].name, 24);
    pills[pillCount].hour = hour;
    pills[pillCount].minute = minute;
    pills[pillCount].active = true;
    pills[pillCount].takenToday = false;
    pillCount++;
    savePills();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

void handleDeletePill() {
  if (server.hasArg("index")) {
    int idx = server.arg("index").toInt();
    if (idx >= 0 && idx < pillCount) {
      for (int i = idx; i < pillCount - 1; i++) pills[i] = pills[i + 1];
      pillCount--;
      savePills();
      server.send(200, "text/plain", "OK");
    } else server.send(400, "text/plain", "Bad Request");
  } else server.send(400, "text/plain", "Bad Request");
}

void handleGetConfig() {
  String json = "{\"tz\":" + String(timezoneMinutes) + "}";
  server.send(200, "application/json", json);
}

void handleSetConfig() {
  if (server.hasArg("tz")) {
    timezoneMinutes = server.arg("tz").toInt();
    if (timezoneMinutes < -720) timezoneMinutes = 0;
    if (timezoneMinutes > 840) timezoneMinutes = 330;
    saveTimezone();
    syncTime();
  }
  server.send(200, "text/plain", "OK");
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);
  // while (!Serial) delay(10);   // <-- REMOVED – device starts without waiting for Serial Monitor
  Serial.println("\n=== Pill Reminder ===");

  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  // Show boot bitmap
  drawBootBitmap();

  round_display_touch_init();

  EEPROM.begin(EEPROM_SIZE);
  loadTimezone();
  loadPills();

  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    localIP = WiFi.localIP().toString();
    Serial.println("\nIP: " + localIP);
    
    tft.fillScreen(TFT_BLACK);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.drawString("IP Address:", 30, 80);
    tft.setFreeFont(&FreeSans12pt7b);
    tft.drawString(localIP, 30, 120);
    ipShowStart = millis();
    showingIP = true;
    
    syncTime();
  } else {
    Serial.println("WiFi failed");
    tft.fillScreen(TFT_RED);
    tft.drawString("WiFi Failed", 30, 100);
    delay(2000);
  }

  server.on("/", handleRoot);
  server.on("/pills", handleGetPills);
  server.on("/add", handleAddPill);
  server.on("/delete", handleDeletePill);
  server.on("/getconfig", handleGetConfig);
  server.on("/config", handleSetConfig);
  server.begin();
  Serial.println("Web server ready");
}

// ========== LOOP ==========
void loop() {
  server.handleClient();

  if (showingIP && millis() - ipShowStart > 5000) {
    showingIP = false;
    drawClockScreen();
  }

  static unsigned long lastTimeUpdate = 0;
  if (!showingIP && !reminderActive && millis() - lastTimeUpdate > 1000) {
    updateClockTime();
    lastTimeUpdate = millis();
  }

  if (!showingIP && !reminderActive) {
    checkReminders();
  }

  if (chsc6x_is_pressed()) {
    int32_t x, y;
    if (chsc6x_get_xy(x, y)) {
      if (reminderActive) {
        if (reminderIndex >= 0) {
          pills[reminderIndex].takenToday = true;
          pills[reminderIndex].active = false;
          savePills();
        }
        drawPillTakenScreen();
        reminderActive = false;
        reminderIndex = -1;
        drawClockScreen();
      }
      delay(200);
    }
  }

  static int lastDay = -1;
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    if (timeinfo.tm_yday != lastDay) {
      lastDay = timeinfo.tm_yday;
      for (int i = 0; i < pillCount; i++) {
        pills[i].takenToday = false;
        pills[i].active = true;
      }
      savePills();
      Serial.println("Daily reset");
    }
  }

  delay(50);
}
