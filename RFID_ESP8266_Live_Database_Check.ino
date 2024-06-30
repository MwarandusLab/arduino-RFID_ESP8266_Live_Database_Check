#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>

const char *ssid = "IOT";
const char *password = "30010231";

const int BUFFER_SIZE = 14;       // RFID DATA FRAME FORMAT: 1byte head (value: 2), 10byte data (2byte version + 8byte tag), 2byte checksum, 1byte tail (value: 3)
const int DATA_SIZE = 10;         // 10byte data (2byte version + 8byte tag)
const int DATA_VERSION_SIZE = 2;  // 2byte version (actual meaning of these two bytes may vary)
const int DATA_TAG_SIZE = 8;      // 8byte tag
const int CHECKSUM_SIZE = 2;      // 2byte checksum

SoftwareSerial ssrfid = SoftwareSerial(12, 8);

uint8_t buffer[BUFFER_SIZE];  // used to store an incoming data frame
int buffer_index = 0;
boolean multipleRead = false;
unsigned long lastTagTime = 0;

WiFiClientSecure client;  // Declare the client instance globally

void setup() {
  Serial.begin(9600);
  ssrfid.begin(9600);
  ssrfid.listen();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  delay(1000);

  Serial.println("WiFi connected");
  Serial.println("IP address: " + WiFi.localIP().toString());
}

void loop() {
  if (ssrfid.available() > 0) {
    bool call_extract_tag = false;

    int ssvalue = ssrfid.read();  // read
    if (ssvalue == -1) {          // no data was read
      return;
    }

    if (ssvalue == 2) {  // RDM630/RDM6300 found a tag => tag incoming
      buffer_index = 0;
    } else if (ssvalue == 3) {  // tag has been fully transmitted
      call_extract_tag = true;  // extract tag at the end of the function call
      multipleRead = true;
    }

    if (buffer_index >= BUFFER_SIZE) {  // checking for a buffer overflow (It's very unlikely that an buffer overflow comes up!)
      Serial.println("Error: Buffer overflow detected!");
      return;
    }

    buffer[buffer_index++] = ssvalue;  // everything is alright => copy current value to buffer

    if (call_extract_tag == true) {
      if (buffer_index == BUFFER_SIZE) {
        unsigned tag = extract_tag();
      } else {  // something is wrong... start again looking for preamble (value: 2)
        buffer_index = 0;
        return;
      }
    }
  }
  // Check if a timeout occurred and reset the buffer if needed
  if (millis() - lastTagTime > 5000) {  // Adjust the timeout value as needed (5000 milliseconds = 5 seconds)
    buffer_index = 0;
    lastTagTime = millis();
    multipleRead = false;
  }

  if (multipleRead) {
    while (ssrfid.available() > 0) {
      int ssvalue = ssrfid.read();  // read
      if (ssvalue == -1) {          // no data was read
        break;
      }
    }
    for (int x = 0; x < 14; x++) {
      buffer[x] = 0;
    }
    multipleRead = false;
  }
}

unsigned extract_tag() {
  uint8_t msg_head = buffer[0];
  uint8_t *msg_data = buffer + 1;  // 10 byte => data contains 2byte version + 8byte tag
  uint8_t *msg_data_version = msg_data;
  uint8_t *msg_data_tag = msg_data + 2;
  uint8_t *msg_checksum = buffer + 11;  // 2 byte
  uint8_t msg_tail = buffer[13];

  // print message that was sent from RDM630/RDM6300
  Serial.println("--------");

  Serial.print("Message-Head: ");
  Serial.println(msg_head);

  Serial.println("Message-Data (HEX): ");
  for (int i = 0; i < DATA_VERSION_SIZE; ++i) {
    Serial.print(char(msg_data_version[i]));
  }
  Serial.println(" (version)");
  for (int i = 0; i < DATA_TAG_SIZE; ++i) {
    Serial.print(char(msg_data_tag[i]));
  }
  Serial.println(" (tagID)");

  Serial.print("Message-Checksum (HEX): ");
  for (int i = 0; i < CHECKSUM_SIZE; ++i) {
    Serial.print(char(msg_checksum[i]));
  }
  Serial.println("");

  Serial.print("Message-Tail: ");
  Serial.println(msg_tail);

  Serial.println("--");

  long tagID = hexstr_to_value(msg_data_tag, DATA_TAG_SIZE);
  Serial.print("Extracted Tag: ");
  Serial.println(tagID);

  // Send the tag ID to the server
  sendTagToServer(tagID);
  delay(1000);

  long checksum = 0;
  for (int i = 0; i < DATA_SIZE; i += CHECKSUM_SIZE) {
    long val = hexstr_to_value(msg_data + i, CHECKSUM_SIZE);
    checksum ^= val;
  }
  Serial.print("Extracted Checksum (HEX): ");
  Serial.print(checksum, HEX);
  if (checksum == hexstr_to_value(msg_checksum, CHECKSUM_SIZE)) {  // compare calculated checksum to retrieved checksum
    Serial.print(" (OK)");                                         // calculated checksum corresponds to transmitted checksum!
  } else {
    Serial.print(" (NOT OK)");  // checksums do not match
  }

  Serial.println("");
  Serial.println("--------");

  return tagID;
}
long hexstr_to_value(uint8_t *str, unsigned int length) {  // converts a hexadecimal value (encoded as ASCII string) to a numeric value
  uint8_t *copy = (uint8_t *)malloc((sizeof(uint8_t) * length) + 1);
  memcpy(copy, str, sizeof(uint8_t) * length);
  copy[length] = '\0';
  // the variable "copy" is a copy of the parameter "str". "copy" has an additional '\0' element to make sure that "str" is null-terminated.
  long value = strtol((char *)copy, NULL, 16);  // strtol converts a null-terminated string to a long value
  free(copy);                                   // clean up
  return value;
}
void sendTagToServer(long tagID) {
  BearSSL::WiFiClientSecure client;  // Use WiFiClient for HTTP communication
  client.setInsecure();
  HTTPClient http;
  String serverURL = "https://safa.co.ke/upload.php";  // Replace with your server URL
  http.begin(client, serverURL);                       // Use client argument for begin() function
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String postData = "tag=" + String(tagID);  // Convert the tag ID to a String
  int httpResponseCode = http.POST(postData);
  //Serial.printf("HTTP Response code: %d\n", httpResponseCode);
  if (httpResponseCode > 0) {
    Serial.printf("Tag sent to server. HTTP Response code: %d\n", httpResponseCode);
    Serial.println("Tag Send Successfully");
    // Wait for the server response
    while (!client.available()) {
      delay(10);
    }

    // Parse the JSON response
    DynamicJsonDocument jsonBuffer(256);  // Adjust the buffer size as needed
    String response = client.readString();
    DeserializationError error = deserializeJson(jsonBuffer, response);

    if (error) {
      Serial.println("JSON parsing error");
    }

    // Check if the tagID is available in the database
    bool tagAvailable = jsonBuffer["tag_available"];

    if(tagAvailable) {
      Serial.println("Tag Found!");
    } else {
      Serial.println("Tag Not Found");
    }
  }else {
    Serial.println("Error sending tag to server.");
  }
  http.end();
}
