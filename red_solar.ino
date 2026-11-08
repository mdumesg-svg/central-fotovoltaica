#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_INA219.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP085_U.h>


const char* ssid = "RED-DIRECT-HD";         
const char* password = "19752000rene2226";   
const char* mqtt_server = "broker.hivemq.com"; 

WiFiClient espClient;
PubSubClient client(espClient);


#define PIN_SDA 21
#define PIN_SCL 22


#define RELAY_BAT_PIN 23  
#define RELAY_FAN_PIN 19 


Adafruit_BMP085_Unified bmp = Adafruit_BMP085_Unified(18001);
Adafruit_INA219 ina219_Red(0x40);     
Adafruit_INA219 ina219_Bat(0x41);     


bool gridPowerActive = true;
bool batActive = false;
bool fanActive = false;
bool manualBatOverride = false; 
bool manualFanOverride = false;
String fanMode = "auto";             
float setpointTemp = 50.0;          

unsigned long lastMsgTime = 0;
const long interval = 2000; 

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando a WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado. IP: " + WiFi.localIP().toString());
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("Comando recibido [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);


  if (message == "TOGGLE_BAT") {
    batActive = !batActive;
    manualBatOverride = true; 
  } 
  else if (message == "TOGGLE_FAN") {
    if (fanMode == "manual") {
      fanActive = !fanActive;
      manualFanOverride = true;
    }
  } 
  else if (message.startsWith("MODE:")) {
    String mode = message.substring(5);
    if (mode == "AUTO") {
      fanMode = "auto";
      manualFanOverride = false; 
    } else if (mode == "MANUAL") {
      fanMode = "manual";
    }
  } 
  else if (message.startsWith("SET_TEMP:")) {
    setpointTemp = message.substring(9).toFloat();
    Serial.print("Nuevo Setpoint Térmico guardado: ");
    Serial.print(setpointTemp);
    Serial.println(" °C");
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conexión MQTT...");
    if (client.connect("ESP32_Client_Energia")) {
      Serial.println("conectado");
      client.subscribe("esp32_energia/cmd");
    } else {
      Serial.print("falló, rc=");
      Serial.print(client.state());
      Serial.println(" reintentando en 5 segundos");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RELAY_BAT_PIN, OUTPUT);
  pinMode(RELAY_FAN_PIN, OUTPUT);
  digitalWrite(RELAY_BAT_PIN, LOW);
  digitalWrite(RELAY_FAN_PIN, LOW);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);


  Wire.begin(PIN_SDA, PIN_SCL);


  if (!bmp.begin()) {
    Serial.println("¡Error! No se encuentra el sensor BMP180.");
  }
  if (!ina219_Red.begin(&Wire)) {
    Serial.println("¡Error! No se encuentra el INA219 de Red (0x40).");
  }
  if (!ina219_Bat.begin(&Wire)) {
    Serial.println("¡Error! No se encuentra el INA219 de Batería (0x41).");
  }
  
  Serial.println("--- SISTEMA INICIADO CORRECTAMENTE ---");
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastMsgTime > interval) {
    lastMsgTime = now;

    float temp = 0.0;
    bmp.getTemperature(&temp);

    float voltsRed   = ina219_Red.getBusVoltage_V();
    float currentRed = ina219_Red.getCurrent_mA();
    float powerRed   = ina219_Red.getPower_mW();

    float voltsBat   = ina219_Bat.getBusVoltage_V();
    float currentBat = ina219_Bat.getCurrent_mA();
    float powerBat   = ina219_Bat.getPower_mW();


    if (voltsRed < 5.0) {
      gridPowerActive = false; 
      if (!manualBatOverride) {
        batActive = true;   
      }
    } else if (voltsRed >= 10.0) {
      gridPowerActive = true;  
      if (!manualBatOverride) {
        batActive = false;   
      }
    }


    if (fanMode == "auto") {
      if (temp >= setpointTemp) {
        fanActive = true;   
      } else {
        fanActive = false;  
      }
    }

    digitalWrite(RELAY_BAT_PIN, batActive ? HIGH : LOW);
    digitalWrite(RELAY_FAN_PIN, fanActive ? HIGH : LOW);

    String payload = "{";
    payload += "\"grid\":" + String(gridPowerActive ? "true" : "false") + ",";
    payload += "\"batActive\":" + String(batActive ? "true" : "false") + ",";
    payload += "\"fanActive\":" + String(fanActive ? "true" : "false") + ",";
    payload += "\"temp\":" + String(temp, 2) + ",";
    payload += "\"volts\":" + String(voltsRed, 2) + ",";
    payload += "\"current\":" + String(currentRed, 2) + ",";
    payload += "\"power\":" + String(powerRed, 2) + ",";
    payload += "\"batVolts\":" + String(voltsBat, 2) + ",";
    payload += "\"batCurrent\":" + String(currentBat, 2) + ",";
    payload += "\"batPower\":" + String(powerBat, 2);
    payload += "}";


    client.publish("esp32_energia/status", payload.c_str());
    

    Serial.println("--- Telemetría Enviada ---");
    Serial.print("Temp: "); Serial.print(temp); Serial.print(" °C | Potencia Bat: "); Serial.print(powerBat); Serial.println(" mW");
  }
}