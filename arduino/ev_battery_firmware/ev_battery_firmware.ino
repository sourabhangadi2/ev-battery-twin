/*
 * ============================================================
 * SELF-HEALING EV BATTERY DIGITAL TWIN
 * ESP32 REAL HARDWARE FIRMWARE
 * ============================================================
 *
 * Sensors:
 *   DS18B20  -> Battery temperature
 *   INA219   -> Battery voltage/current/power
 *
 * Actuator:
 *   1-channel 5V relay -> 5V cooling fan
 *
 * Display:
 *   I2C LCD 16x2
 *
 * Communication:
 *   ESP32 -> Wi-Fi -> Python Flask Digital Twin
 *
 * No USB serial communication is used for telemetry.
 * USB is only used for programming/debugging.
 * ============================================================
 */

// ============================================================
// LIBRARIES
// ============================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <Wire.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include <Adafruit_INA219.h>

#include <LiquidCrystal_I2C.h>

// ============================================================
// WI-FI SETTINGS
// ============================================================

const char *ssid = "SAGAR123";
const char *password = "SAGAR1243";

// ============================================================
// PYTHON FLASK SERVER
// ============================================================

// Laptop IP address running Flask
const String serverIP = "10.29.175.245";
const int serverPort = 5000;

const String endpoint =
    "http://" +
    serverIP +
    ":" +
    String(serverPort) +
    "/update_sensor_data";

// ============================================================
// ESP32 PIN DEFINITIONS
// ============================================================

// I2C
#define I2C_SDA 21
#define I2C_SCL 22

// DS18B20
#define DS18B20_PIN 4

// Relay
#define RELAY_PIN 26

// ============================================================
// RELAY LOGIC
// ============================================================

// Your 5V relay module is active LOW.
//
// LOW  = relay ON
// HIGH = relay OFF

#define RELAY_ON LOW
#define RELAY_OFF HIGH

// ============================================================
// INA219
// ============================================================

Adafruit_INA219 ina219;

// ============================================================
// DS18B20
// ============================================================

OneWire oneWire(DS18B20_PIN);

DallasTemperature ds18b20(
    &oneWire);

// ============================================================
// LCD
// ============================================================

// Common address is 0x27.
// If your LCD scanner shows another address,
// change 0x27 accordingly.

LiquidCrystal_I2C lcd(
    0x27,
    16,
    2);

// ============================================================
// TIMING
// ============================================================

const unsigned long telemetryInterval = 1000;

unsigned long lastTelemetryTime = 0;

// ============================================================
// SETUP
// ============================================================

