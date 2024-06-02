#include <WiFi.h>
#include <SPIFFS.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <iostream>
#include <string.h>
#include <Stream.h>

// WiFi settings
const char* ssid = "Network";
const char* password = "4467996m";

// MQTT settings
const char* mqtt_server = "13.60.35.236";
const int mqtt_port = 1883;
const char* mqtt_user = "userTTPU";
const char* mqtt_password = "mqttpass";
const char* mqtt_topic_subscribe = "ttpu/User";
const char* mqtt_topic_publish = "ttpu/Response";

// LED pins
const int led_1 = 32;
const int led_2 = 25;
const int led_connection = 27;

WiFiClient espClient;
PubSubClient client(espClient);

String lastDownloadedFile = ""; // Global variable to store last downloaded file URL
int volume = 5; // Global variable for volume

void callback(char* topic, byte* payload, unsigned int length);
void connectToMQTT();
void downloadFile(String url);
void checkAndDeleteFile(const char* path);
void sendStatusResponse();
void playLastFile();
void defaultRing();
void stopRing();
void blinkLED(int ledPin);
void setVolume(int vol);

void setup() {
  Serial.begin(115200);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // Set up MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Set LED pins as output
  pinMode(led_1, OUTPUT);
  pinMode(led_2, OUTPUT);
  pinMode(led_connection, OUTPUT);

  // Connect to MQTT
  connectToMQTT();
}

void connectToMQTT() {
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("Connected to MQTT");
      client.subscribe(mqtt_topic_subscribe);

      // Turn on connection LED
      digitalWrite(led_connection, HIGH);
    } else {
      Serial.print("Failed to connect to MQTT, state=");
      Serial.print(client.state());
      delay(2000);

      // Turn off connection LED
      digitalWrite(led_connection, LOW);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  // Blink led_1 for 1.5 second on message receive
  blinkLED(led_1);

  // Check if the message contains a URL
  if (message.startsWith("ringbell_audio_1: ")) {
    String url = message.substring(strlen("ringbell_audio_1: "));
    Serial.print("Downloading file from: ");
    Serial.println(url);

    downloadFile(url);
  }

  // Check if the message is "status"
  if (message.equals("status")) {
    sendStatusResponse();
  }

  if (message.equals("play")) {
    playLastFile();
  }
  if (message.equals("ring")) {
    defaultRing();
  }
  if (message.equals("stop")) {
    stopRing();
  }
  if (message.startsWith("volume:")){
    int vol;
    // Extract the substring starting from index 7
    String volStr = message.substring(7);

  // Convert the substring to an integer
    vol = volStr.toInt();
    setVolume(vol);

  }
}

void sendStatusResponse() {
  // Create a JSON object
  StaticJsonDocument<512> doc;
  
  // Timestamp
  char timestampBuffer[20];
  time_t now = time(nullptr);
  struct tm * timeinfo;
  timeinfo = localtime(&now);
  sprintf(timestampBuffer, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);

  // Fill JSON object
  doc["status"] = "good";
  doc["timestamp"] = timestampBuffer;
  doc["volume"] = volume;

  // Last downloaded file URL
  if (lastDownloadedFile.length() > 0) {
    doc["last_file"] = lastDownloadedFile;
  } else {
    doc["last_file"] = nullptr;
  }

  // Connected WiFi name
  doc["wifi"] = WiFi.SSID();

  // Connected MQTT details
  doc["MQTT"] = mqtt_server;
  doc["Topic"] = mqtt_topic_subscribe;

  // Serialize JSON to a char array
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);

  // Publish JSON message
  if (client.publish(mqtt_topic_publish, jsonBuffer)) {
    Serial.println("Status response sent");
    Serial.print("JSON Message: ");
    Serial.println(jsonBuffer);

    // Blink led_2 for 1 second on response send
    blinkLED(led_2);
  } else {
    Serial.println("Failed to send status response");
  }
}

void playLastFile() {
  if (lastDownloadedFile.length() == 0 || !SPIFFS.exists("/downloaded_file.m4a")) {
    const char* msg = "There is no last downloaded file exists!!!";
    client.publish(mqtt_topic_publish, msg);
    Serial.println(msg);

    // Blink led_2 for 1 second on response send
    blinkLED(led_2);
  } else {
    String msg = "Playing last downloaded file\nFile URL: " + lastDownloadedFile;
    client.publish(mqtt_topic_publish, msg.c_str());
    Serial.println(msg);

    // Blink led_2 for 1 second on response send
    blinkLED(led_2);
  }
}

void defaultRing() {
  const char* msg = "Default audio is playing";
  client.publish(mqtt_topic_publish, msg);
  Serial.println(msg);

  // Blink led_2 for 1 second on response send
  blinkLED(led_2);
}

void stopRing() {
  const char* msg = "Stopped playing audio";
  client.publish(mqtt_topic_publish, msg);
  Serial.println(msg);

  // Blink led_2 for 1 second on response send
  blinkLED(led_2);
}

void downloadFile(String url) {
  checkAndDeleteFile("/downloaded_file.m4a"); // Check and delete existing file

  HTTPClient http;
  http.begin(url);

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    File file = SPIFFS.open("/downloaded_file.m4a", FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file for writing");
      const char* msg = "Failed to open file for writing";
      client.publish(mqtt_topic_publish, msg);
      return;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t size = http.getSize();
    uint8_t buff[128] = { 0 };

    while (http.connected() && size > 0) {
      size_t len = stream->available();
      if (len) {
        int c = stream->readBytes(buff, ((len > sizeof(buff)) ? sizeof(buff) : len));
        file.write(buff, c);
        size -= c;
      }
      delay(1);
    }
    file.close();
    Serial.println("File downloaded successfully");
    const char* msg = "File downloaded successfully";
    client.publish(mqtt_topic_publish, msg);


    // Update last downloaded file URL
    lastDownloadedFile = url;
  } else {
    Serial.printf("Failed to download file, HTTP code: %d\n", httpCode);
    const char* msg = "Failed to download file";
    client.publish(mqtt_topic_publish, msg);
  }
  http.end();
}

void checkAndDeleteFile(const char* path) {
  if (SPIFFS.exists(path)) {
    Serial.print("File exists, deleting: ");
    const char* msg = "File exists, deleting....";
    client.publish(mqtt_topic_publish, msg);
    Serial.println(path);
    if (SPIFFS.remove(path)) {
      Serial.println("File deleted successfully");
      const char* msg = "File deleted successfully....";
     client.publish(mqtt_topic_publish, msg);
    } else {
      Serial.println("Failed to delete file");
       const char* msg = "Failed to delete file....";
     client.publish(mqtt_topic_publish, msg);
    }
  }
}

void blinkLED(int ledPin) {
  digitalWrite(ledPin, HIGH);
  delay(1000); // 1 second
  digitalWrite(ledPin, LOW);
}

void setVolume(int vol) {
  volume=vol;
  const char* msg = "Volume:"+volume;
  client.publish(mqtt_topic_publish, msg);
  
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    // Turn off connection LED if WiFi is not connected
    digitalWrite(led_connection, LOW);
    // Try to reconnect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      Serial.println("Reconnecting to WiFi...");
    }
    Serial.println("Reconnected to WiFi");
  }

  if (!client.connected()) {
    connectToMQTT();
  }
  client.loop();

  // Ensure connection LED is on if both WiFi and MQTT are connected
  if (WiFi.status() == WL_CONNECTED && client.connected()) {
    digitalWrite(led_connection, HIGH);
  } else {
    digitalWrite(led_connection, LOW);
  }
}
