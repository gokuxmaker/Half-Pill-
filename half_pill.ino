
#include <WiFi.h>
#include <WebServer.h>

#define EPAPER_ENABLE
#include "TFT_eSPI.h"
#ifdef EPAPER_ENABLE
EPaper epaper;
#endif

const char* ssid = "ClapBoard_AP";
const char* password = "123456789";

WebServer server(80);

// Film clap board fields - updated with your example values
String roll = "A001";
String scene = "35A";
String take = "1";
String prod = "PANDIPADA";
String director = "GOKUL";
String dop = "GOKUL";
String note = "ISO 120";
String date = "11.03.1999";
IPAddress localIP;

void showIPOnBoot() {
#ifdef EPAPER_ENABLE
  epaper.begin();
  epaper.fillScreen(TFT_WHITE);
  epaper.setTextColor(TFT_BLACK, TFT_WHITE);
  epaper.setTextSize(3);
  
  String ipText = "IP: " + localIP.toString();
  // Center the IP text
  epaper.setTextSize(3);
  epaper.setFreeFont();
  int textWidth = ipText.length() * 18; // Approximate width
  int x = (800 - textWidth) / 2;
  if (x < 0) x = 0;
  epaper.drawString(ipText, x, 240);
  
  epaper.update();
  delay(5000); // Show IP for 5 seconds
#endif
}

void updateDisplay() {
#ifdef EPAPER_ENABLE
  epaper.begin();
  epaper.fillScreen(TFT_WHITE);
  epaper.setTextColor(TFT_BLACK, TFT_WHITE);

  // Draw all lines exactly as specified
  epaper.drawLine(0, 0, 0, 0, TFT_BLACK);
  epaper.drawLine(0, 193, 798, 193, TFT_BLACK);
  epaper.drawLine(0, 313, 798, 313, TFT_BLACK);
  epaper.drawLine(400, 314, 400, 478, TFT_BLACK);
  epaper.drawLine(0, 394, 798, 394, TFT_BLACK);
  epaper.drawLine(266, -1, 266, 192, TFT_BLACK);
  epaper.drawLine(548, -1, 548, 192, TFT_BLACK);

  // Set free font as specified
  epaper.setFreeFont();

  // Top section labels - Size 3
  epaper.setTextSize(3);
  epaper.drawString("ROLL", 5, 6);
  epaper.drawString("SCENE", 271, 7);
  epaper.drawString("TAKE", 560, 5);

  // Top section values - Size 8
  epaper.setTextSize(8);
  epaper.drawString(roll, 41, 76);
  epaper.drawString(scene, 335, 73);
  epaper.drawString(take, 660, 72);

  // PROD section - Size 3 label, Size 8 value
  epaper.setTextSize(3);
  epaper.drawString("PROD", 3, 197);
  epaper.setTextSize(8);
  epaper.drawString(prod, 188, 224);

  // Middle section labels - Size 2
  epaper.setTextSize(2);
  epaper.drawString("DIR", 2, 316);
  epaper.drawString("DOP", 403, 316);
  epaper.drawString("NOTE", 2, 397);
  epaper.drawString("DATE", 403, 397);

  // Middle section values - Size 6 for DIR and DOP, Size 6 for NOTE and DATE
  epaper.setTextSize(6);
  epaper.drawString(director, 95, 335);
  epaper.drawString(dop, 511, 337);
  
  // NOTE and DATE values
  epaper.drawString(note, 48, 416);
  epaper.drawString(date, 420, 415);

  epaper.update();
#endif
}