void setup()
{
  // --------------------------------------------------------
  // Serial is only for debugging.
  // It is NOT used for data transfer.
  // --------------------------------------------------------

  Serial.begin(115200);

  delay(1000);

  // --------------------------------------------------------
  // Relay
  // --------------------------------------------------------

  pinMode(
      RELAY_PIN,
      OUTPUT);

  // Fan initially OFF
  digitalWrite(
      RELAY_PIN,
      RELAY_OFF);

  // --------------------------------------------------------
  // I2C
  // --------------------------------------------------------

  Wire.begin(
      I2C_SDA,
      I2C_SCL);

  // --------------------------------------------------------
  // LCD
  // --------------------------------------------------------

  lcd.init();

  lcd.backlight();

  lcd.clear();

  lcd.setCursor(0, 0);

  lcd.print("EV BATTERY");

  lcd.setCursor(0, 1);

  lcd.print("Starting...");

  // --------------------------------------------------------
  // INA219
  // --------------------------------------------------------

  if (!ina219.begin())
  {
    Serial.println(
        "INA219 NOT FOUND!");

    lcd.clear();

    lcd.setCursor(0, 0);

    lcd.print("INA219 ERROR");

    lcd.setCursor(0, 1);

    lcd.print("Check wiring");

    delay(3000);
  }
  else
  {
    Serial.println(
        "INA219 OK");
  }

  // --------------------------------------------------------
  // DS18B20
  // --------------------------------------------------------

  ds18b20.begin();

  Serial.println(
      "DS18B20 initialized");

  // --------------------------------------------------------
  // Wi-Fi
  // --------------------------------------------------------

  connectWiFi();

  // --------------------------------------------------------
  // Ready
  // --------------------------------------------------------

  lcd.clear();

  lcd.setCursor(0, 0);

  lcd.print("Digital Twin");

  lcd.setCursor(0, 1);

  lcd.print("Connected");

  delay(1500);
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop()
{
  // --------------------------------------------------------
  // Maintain Wi-Fi connection
  // --------------------------------------------------------

  if (WiFi.status() != WL_CONNECTED)
  {
    connectWiFi();
  }

  // --------------------------------------------------------
  // Telemetry interval
  // --------------------------------------------------------

  unsigned long currentMillis =
      millis();

  if (
      currentMillis -
          lastTelemetryTime >=
      telemetryInterval)
  {
    lastTelemetryTime =
        currentMillis;

    // ----------------------------------------------------
    // READ DS18B20
    // ----------------------------------------------------

    ds18b20.requestTemperatures();

    float temperature =
        ds18b20.getTempCByIndex(0);

    // ----------------------------------------------------
    // READ INA219
    // ----------------------------------------------------

    float busVoltage =
        ina219.getBusVoltage_V();

    float shuntVoltage =
        ina219.getShuntVoltage_mV();

    float current_mA =
        ina219.getCurrent_mA();

    // ----------------------------------------------------
    // Battery voltage
    //
    // INA219 bus voltage + shunt voltage
    // ----------------------------------------------------

    float batteryVoltage =
        busVoltage +
        (shuntVoltage / 1000.0);

    // ----------------------------------------------------
    // Convert current to amperes
    // ----------------------------------------------------

    float currentA =
        current_mA / 1000.0;

    // ----------------------------------------------------
    // Power
    // ----------------------------------------------------

    float powerW =
        batteryVoltage *
        currentA;

    // ----------------------------------------------------
    // Validate temperature
    // ----------------------------------------------------

    if (
        temperature == DEVICE_DISCONNECTED_C)
    {
      Serial.println(
          "DS18B20 read error");

      lcd.clear();

      lcd.setCursor(0, 0);

      lcd.print("TEMP SENSOR");

      lcd.setCursor(0, 1);

      lcd.print("ERROR");

      return;
    }

    // ----------------------------------------------------
    // SERIAL DEBUG
    // ----------------------------------------------------

    Serial.println(
        "-----------------------------");

    Serial.printf(
        "Temperature : %.2f C\n",
        temperature);

    Serial.printf(
        "Voltage     : %.3f V\n",
        batteryVoltage);

    Serial.printf(
        "Current     : %.3f A\n",
        currentA);

    Serial.printf(
        "Power       : %.3f W\n",
        powerW);

    Serial.print(
        "WiFi IP     : ");

    Serial.println(
        WiFi.localIP());

    // ----------------------------------------------------
    // LCD
    // ----------------------------------------------------

    updateLCD(
        temperature,
        batteryVoltage,
        currentA);

    // ----------------------------------------------------
    // SEND TO PYTHON
    // ----------------------------------------------------

    sendTelemetry(
        temperature,
        batteryVoltage,
        currentA);
  }
}

// ============================================================
// WI-FI CONNECTION
// ============================================================

void connectWiFi()
{
  Serial.println();

  Serial.print(
      "Connecting to Wi-Fi: ");

  Serial.println(
      ssid);

  lcd.clear();

  lcd.setCursor(0, 0);

  lcd.print("WiFi Connecting");

  WiFi.begin(
      ssid,
      password);

  int retryCount = 0;

  while (
      WiFi.status() != WL_CONNECTED &&
      retryCount < 20)
  {
    delay(500);

    Serial.print(".");

    retryCount++;
  }

  if (
      WiFi.status() ==
      WL_CONNECTED)
  {
    Serial.println();

    Serial.println(
        "WiFi connected!");

    Serial.print(
        "ESP32 IP: ");

    Serial.println(
        WiFi.localIP());

    lcd.clear();

    lcd.setCursor(0, 0);

    lcd.print("WiFi Connected");

    lcd.setCursor(0, 1);

    lcd.print(
        WiFi.localIP());

    delay(1500);
  }
  else
  {
    Serial.println();

    Serial.println(
        "WiFi connection failed");

    lcd.clear();

    lcd.setCursor(0, 0);

    lcd.print("WiFi FAILED");

    lcd.setCursor(0, 1);

    lcd.print("Retrying...");
  }
}

// ============================================================
// SEND TELEMETRY TO PYTHON
// ============================================================

void sendTelemetry(
    float temperature,
    float voltage,
    float current)
{
  if (
      WiFi.status() !=
      WL_CONNECTED)
  {
    Serial.println(
        "WiFi not connected");

    return;
  }

  HTTPClient http;

  // --------------------------------------------------------
  // Start HTTP connection
  // --------------------------------------------------------

  http.begin(
      endpoint);

  http.addHeader(
      "Content-Type",
      "application/json");

  // --------------------------------------------------------
  // Create JSON
  // --------------------------------------------------------

  StaticJsonDocument<256> doc;

  doc["source"] =
      "esp32";

  doc["temperature"] =
      temperature;

  doc["voltage"] =
      voltage;

  doc["current"] =
      current;

  String requestBody;

  serializeJson(
      doc,
      requestBody);

  Serial.print(
      "Sending JSON: ");

  Serial.println(
      requestBody);

  // --------------------------------------------------------
  // HTTP POST
  // --------------------------------------------------------

  int httpResponseCode =
      http.POST(
          requestBody);

  // --------------------------------------------------------
  // Process response
  // --------------------------------------------------------

  if (
      httpResponseCode == 200)
  {
    String responseString =
        http.getString();

    Serial.print(
        "Python response: ");

    Serial.println(
        responseString);

    // ----------------------------------------------------
    // Parse response
    // ----------------------------------------------------

    StaticJsonDocument<512>
        responseDoc;

    DeserializationError error =
        deserializeJson(
            responseDoc,
            responseString);

    if (!error)
    {
      JsonObject commands =
          responseDoc["commands"];

      bool relayState =
          commands["relay"] |
          false;

      bool fanState =
          commands["fan"] |
          false;

      bool buzzerState =
          commands["buzzer"] |
          false;

      // ------------------------------------------------
      // Fan is physically controlled by RELAY.
      //
      // If Python requests fan ON,
      // turn relay ON.
      //
      // If Python requests critical isolation,
      // relay can be OFF.
      // ------------------------------------------------

      if (
          !relayState)
      {
        // Critical battery isolation
        digitalWrite(
            RELAY_PIN,
            RELAY_OFF);
      }
      else if (
          fanState)
      {
        // Cooling required
        digitalWrite(
            RELAY_PIN,
            RELAY_ON);
      }
      else
      {
        // Normal operation
        digitalWrite(
            RELAY_PIN,
            RELAY_OFF);
      }

      Serial.print(
          "Relay: ");

      Serial.println(
          digitalRead(RELAY_PIN) == RELAY_ON
              ? "ON"
              : "OFF");

      Serial.print(
          "Fan command: ");

      Serial.println(
          fanState
              ? "ON"
              : "OFF");

      Serial.print(
          "Buzzer command: ");

      Serial.println(
          buzzerState
              ? "ON"
              : "OFF");
    }
    else
    {
      Serial.print(
          "JSON error: ");

      Serial.println(
          error.c_str());
    }
  }
  else
  {
    Serial.print(
        "HTTP error: ");

    Serial.println(
        httpResponseCode);
  }

  // --------------------------------------------------------
  // Close HTTP
  // --------------------------------------------------------

  http.end();
}

// ============================================================
// LCD UPDATE
// ============================================================

void updateLCD(
    float temperature,
    float voltage,
    float current)
{
  lcd.clear();

  // --------------------------------------------------------
  // Line 1: Temperature
  // --------------------------------------------------------

  lcd.setCursor(0, 0);

  lcd.print("T:");

  lcd.print(
      temperature,
      1);

  lcd.print("C ");

  lcd.print("V:");

  lcd.print(
      voltage,
      2);

  // --------------------------------------------------------
  // Line 2: Current
  // --------------------------------------------------------

  lcd.setCursor(0, 1);

  lcd.print("I:");

  lcd.print(
      current,
      2);

  lcd.print("A ");

  if (
      digitalRead(RELAY_PIN) == RELAY_ON)
  {
    lcd.print("FAN ON");
  }
  else
  {
    lcd.print("FAN OFF");
  }
}