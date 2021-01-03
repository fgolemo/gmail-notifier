#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <WiFiClientSecure.h>
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
//needed for library
#include g<DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

const char *host = "mail.google.com"; // the Gmail server
const char *url = "/mail/feed/atom";  // the Gmail feed url
const int httpsPort = 443;            // the port to connect to the email server

const byte LED = D1;
const byte BUTTONPIN = D2;
bool lastButtonState = HIGH;
int lastGmailNumber = 0;
int currentUnread = 0;
unsigned long myTime;

//define your default values here, if there are different values in config.json, they are overwritten.
char gmail_fingerprint[64] = "D1 AF BF FE 4D D7 E0 F6 A8 38 A6 49 05 8F E9 82 07 FE 19 A0";
char gmail_credentials[64];
char gmail_refreshrate[10] = "1800"; // 30 minutes expressed as seconds

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback() {
    Serial.println("Should save config");
    shouldSaveConfig = true;
}

int getUnread() {    // a function to get the number of unread emails in your Gmail inbox
    WiFiClientSecure client; // Use WiFiClientSecure class to create TLS (HTTPS) connection
    client.setInsecure(); // necessary according to the wiki
    Serial.printf("Connecting to %s:%d ... \r\n", host, httpsPort);
    if (!client.connect(host, httpsPort)) {   // Connect to the Gmail server, on port 443
        Serial.println("Connection failed");    // If the connection fails, stop and return
        return -1;
    }

    if (client.verify(gmail_fingerprint, host)) {   // Check the SHA-1 fingerprint of the SSL certificate
        Serial.println("Certificate matches");
    } else {                                  // if it doesn't match, it's not safe to continue
        Serial.println("Certificate doesn't match");
        return -1;
    }

    Serial.print("Requesting URL: ");
    Serial.println(url);

    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Authorization: Basic " + gmail_credentials + "\r\n" +
                 "User-Agent: ESP8266\r\n" +
                 "Connection: close\r\n\r\n"); // Send the HTTP request headers

    Serial.println("Request sent");

    int unread = -1;

    while (client.connected()) {                          // Wait for the response. The response is in XML format
        client.readStringUntil('<');                        // read until the first XML tag
        String tagname = client.readStringUntil('>');       // read until the end of this tag to get the tag name
        if (tagname ==
            "fullcount") {                       // if the tag is <fullcount>, the next string will be the number of unread emails
            String unreadStr = client.readStringUntil('<');   // read until the closing tag (</fullcount>)
            unread = unreadStr.toInt();                       // convert from String to int
            break;                                            // stop reading
        }                                                   // if the tag is not <fullcount>, repeat and read the next tag
    }
    Serial.println("Connection closed");

    return unread;                                        // Return the number of unread emails
}