String htmlPage() {
  String html = R"HTML(
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Film Clap Board Control</title>
  <style>
    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
    }
    
    body {
      font-family: 'Segoe UI', Arial, sans-serif;
      background: #8fc31f;
      margin: 0;
      padding: 15px;
      display: flex;
      justify-content: center;
      align-items: center;
      min-height: 100vh;
      color: #333;
    }
    
    .container {
      max-width: 800px;
      width: 100%;
      margin: 0 auto;
    }
    
    .header {
      text-align: center;
      margin-bottom: 20px;
      color: white;
    }
    
    .header h1 {
      font-size: 2rem;
      margin-bottom: 5px;
      text-shadow: 0 2px 4px rgba(0,0,0,0.3);
    }
    
    .header p {
      font-size: 1rem;
      opacity: 0.9;
    }
    
    .card {
      background: rgba(255, 255, 255, 0.95);
      padding: 20px;
      border-radius: 15px;
      box-shadow: 0 10px 25px rgba(0, 0, 0, 0.2);
    }
    
    .form-grid {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 15px;
      margin-bottom: 20px;
    }
    
    .form-group {
      margin-bottom: 12px;
    }
    
    label {
      display: block;
      font-weight: 600;
      margin-bottom: 6px;
      color: #495057;
      font-size: 0.85rem;
    }
    
    input {
      width: 100%;
      padding: 10px 12px;
      border-radius: 8px;
      border: 2px solid #e9ecef;
      font-size: 14px;
      transition: all 0.3s ease;
      background: white;
    }
    
    input:focus {
      outline: none;
      border-color: #8fc31f;
      box-shadow: 0 0 0 3px rgba(143, 195, 31, 0.2);
    }
    
    .full-width {
      grid-column: 1 / -1;
    }
    
    .two-columns {
      grid-column: span 2;
    }
    
    .btn-container {
      text-align: center;
      margin-top: 20px;
    }
    
    .btn {
      background: #8fc31f;
      color: white;
      padding: 12px 30px;
      border-radius: 8px;
      font-weight: bold;
      border: none;
      cursor: pointer;
      font-size: 1rem;
      transition: all 0.3s ease;
      width: 100%;
    }
    
    .btn:hover {
      background: #7aad1a;
      transform: translateY(-1px);
    }
    
    .btn:active {
      transform: translateY(0);
    }
    
    .ip-display {
      text-align: center;
      margin-top: 15px;
      color: white;
      font-size: 0.9rem;
    }
    
    @media (max-width: 768px) {
      .form-grid {
        grid-template-columns: 1fr;
        gap: 12px;
      }
      
      .two-columns {
        grid-column: span 1;
      }
      
      .header h1 {
        font-size: 1.8rem;
      }
      
      .card {
        padding: 15px;
      }
      
      body {
        padding: 10px;
      }
    }
    
    @media (max-width: 480px) {
      .header h1 {
        font-size: 1.6rem;
      }
      
      input {
        padding: 8px 10px;
        font-size: 13px;
      }
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>Film Clap Board Control</h1>
      <p>Update film production information</p>
    </div>
    
    <div class="card">
      <form method="POST" action="/update">
        <div class="form-grid">
          <div class="form-group">
            <label for="roll">ROLL</label>
            <input type="text" id="roll" name="roll" value=")HTML";
  html += roll;
  html += R"HTML(" placeholder="e.g., A001" maxlength="10">
          </div>
          
          <div class="form-group">
            <label for="scene">SCENE</label>
            <input type="text" id="scene" name="scene" value=")HTML";
  html += scene;
  html += R"HTML(" placeholder="e.g., 35A" maxlength="10">
          </div>
          
          <div class="form-group">
            <label for="take">TAKE</label>
            <input type="text" id="take" name="take" value=")HTML";
  html += take;
  html += R"HTML(" placeholder="e.g., 1" maxlength="10">
          </div>
          
          <div class="form-group full-width">
            <label for="prod">PRODUCTION (PROD)</label>
            <input type="text" id="prod" name="prod" value=")HTML";
  html += prod;
  html += R"HTML(" placeholder="e.g., PANDIPADA" maxlength="30">
          </div>
          
          <div class="form-group two-columns">
            <label for="director">DIRECTOR (DIR)</label>
            <input type="text" id="director" name="director" value=")HTML";
  html += director;
  html += R"HTML(" placeholder="e.g., GOKUL" maxlength="30">
          </div>
          
          <div class="form-group">
            <label for="dop">DIRECTOR OF PHOTOGRAPHY (DOP)</label>
            <input type="text" id="dop" name="dop" value=")HTML";
  html += dop;
  html += R"HTML(" placeholder="e.g., GOKUL" maxlength="30">
          </div>
          
          <div class="form-group two-columns">
            <label for="note">NOTE</label>
            <input type="text" id="note" name="note" value=")HTML";
  html += note;
  html += R"HTML(" placeholder="e.g., ISO 120" maxlength="30">
          </div>
          
          <div class="form-group">
            <label for="date">DATE</label>
            <input type="text" id="date" name="date" value=")HTML";
  html += date;
  html += R"HTML(" placeholder="e.g., 11.03.1999" maxlength="15">
          </div>
        </div>
        
        <div class="btn-container">
          <button class="btn" type="submit">
            Update Clap Board
          </button>
        </div>
      </form>
    </div>
    
    <div class="ip-display">
      <p>Connect to: <strong>)HTML";
  html += localIP.toString();
  html += R"HTML(</strong></p>
    </div>
  </div>
  
  <script>
    document.addEventListener('DOMContentLoaded', function() {
      // Set default date to today if empty
      const dateInput = document.getElementById('date');
      if (dateInput.value === '') {
        const today = new Date();
        const dd = String(today.getDate()).padStart(2, '0');
        const mm = String(today.getMonth() + 1).padStart(2, '0');
        const yyyy = today.getFullYear();
        dateInput.value = `${dd}.${mm}.${yyyy}`;
      }
    });
  </script>
</body>
</html>)HTML";

  return html;
}

void handleRoot() {
  server.send(200, "text/html", htmlPage());
}

void handleUpdate() {
  if (server.hasArg("roll")) roll = server.arg("roll");
  if (server.hasArg("scene")) scene = server.arg("scene");
  if (server.hasArg("take")) take = server.arg("take");
  if (server.hasArg("prod")) prod = server.arg("prod");
  if (server.hasArg("director")) director = server.arg("director");
  if (server.hasArg("dop")) dop = server.arg("dop");
  if (server.hasArg("note")) note = server.arg("note");
  if (server.hasArg("date")) date = server.arg("date");

  updateDisplay();
  
  // Redirect back to the main page
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "Updated. Redirecting...");
}

void setup() {
  Serial.begin(115200);
  
  // Start WiFi Access Point
  WiFi.softAP(ssid, password);
  localIP = WiFi.softAPIP();
  
  Serial.println("Access Point Started");
  Serial.print("IP Address: ");
  Serial.println(localIP);

  // Show IP on display for 5 seconds
  showIPOnBoot();
  
  // Initialize web server routes
  server.on("/", handleRoot);
  server.on("/update", HTTP_POST, handleUpdate);
  server.begin();
  
  Serial.println("HTTP server started");
  
  // Initial display update
  updateDisplay();
}

void loop() {
  server.handleClient();
}
