#include "VOneMqttClient.h"
#include "DHT.h"
#include <ESP32Servo.h>

// Device IDs for V-One
const char* DHT11Sensor = "bd998b43-132f-4e5f-a291-0335b06bb1a9";
const char* MoistureSensor = "d8404461-57ad-45a6-ae59-cac90074aa1c";
const char* WaterLevel = "bf48187e-e2af-41b2-9d46-227a209517b6";
const char* RainSensor = "8b0a84e9-494f-4338-9720-93faf4927d11";
const char* ServoMotor = "14db892f-1515-4933-b615-b589dbfb5b57";

// Pins for each sensor and actuator
// Modify it according to your own pins
const int dht11Pin = 42;
const int moisturePin = A2;
const int depthPin = A5;
const int rainSensorPin = A8;
const int redledPin = 9;
const int yellowledPin = 5;
const int greenledPin = 4;
const int servoPin = A11;

// Variables for Moisture Sensor
// Modify MinMoistureValue and MaxMoistureValue according to your own sensor
int MinMoistureValue = 4095;
int MaxMoistureValue = 1800;
int MinMoisture = 0;
int MaxMoisture = 100;
int Moisture = 0;

// Variables for Water Level Sensor
// Modify MinDepthValue and MaxDepthValue according to your own sensor
int MinDepthValue = 0;
int MaxDepthValue = 1300;
int MinDepth = 0;
int MaxDepth = 100;
int depth = 0;

// Humidity and Moisture Thresholds
// Modify these threshold according to your sensor's range
const int rainThreshold = 2600; 
const int humidityThreshold = 90;  
const int moistureThreshold = 65; 

// DHT11 Sensor Setup
#define DHTTYPE DHT11
DHT dht(dht11Pin, DHTTYPE);

// Servo Setup
Servo Myservo;

// MQTT Client Setup
VOneMqttClient voneClient;

// Last message time
unsigned long lastMsgTime = 0;

// Functions to control the actuator from the V-One to the hardware circuit
void triggerActuator_callback(const char* actuatorDeviceId, const char* actuatorCommand)
{
  //actuatorCommand format {"servo":90}
  Serial.print("Main received callback : ");
  Serial.print(actuatorDeviceId);
  Serial.print(" : ");
  Serial.println(actuatorCommand);

  String errorMsg = "";

  JSONVar commandObjct = JSON.parse(actuatorCommand);
  JSONVar keys = commandObjct.keys();

  if (String(actuatorDeviceId) == ServoMotor)
  {
    //{"servo":90}
    String key = "";
    JSONVar commandValue = "";
    for (int i = 0; i < keys.length(); i++) {
      key = (const char* )keys[i];
      commandValue = commandObjct[keys[i]];

    }
    Serial.print("Key : ");
    Serial.println(key.c_str());
    Serial.print("value : ");
    Serial.println(commandValue);

    int angle = (int)commandValue;
    Myservo.write(angle);
    voneClient.publishActuatorStatusEvent(actuatorDeviceId, actuatorCommand, errorMsg.c_str(), true);//publish actuator status
  }
  else
  {
    Serial.print(" No actuator found : ");
    Serial.println(actuatorDeviceId);
    errorMsg = "No actuator found";
    voneClient.publishActuatorStatusEvent(actuatorDeviceId, actuatorCommand, errorMsg.c_str(), false);//publish actuator status
  }
}