void setup() {
    // put your setup code here, to run once:
    Serial.begin(115200);
    Serial.println();
    myTime = millis();

    pinMode(BUTTONPIN, INPUT);
    pinMode(LED, OUTPUT);

    // flash LED one time to indicate powering on
    digitalWrite(LED, LOW);
    delay(100);
    digitalWrite(LED, HIGH);
    delay(500);
    digitalWrite(LED, LOW);

    //clean FS, for testing
//    SPIFFS.format();

    //read configuration from FS json
    Serial.println("mounting FS...");

    if (SPIFFS.begin()) {
        Serial.println("mounted file system");
        if (SPIFFS.exists("/config.json")) {
            //file exists, reading and loading
            Serial.println("reading config file");
            File configFile = SPIFFS.open("/config.json", "r");
            if (configFile) {
                Serial.println("opened config file");
                size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                configFile.readBytes(buf.get(), size);

                DynamicJsonDocument json(1024);
                auto deserializeError = deserializeJson(json, buf.get());
                serializeJson(json, Serial);
                if (!deserializeError) {

                    Serial.println("\nparsed json");
                    strcpy(gmail_credentials, json["gmail_credentials"]);
                    strcpy(gmail_fingerprint, json["gmail_fingerprint"]);
                    strcpy(gmail_refreshrate, json["gmail_refreshrate"]);
//                    strcpy(mqtt_port, json["mqtt_port"]);
//                    strcpy(blynk_token, json["blynk_token"]);
                } else {
                    Serial.println("failed to load json config");
                }
                configFile.close();
            }
        }
    } else {
        Serial.println("failed to mount FS");
    }
    //end read

    // The extra parameters to be configured (can be either global or just in the setup)
    WiFiManagerParameter custom_gmail_credentials("gmail_credentials", "GMail Credentials", gmail_credentials, 64);
    WiFiManagerParameter custom_gmail_fingerprint("gmail_fingerprint", "GMail Fingerprint", gmail_fingerprint, 64);
    WiFiManagerParameter custom_gmail_refreshrate("gmail_refreshrate", "GMail Refreshrate(s)", gmail_refreshrate, 10);

    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    //set config save notify callback
    wifiManager.setSaveConfigCallback(saveConfigCallback);

    //set static ip
//    wifiManager.setSTAStaticIPConfig(IPAddress(10, 0, 1, 99), IPAddress(10, 0, 1, 1), IPAddress(255, 255, 255, 0));

    //add all your parameters here
    wifiManager.addParameter(&custom_gmail_credentials);
    wifiManager.addParameter(&custom_gmail_fingerprint);
    wifiManager.addParameter(&custom_gmail_refreshrate);

    //reset/change settings
    if (digitalRead(BUTTONPIN) == LOW) {
        Serial.println("===RESETTING===");
        wifiManager.resetSettings();

        // flash 3 times short to indicate reset
        for (int i = 0; i<3; i++) {
            digitalWrite(LED, HIGH);
            delay(200);
            digitalWrite(LED, LOW);
            delay(100);
        }
    }
    //

    //set minimum quality of signal so it ignores AP's under that quality
    //defaults to 8%
    //wifiManager.setMinimumSignalQuality();

    //sets timeout until configuration portal gets turned off
    //useful to make it all retry or go to sleep
    //in seconds
    //wifiManager.setTimeout(120);

    //fetches ssid and pass and tries to connect
    //if it does not connect it starts an access point with the specified name
    //and goes into a blocking loop awaiting configuration
    if (!wifiManager.autoConnect("Gmail-Notifier Setup", "ladeeda123%")) {
        Serial.println("failed to connect and hit timeout");
        delay(3000);
        //reset and try again, or maybe put it to deep sleep
        ESP.reset();
        delay(5000);
    }

    //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");

    // flash 2 times long to indicate connection
    for (int i = 0; i<2; i++) {
        digitalWrite(LED, HIGH);
        delay(1000);
        digitalWrite(LED, LOW);
        delay(200);
    }

    //read updated parameters
    strcpy(gmail_credentials, custom_gmail_credentials.getValue());
    strcpy(gmail_fingerprint, custom_gmail_fingerprint.getValue());
    strcpy(gmail_refreshrate, custom_gmail_refreshrate.getValue());

    Serial.println("The values in the file are: ");
    Serial.println("\tgmail_credentials : " + String(gmail_credentials));
    Serial.println("\tgmail_fingerprint : " + String(gmail_fingerprint));
    Serial.println("\tgmail_refreshrate : " + String(gmail_refreshrate));

    //save the custom parameters to FS
    if (shouldSaveConfig) {
        Serial.println("saving config");
        DynamicJsonDocument json(1024);
        json["gmail_credentials"] = gmail_credentials;
        json["gmail_fingerprint"] = gmail_fingerprint;
        json["gmail_refreshrate"] = gmail_refreshrate;

        File configFile = SPIFFS.open("/config.json", "w");
        if (!configFile) {
            Serial.println("failed to open config file for writing");
        }

        serializeJson(json, Serial);
        serializeJson(json, configFile);
        configFile.close();
    }

    Serial.println("local ip");
    Serial.println(WiFi.localIP());
}


void loop() {
    unsigned long nuTime = millis();

    if (nuTime - myTime > String(gmail_refreshrate).toInt()*1000) {
        myTime = millis();
        currentUnread = getUnread();
        Serial.println("Got new unread number: " + String(currentUnread));

        if (currentUnread > lastGmailNumber) {
            // got new mails
            Serial.println("That's more than before... turning on LED");
            digitalWrite(LED, HIGH);
        }
        lastGmailNumber = currentUnread;
    }

    auto buttonState = digitalRead(BUTTONPIN);

    if (lastButtonState != buttonState && buttonState == HIGH) {
        Serial.println("Pressed button, deleting notification");
        digitalWrite(LED, LOW);
        lastGmailNumber = currentUnread;
    }
    lastButtonState = buttonState;
}