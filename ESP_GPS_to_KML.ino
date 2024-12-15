// from https://www.tastethecode.com/trail-mapping-with-gps-esp32-and-microsd-card
//see also https://www.tastethecode.com/helium-based-diy-gps-vehicle-tracker-with-rys8839-rylr993-and-esp32
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

#define RX_gps 16
#define TX_gps 17

#define trigger_pin 25
#define LED_pin 32
#define LED_position_pin 12

HardwareSerial gps_serial(2);

TinyGPSPlus gps;

double last_lat = 0;
double last_lng = 0;

const int chipSelect = 5;
bool recording = false;
bool has_valid_position = false;
String recording_file = "";

int ledState = LOW;
int buttonState;
int lastButtonState = LOW;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

void setup() {
  Serial.begin(115200);

  pinMode(LED_pin, OUTPUT);
  pinMode(LED_position_pin, OUTPUT);
  pinMode(trigger_pin, INPUT);
  digitalWrite(LED_pin, LOW);
  digitalWrite(LED_position_pin, LOW);

  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    // don't do anything more:
    while (1);
  }

  gps_serial.begin(115200, SERIAL_8N1, RX_gps, TX_gps);

  //wait a bit for serail to stabilize
  delay(1000);
}

void loop() {
  int reading = digitalRead(trigger_pin);
  if (reading != lastButtonState) {
    // reset the debouncing timer
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:

    // if the button state has changed:
    if (reading != buttonState) {
      buttonState = reading;

      // only toggle the LED if the new button state is HIGH
      if (buttonState == HIGH) {
        ledState = !ledState;
        if(recording) {
          //stop recording and finalize file
          finalizeFile();
          recording = false;
        } else {
          //start recording
          recording_file = getNextFileName(SD);
          startFile();
          recording = true;
        }
      }
    }
  }

  // set the LEDs:
  digitalWrite(LED_pin, ledState);
  digitalWrite(LED_position_pin, has_valid_position ? HIGH : LOW);

  // save the reading. Next time through the loop, it'll be the lastButtonState:
  lastButtonState = reading;

  if (Serial.available()) {
    String content = Serial.readString();
    content.trim();
    if (content.startsWith("gps:")) {
      Serial.println("Writing to GPS Module");
      sendGPSCommand(content.substring(4));
    } else if (content == "current_gps") {
      Serial.println("Current position: " + String(gps.location.lat(), 6) + ", " + String(gps.location.lng(), 6));
      Serial.println("Distance: " + String(gps.distanceBetween(gps.location.lat(), gps.location.lng(), last_lat, last_lng)));
      Serial.println("Altitude: " + String(gps.altitude.meters()));
      Serial.println("Failed Checksum: " + String(gps.failedChecksum()));
      recordGPSPosition();
    }
  }

  String gps_incomming = "";
  while (gps_serial.available() > 0) {
    char c = (char)gps_serial.read();
    gps_incomming += c;
    if (gps.encode(c)) {
      handleLocation();
    }
  }
  // if(gps_incomming != "") {
  //   Serial.println(gps_incomming);
  // }
}

String getNextFileName(fs::FS& fs) {
  int count = 1;
  File root = fs.open("/");
  if (!root) {
    Serial.println("Failed to open directory");
    return "";
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return "";
  }

  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      count++;
    }
    file = root.openNextFile();
  }
  char buffer [9];
  sprintf (buffer, "%04d.kml", count);
  return buffer;
}

void startFile() {
  String file_start = R"=="==(<?xml version="1.0" encoding="UTF-8"?>
  <kml xmlns="http://www.opengis.net/kml/2.2">
    <Document>
      <name>Paths</name>
      <description>Recorded path using the Nature Trail Mapper</description>
      <Style id="yellowLineGreenPoly">
        <LineStyle>
          <color>7f00ffff</color>
          <width>4</width>
        </LineStyle>
        <PolyStyle>
          <color>7f00ff00</color>
        </PolyStyle>
      </Style>
      <Placemark>
        <name>Recorded Path</name>
        <description>A line indicated a recorded path.</description>
        <styleUrl>#yellowLineGreenPoly</styleUrl>
        <LineString>
          <extrude>0</extrude>
          <tessellate>0</tessellate>
          <altitudeMode>clampToGround</altitudeMode>
          <coordinates>)=="==";
  writeToFile(recording_file, file_start, FILE_WRITE);
}

void finalizeFile() {
  String file_start = R"(</coordinates>
        </LineString>
      </Placemark>
    </Document>
  </kml>)";
  writeToFile(recording_file, file_start);
}

void writeToFile(String file_name, String data_to_write) {
  writeToFile(file_name, data_to_write, FILE_APPEND);
}

void writeToFile(String file_name, String data_to_write, char* mode) {
  File dataFile = SD.open("/" + file_name, mode);

  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(data_to_write);
    dataFile.close();
  }
  // if the file isn't open, pop up an error:
  else {
    Serial.println("error opening file");
  }
}

void sendGPSCommand(String command) {
  command = command + "\r\n";
  char* buf = (char*)malloc(sizeof(char) * command.length() + 1);
  Serial.println(command);
  command.toCharArray(buf, command.length() + 1);
  gps_serial.write(buf);
  free(buf);
}

void recordGPSPosition() {
  if(recording) {
    writeToFile(recording_file, String(gps.location.lng(), 6) + "," + String(gps.location.lat(), 6) + "," + String(gps.altitude.meters()));
  }
}

void handleLocation() {
  if (gps.location.isValid()) {
    has_valid_position = true;
    if (last_lat == 0 || last_lng == 0) {
      //first position, set initial values
      last_lat = gps.location.lat();
      last_lng = gps.location.lng();
      Serial.println("Initial position: " + String(last_lat, 6) + ", " + String(last_lng, 6));
      recordGPSPosition();
    } else {
      //update last position if distance is greater than 1m
      if (gps.distanceBetween(gps.location.lat(), gps.location.lng(), last_lat, last_lng) > 1) {
        last_lat = gps.location.lat();
        last_lng = gps.location.lng();
        Serial.println("Updated position: " + String(last_lat, 6) + ", " + String(last_lng, 6) + ", " + String(gps.altitude.meters()));
        recordGPSPosition();
      }
    }
  } else {
    has_valid_position = false;
  }
}