// For Wi-Fi Setup
void setup_wifi() {
  delay(10);
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// Start the system
void setup() {
  setup_wifi();
  voneClient.setup();
  voneClient.registerActuatorCallback(triggerActuator_callback);

  // Initialize Sensors
  dht.begin();
  
  // Initialize Actuators
  pinMode(redledPin, OUTPUT);
  pinMode(yellowledPin, OUTPUT);
  pinMode(greenledPin, OUTPUT);
  Myservo.attach(servoPin);
  
  // Initialize turn off for all LED 
  digitalWrite(redledPin, LOW);
  digitalWrite(yellowledPin, LOW);
  digitalWrite(greenledPin, LOW);
}

// Loop the system
void loop() {

  // Reconnect to the V-One using MQTT if failed
  if (!voneClient.connected()) {
    voneClient.reconnect();
    voneClient.publishDeviceStatusEvent(DHT11Sensor, true);
    voneClient.publishDeviceStatusEvent(MoistureSensor, true);
    voneClient.publishDeviceStatusEvent(WaterLevel, true);
    voneClient.publishDeviceStatusEvent(RainSensor, true);
  }
  voneClient.loop();

  unsigned long cur = millis();
  if (cur - lastMsgTime > INTERVAL) {
    lastMsgTime = cur;

    // Read Rain Sensor (Analog)
    int rainValue = analogRead(rainSensorPin);
    Serial.print("Rain Sensor Value: ");
    Serial.println(rainValue);
    voneClient.publishTelemetryData(RainSensor, "Raining", rainValue);

    // Stop the system if Rain Value is greater than 2600
    if (rainValue > rainThreshold) {
      // No significant rain detected
      Serial.println("No rain detected. System inactive.");

      // Set all outputs to inactive
      digitalWrite(redledPin, LOW);
      digitalWrite(yellowledPin, LOW);
      digitalWrite(greenledPin, LOW);
      Myservo.write(0);  // Ensure the valve is fully open
      return;  // Skip further processing
    }

    // Continue the system if Rain Value is lower than 2600
    // Rain detected
    Serial.println("Rain detected. System active.");

    // Read DHT11 Sensor
    float h = dht.readHumidity();
    int t = dht.readTemperature();
    JSONVar payloadObject;
    payloadObject["Humidity"] = h;
    payloadObject["Temperature"] = t;
    voneClient.publishTelemetryData(DHT11Sensor, payloadObject);

    // Read Moisture Sensor
    int sensorValue = analogRead(moisturePin);
    Moisture = map(sensorValue, MinMoistureValue, MaxMoistureValue, MinMoisture, MaxMoisture);
    voneClient.publishTelemetryData(MoistureSensor, "Soil moisture", Moisture);

    // Read Water Level Sensor
    int waterLevelsensor = analogRead(depthPin);
    depth = map(waterLevelsensor, MinDepthValue, MaxDepthValue, MinDepth, MaxDepth);
    voneClient.publishTelemetryData(WaterLevel, "Depth", depth);

    // Control LEDs and Buzzer based on water level
    if (depth > 75) {
      // Flood state
      // Open the Red LED light while close others
      digitalWrite(redledPin, HIGH);
      digitalWrite(yellowledPin, LOW);
      digitalWrite(greenledPin, LOW);
      Myservo.write(90);  // Open valve or door for water flow

      // Make the Red LED blinking
      static unsigned long previousMillis = 0;
      static const long interval = 1000;
      if (millis() - previousMillis >= interval) {
        previousMillis = millis();
        digitalWrite(redledPin, !digitalRead(redledPin));  // Toggle Red LED
      } else {
      }
    } else if (depth > 45) {
      // Warning state
      digitalWrite(redledPin, LOW);
      digitalWrite(greenledPin, LOW);
      Myservo.write(90);  // Open valve or door for water flow

      // Check if humidity or moisture level exceed the threshold
      // If greater than threshold, then blink Yellow LED light
      // If lower than threshold, then steady Yellow LED Light
      if (Moisture > moistureThreshold) {
        static unsigned long previousMillisYellow = 0;
        static const long intervalYellow = 1000;
        if (millis() - previousMillisYellow >= intervalYellow) {
          previousMillisYellow = millis();
          digitalWrite(yellowledPin, !digitalRead(yellowledPin));  // Toggle Yellow LED
        }
      } else {
        digitalWrite(yellowledPin, HIGH);  // Yellow LED steady
      }
    } else {
      // Safe state
      // Turn on Green LED while turn off others
      digitalWrite(redledPin, LOW);
      digitalWrite(yellowledPin, LOW);
      digitalWrite(greenledPin, HIGH);
      Myservo.write(0);  // Close valve or door
    }
  }
}
