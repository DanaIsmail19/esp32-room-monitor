/*
 * ESP32 Room Monitor
 * ------------------
 * Reads temperature/humidity from a DHT22, shows it on an SSD1306 OLED,
 * raises an alarm (red LED + buzzer) when temperature crosses a threshold
 * set by a potentiometer, and serves the readings over Wi-Fi:
 *
 *   GET /          -> live HTML dashboard (auto-refreshes)
 *   GET /api       -> JSON
 *   GET /metrics   -> Prometheus text format (easy to scrape from Zabbix/Prometheus)
 *
 * A push button silences the alarm until the temperature drops back to normal.
 * Runs as-is in the Wokwi simulator (diagram.json included).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- Configuration ----------
// "Wokwi-GUEST" works in the simulator. Change for real hardware.
const char *WIFI_SSID = "Wokwi-GUEST";
const char *WIFI_PASS = "";

constexpr uint8_t PIN_DHT    = 15;
constexpr uint8_t PIN_LED_OK = 2;    // green
constexpr uint8_t PIN_LED_AL = 4;    // red
constexpr uint8_t PIN_BUZZER = 18;
constexpr uint8_t PIN_BUTTON = 19;   // to GND, uses internal pull-up
constexpr uint8_t PIN_POT    = 34;   // ADC1, input only

constexpr float THRESH_MIN_C = 20.0;  // pot fully left
constexpr float THRESH_MAX_C = 40.0;  // pot fully right
constexpr float HYSTERESIS_C = 0.5;   // avoids alarm flapping at the boundary
constexpr uint32_t READ_INTERVAL_MS = 2000;  // DHT22 max rate is ~0.5 Hz

// ---------- Globals ----------
DHT dht(PIN_DHT, DHT22);
Adafruit_SSD1306 oled(128, 64, &Wire, -1);
WebServer server(80);

struct State {
  float tempC = NAN, humidity = NAN, thresholdC = 30.0;
  bool alarm = false, silenced = false, sensorOk = false;
  uint32_t readings = 0, sensorErrors = 0;
} st;

// ---------- Helpers ----------
float readThreshold() {
  int raw = analogRead(PIN_POT);                 // 0..4095
  return THRESH_MIN_C + (THRESH_MAX_C - THRESH_MIN_C) * raw / 4095.0f;
}

void updateAlarm() {
  if (!st.sensorOk) return;
  if (!st.alarm && st.tempC >= st.thresholdC) {
    st.alarm = true;
  } else if (st.alarm && st.tempC < st.thresholdC - HYSTERESIS_C) {
    st.alarm = false;
    st.silenced = false;                          // re-arm for next event
  }
  digitalWrite(PIN_LED_AL, st.alarm);
  digitalWrite(PIN_LED_OK, !st.alarm);
}

void driveBuzzer() {
  // Beep pattern: 200 ms on / 300 ms off, non-blocking
  if (st.alarm && !st.silenced && (millis() % 500) < 200)
    tone(PIN_BUZZER, 2000);
  else
    noTone(PIN_BUZZER);
}

void drawOled() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(WiFi.isConnected() ? WiFi.localIP().toString() : String("WiFi..."));

  oled.setTextSize(2);
  oled.setCursor(0, 16);
  if (st.sensorOk) {
    oled.printf("%.1f C", st.tempC);
    oled.setCursor(0, 34);
    oled.printf("%.0f %%RH", st.humidity);
  } else {
    oled.print("SENSOR ERR");
  }

  oled.setTextSize(1);
  oled.setCursor(0, 56);
  if (st.alarm) oled.print(st.silenced ? "ALARM (silenced)" : "!! ALARM !!");
  else          oled.printf("Limit: %.1f C", st.thresholdC);
  oled.display();
}

// ---------- Web handlers ----------
void handleApi() {
  char buf[256];
  snprintf(buf, sizeof buf,
    "{\"temperature_c\":%.2f,\"humidity\":%.1f,\"threshold_c\":%.1f,"
    "\"alarm\":%s,\"silenced\":%s,\"sensor_ok\":%s,\"uptime_s\":%lu}",
    st.tempC, st.humidity, st.thresholdC,
    st.alarm ? "true" : "false", st.silenced ? "true" : "false",
    st.sensorOk ? "true" : "false", millis() / 1000);
  server.send(200, "application/json", buf);
}

void handleMetrics() {
  String m;
  m += "# TYPE room_temperature_celsius gauge\nroom_temperature_celsius " + String(st.tempC, 2) + "\n";
  m += "# TYPE room_humidity_percent gauge\nroom_humidity_percent " + String(st.humidity, 1) + "\n";
  m += "# TYPE room_threshold_celsius gauge\nroom_threshold_celsius " + String(st.thresholdC, 1) + "\n";
  m += "# TYPE room_alarm gauge\nroom_alarm " + String(st.alarm) + "\n";
  m += "# TYPE sensor_errors_total counter\nsensor_errors_total " + String(st.sensorErrors) + "\n";
  m += "# TYPE wifi_rssi_dbm gauge\nwifi_rssi_dbm " + String(WiFi.RSSI()) + "\n";
  server.send(200, "text/plain; version=0.0.4", m);
}

void handleRoot() {
  static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Room Monitor</title>
<style>body{font-family:system-ui;background:#111;color:#eee;display:grid;place-items:center;height:100vh;margin:0}
.card{background:#1d1d1d;padding:2rem 3rem;border-radius:16px;text-align:center}
.big{font-size:3.5rem;font-weight:700}.al{color:#ff5050}.ok{color:#50e080}small{color:#999}</style></head>
<body><div class=card><small>ESP32 Room Monitor</small>
<div class=big id=t>--</div><div id=h>--</div><p id=s>...</p><small id=l></small></div>
<script>
async function u(){try{const d=await (await fetch('/api')).json();
t.textContent=d.temperature_c.toFixed(1)+' °C';h.textContent=d.humidity.toFixed(0)+' % RH';
s.textContent=d.alarm?(d.silenced?'ALARM (silenced)':'ALARM'):'Normal';s.className=d.alarm?'al':'ok';
l.textContent='Threshold '+d.threshold_c.toFixed(1)+' °C · up '+d.uptime_s+' s';}catch(e){s.textContent='offline'}}
u();setInterval(u,2000);
</script></body></html>)HTML";
  server.send_P(200, "text/html", PAGE);
}

// ---------- Setup / loop ----------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_OK, OUTPUT);
  pinMode(PIN_LED_AL, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  dht.begin();

  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("OLED not found");
  oled.clearDisplay(); oled.display();

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");

  server.on("/", handleRoot);
  server.on("/api", handleApi);
  server.on("/metrics", handleMetrics);
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });
}

void loop() {
  static uint32_t lastRead = 0;
  static bool serverStarted = false, lastBtn = HIGH;

  // Start web server once Wi-Fi is up (non-blocking connect)
  if (!serverStarted && WiFi.isConnected()) {
    server.begin();
    serverStarted = true;
    Serial.printf("\nConnected. Open http://%s/\n", WiFi.localIP().toString().c_str());
  }
  if (serverStarted) server.handleClient();

  // Button: falling edge silences an active alarm
  bool btn = digitalRead(PIN_BUTTON);
  if (lastBtn == HIGH && btn == LOW && st.alarm) {
    st.silenced = true;
    Serial.println("Alarm silenced");
  }
  lastBtn = btn;

  if (millis() - lastRead >= READ_INTERVAL_MS) {
    lastRead = millis();
    float t = dht.readTemperature(), h = dht.readHumidity();
    st.sensorOk = !(isnan(t) || isnan(h));
    if (st.sensorOk) { st.tempC = t; st.humidity = h; st.readings++; }
    else st.sensorErrors++;
    st.thresholdC = readThreshold();
    updateAlarm();
    drawOled();
    Serial.printf("T=%.1fC H=%.0f%% limit=%.1fC alarm=%d\n",
                  st.tempC, st.humidity, st.thresholdC, st.alarm);
  }

  driveBuzzer();
}
