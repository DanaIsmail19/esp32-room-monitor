# ESP32 Room Monitor

A small microcontroller project: an ESP32 reads temperature and humidity, shows them on an OLED, sounds an alarm when it gets too hot, and serves the data over Wi-Fi as a web dashboard, JSON and Prometheus metrics.

## What it shows

| Concept | Where |
|---|---|
| Digital single-wire sensor protocol | DHT22 on GPIO 15 |
| I²C bus | SSD1306 OLED on GPIO 21 (SDA) / 22 (SCL) |
| ADC (analog input) | Potentiometer on GPIO 34 sets the alarm threshold (20–40 °C) |
| GPIO output + PWM tone | Green/red LEDs, buzzer |
| GPIO input with internal pull-up + edge detection | Silence button |
| Non-blocking timing with `millis()` | Main loop: no `delay()` anywhere |
| Hysteresis | Alarm clears only 0.5 °C below the threshold, so it doesn't flap |
| Embedded web server | `/`, `/api`, `/metrics` |

## Parts

ESP32 DevKit · DHT22 · 0.96" SSD1306 I²C OLED (128×64) · green + red LED · 2× 220 Ω resistors · active/passive buzzer · push button · 10 kΩ potentiometer · breadboard + jumpers

## Wiring

| Part | Pin | ESP32 |
|---|---|---|
| DHT22 | VCC / DATA / GND | 3V3 / GPIO 15 / GND (add 10 kΩ pull-up DATA→3V3 if your module lacks one) |
| OLED | VCC / GND / SDA / SCL | 3V3 / GND / GPIO 21 / GPIO 22 |
| Green LED | anode via 220 Ω | GPIO 2 |
| Red LED | anode via 220 Ω | GPIO 4 |
| Buzzer | + / − | GPIO 18 / GND |
| Button | one side / other side | GPIO 19 / GND |
| Potentiometer | ends / wiper | 3V3 & GND / GPIO 34 |

## Run it without hardware (Wokwi)

1. Go to wokwi.com → New Project → ESP32 (Arduino).
2. Paste `src/main.cpp` into `sketch.ino`, and replace `diagram.json` with the one here.
3. In the Library Manager add: `DHT sensor library`, `Adafruit SSD1306`, `Adafruit GFX Library`.
4. Press ▶. Click the DHT22 to drag the temperature above the threshold → red LED + beep. Press the button to silence. Turn the pot to move the threshold.

With the Wokwi VS Code extension + PlatformIO, `pio run` then "Wokwi: Start Simulator" uses `wokwi.toml`, and the dashboard is forwarded to http://localhost:8180.

## Run it on a real ESP32

1. Set `WIFI_SSID` / `WIFI_PASS` at the top of `src/main.cpp`.
2. `pio run -t upload && pio device monitor`
3. Open the IP printed in the serial monitor (also shown on the OLED).

## Endpoints

```
GET /         live dashboard (refreshes every 2 s)
GET /api      {"temperature_c":26.00,"humidity":45.0,"threshold_c":30.0,"alarm":false,...}
GET /metrics  room_temperature_celsius 26.00 ...
```

`/metrics` can be scraped by Prometheus, or polled by Zabbix with an HTTP agent item + a regex/Prometheus preprocessing step.

## Ideas to extend

- Deep sleep between readings for battery operation
- Publish to MQTT instead of (or as well as) HTTP
- Store the threshold in NVS (`Preferences`) so it survives reboots
- Swap the DHT22 for a BME280 (I²C, more accurate, adds pressure)
