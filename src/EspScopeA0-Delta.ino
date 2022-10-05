/*
  This application is sampling analog input A0 of ESP8266
  so it then can be displayed on-line in a web browser
  Application consist of several files
  Please check repository below for details

  Repository: https://github.com/krzychb/EspScopeA0
  Version: Delta
  Flie: EspScopeA0-Delta.ino
  Revision: 0.1.0
  Date: 10-Jul-2016
  Author: krzychb at gazeta.pl

  Copyright (c) 2016 Krzysztof Budzynski. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <ADS1X15.h>

const char* ssid = "PlaNet2 SensorNet";
const char* password = "GastPass";

ESP8266WebServer server(80);


//
// Install WebSocketes library by Markus Sattler
// https://github.com/Links2004/arduinoWebSockets
//
#include <Hash.h>
#include <WebSocketsServer.h>
WebSocketsServer webSocket = WebSocketsServer(81);
uint8_t socketNumber;
unsigned long messageNumber;

// tracking of number of Wi-Fi reconnects
// and total connection time
unsigned long numberOfReconnects;
unsigned long millisConnected;

//
// Continuous sampling rate of A0 in this application is about 12 samples/ms
// Wi-Fi connection gets stuck if continuous A0 sampling is longer than 60ms
// Therefore maximum of 720 samples can be made
//
#define NUMBER_OF_SAMPLES 720  // number of samples taken
#define NUMBER_OF_CHANNELS 8
unsigned int samples[8][NUMBER_OF_SAMPLES];
unsigned int numberOfSamples = NUMBER_OF_SAMPLES;

// trigger taking samples if A0 reads greater than threshold value below
#define SAMPLE_TRESHOLD 0
unsigned int sampleTreshold = SAMPLE_TRESHOLD;

// Naximum waiting time in ms for a trigger
#define TRIGGER_TIMEOUT 500

// state machine states
unsigned int state;
#define SEQUENCE_IDLE 0x00
#define GET_SAMPLE 0x10
#define GET_SAMPLE__WAITING 0x12

unsigned long millisLastSample;
unsigned long millisWaitingForTrigger;

ADS1115 ads(0x48);

void analogSample(void)
{
  if (state == SEQUENCE_IDLE)
  {
    return;
  }
  else if (state == GET_SAMPLE)
  {
    millisWaitingForTrigger = millis();
    state = GET_SAMPLE__WAITING;
    return;
  }
  else if (state == GET_SAMPLE__WAITING)
  {
    if (millis() > millisWaitingForTrigger + TRIGGER_TIMEOUT)
    {
      String message = "# " + String(messageNumber) + " -1";
      webSocket.sendTXT(socketNumber, message);
      state = SEQUENCE_IDLE;
      return;
    }
    // ninimum period to restart sampling is 1ms
    else if (millis() > millisLastSample)
    {
      String message = "# " + String(messageNumber) + " ";
      samples[0][0] = analogRead(A0);
      for(int j = 0; j < 8; j++) {
        if (samples[j][0] > sampleTreshold)
        {
          for (int i = 1; i < numberOfSamples; i++)
          {
            samples[j][i] = 0;
            if(0 == j) {
              samples[j][i] = ads.readADC(0);
            } else if(1 == j){ 
              samples[j][i] = ads.readADC(1);
            } else if(2 == j) {
              samples[j][i] = ads.readADC(2);
            } else if(3 == j) {
              samples[j][i] = ads.readADC(3);
            } else {
              samples[j][i] = analogRead(A0);
            }           
          

          message = message + String(samples[j][i]) + ";";
          
          }

        }
      }
      message += " ";
      message[message.length() - 1] = '\0';
      webSocket.sendTXT(socketNumber, message);
      state = SEQUENCE_IDLE;
      millisLastSample = millis();
    }
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t lenght)
{
  switch (type)
  {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      //
      state = SEQUENCE_IDLE;
      break;
    case WStype_CONNECTED:
      Serial.printf("[%u] Connected from ", num);
      Serial.println(webSocket.remoteIP(num));
      // send message back to client
      webSocket.sendTXT(num, "Connected");
      break;
    case WStype_TEXT:
      //
      // Format of message to process
      // # MESSAGE_NUMBER SAMPLE_TRESHOLD NUMBER_OF_SAMPLES
      // other fromats are ignored
      //
            if (payload[0] == '#')
      {
        char* token = strtok((char*) &payload[2], " ");
        messageNumber = (unsigned long) strtol(token, NULL, 10);
        token = strtok(NULL, " ");
        sampleTreshold = (unsigned int) strtol(token, NULL, 10);
        token = strtok(NULL, " ");
        numberOfSamples = (unsigned int) strtol(token, NULL, 10);
        //
        // do not exceed the size of table to store samples
        //
        if (numberOfSamples > NUMBER_OF_SAMPLES)
        {
          numberOfSamples = NUMBER_OF_SAMPLES;
        }
        socketNumber = num;
        state = GET_SAMPLE;
      }
      else
      {
        Serial.printf("[%u] get Text: %s\n", num, payload);
      }
      break;
    default:
      Serial.println("Case?");
      break;
  }
}


//
// Monitor Wi-Fi connection if it is alive
// If not alivem then wait until it reconnects
//
void isWiFiAlive(void)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.print("not connected");
    while (WiFi.status() != WL_CONNECTED)
    {
      Serial.print(".");
      delay(500);
    }
    numberOfReconnects++;
    millisConnected = millis();
  }
}


void setup(void)
{
  Serial.begin(115200);
  Serial.println();
  Serial.println("EspScopeA0-Delta 0.1.0");

  SPIFFS.begin();

  Serial.printf("Connecting to %s\n", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());

  server.onNotFound([]() {
    Serial.println("HTTP > Not Found");
    server.send(404, "text/plain", "FileNotFound");
  });

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSockets started");

  server.begin();
  server.serveStatic("/", SPIFFS, "/", "no-cache");
  Serial.println("HTTP server started");

  if(ads.begin()) {
    Serial.printf("ADS11115 started sucessfully\n");
  } else {
    Serial.printf("ADS1115 not started\n");
  }
  ads.setGain(0);      // 6.144 volt
  ads.setDataRate(7);  // fast
  ads.setMode(0);      // continuous mode
  ads.readADC(0);      // first read to trigger
  ads.readADC(1);      // first read to trigger
  ads.readADC(2);      // first read to trigger
  ads.readADC(3);      // first read to trigger
  if(ads.isConnected()) {
    Serial.println("ADS1115 conntected at addr 0x48\n");
  } else {
    Serial.println("ADS1115 not conntected\n");
  }

  Serial.print("Open http://");
  Serial.print(WiFi.localIP());
  Serial.println("/ to see the scope");
}


void loop(void)
{
  isWiFiAlive();
  server.handleClient();
  webSocket.loop();
  analogSample();
}
