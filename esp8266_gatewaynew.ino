 /*

UKHASnet Gateway Code by Phil Crump M0DNY

Based on UKHASnet rf69_repeater by James Coxon M6JCX

*/

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <ESP8266HTTPClient.h>

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal

#include <EEPROM.h>
#include <SPI.h>
#include <string.h>
#include <stdio.h>
#include "RFM69Config.h"
#include "RFM69.h"
#include "NodeConfig.h"

#define USE_SERIAL Serial

ESP8266WiFiMulti WiFiMulti;


//************* Misc Setup ****************/
uint8_t n;
uint32_t count = 1, data_interval = 2;
uint8_t data_count = 97; // 'a'
char data[64] = "3aT30V4.3[AB1]";

char string_end[] = "]";
int packet_len;

RFM69 rf69;

#ifdef ENABLE_RFM_TEMPERATURE
int8_t sampleRfmTemp() {
    int8_t rfmTemp = rf69.readTemp();
    //while(rfmTemp>100) {
      //  rfmTemp = rf69.readTemp();
    //}
    rfmTemp-=RX_TEMP_FUDGE;
    return rfmTemp;
}
#endif

int gen_Data(){

  String dataString;
  
  dataString = char(num_repeats);
  dataString += char(data_count);
  #ifdef LOCATION_STRING
  if(data_count=='a' or data_count=='z') {
      dataString += "L";
      dataString += LOCATION_STRING;
  }
  #endif
  
  #ifdef ENABLE_RFM_TEMPERATURE
  dataString += "T";
  dataString += sampleRfmTemp();
  #endif

  #ifdef ENABLE_RSSI
  //sprintf(data,"%sR%d",data,rx_rssi);
  dataString += "R";
  dataString +=  rf69.lastRssi();
  dataString += ",";
  dataString += rf69.sampleRssi();
  #endif

  dataString += "[";
  dataString += id;
  dataString += "]";

  uint8_t data_len = sizeof(data);
  dataString.toCharArray(data, data_len);
  USE_SERIAL.println(dataString);
  USE_SERIAL.printf("[DATA] packet: %s...\n", data);
  return dataString.length();
}

void setup() 
{
  //analogReference(INTERNAL); // 1.1V ADC reference
  randomSeed(analogRead(6));
  SPI.setFrequency(1000000);
  int eeprom_addr = 0;
  
   USE_SERIAL.begin(115200);
   USE_SERIAL.setDebugOutput(true);

    USE_SERIAL.println();
    USE_SERIAL.println();
    USE_SERIAL.println();

    for(uint8_t t = 4; t > 0; t--) {
        USE_SERIAL.printf("[SETUP] WAIT %d...\n", t);
        USE_SERIAL.flush();
        delay(1000);
    }
    WiFiMulti.addAP(WIFISSID, WIFIPASSWD);
    
      //WiFi.begin(WIFISSID, WIFIPASSWD);
      //WiFi.config(staticIP, gateway, subnet);
      int z = 0;
      while ((WiFiMulti.run() != WL_CONNECTED) & (z < 25))
      {
        delay(500);
        Serial.print(".");
        z++;
      }
      USE_SERIAL.println(" - done");

  while (!rf69.init()){
    delay(100);
  }

  USE_SERIAL.println("RFM Configured");

  int packet_len = gen_Data();
  
  USE_SERIAL.println("Packet Gen");
  
  rf69.send((uint8_t*)data, packet_len, rfm_power);

  USE_SERIAL.println("Packet Sent");
  
  rf69.setMode(RFM69_MODE_RX);
  rf69.SetLnaMode(RF_TESTLNA_SENSITIVE);
  
  // Send our own packet to serial port
    for (int j=0; j<packet_len; j++)
    {
        if(data[j]==']')
        {
            USE_SERIAL.println(data[j]);
            break;
        }
        else
        {
            USE_SERIAL.print(data[j]);
        }
    }

}

void loop()
{
  //USE_SERIAL.printf("[DATA] Temp %d\n", sampleRfmTemp());

  count++;
    
    for(int i=0;i<20;i++) {
      delay(50);
      
      if (rf69.checkRx()) {
        USE_SERIAL.println("Found Packet");
        uint8_t buf[64];
        uint8_t len = sizeof(buf);
        int rx_rssi;
        
        rf69.recv(buf, &len);
        rx_rssi = rf69.lastRssi();
        for (int j=0; j<len; j++) {
            USE_SERIAL.print((char)buf[j]);
            if(buf[j]==']') break;
        }
        USE_SERIAL.print("|");
        USE_SERIAL.println(rx_rssi);
        

            // wait for WiFi connection
        if((WiFiMulti.run() == WL_CONNECTED)) {

          HTTPClient http;
  
          USE_SERIAL.print("[HTTP] begin...\n");
          // configure traged server and url
  
          USE_SERIAL.print("[HTTP] POST...\n");
          // start connection and send HTTP header
  
          http.begin("http://www.ukhas.net/api/upload");
          http.addHeader("Content-Type", "application/x-www-form-urlencoded");
          
          //Build up the string to upload to server
          String uploadPacket = "origin=";
          uploadPacket += id; //gateway nodes ID
          uploadPacket += "&data=";
          
          for (int i = 0; i < len-1; i++){ 
            uploadPacket += char(buf[i]); //copy the packet from the buffer we got from rf69.recv into our upload string. There may be neater ways of doing this.
          }
          
          uploadPacket += "&rssi=";      
          uploadPacket += String(rf69.lastRssi());
          uploadPacket += "\0"; //null terminate the string for safe keeping
          
          http.POST(uploadPacket);
          
          http.writeToStream(&Serial);
          http.end();
          USE_SERIAL.println();
        }

                // find end of packet & start of repeaters
        uint8_t end_bracket = -1, start_bracket = -1;        
        for (int k=0; k<len; k++) {
          if (buf[k] == '[') {
            start_bracket = k;
          }
          else if (buf[k] == ']') {
            end_bracket = k;
            buf[k+1] = '\0';
            break;
          }
        }

        // Need to take the recieved buffer and decode it and add a reference 
        if (buf[0] > '0' && end_bracket != -1 && strstr((const char *)&buf[start_bracket], id) == NULL) {
          // Reduce the repeat value
          buf[0]--;
          
          // Add the repeater ID
          packet_len = end_bracket + sprintf((char *)&buf[end_bracket], ",%s]", id);

          //random delay to try and avoid packet collision
          delay(random(50, 800));
          
          rf69.send((uint8_t*)buf, packet_len, rfm_power);
        }
      }
    }
  
  if (count >= data_interval){
    data_count++;

    if(data_count > 122){
      data_count = 98; //'b'
    }
    
    packet_len = gen_Data();
    rf69.send((uint8_t*)data, packet_len, rfm_power);
    
    rf69.setMode(RFM69_MODE_RX);
    rf69.SetLnaMode(RF_TESTLNA_SENSITIVE);
    
    // Send our own packet to serial port
    for (int j=0; j<packet_len; j++)
    {
        if(data[j]==']') // Check for last char in packet
        {
            USE_SERIAL.println(data[j]);
            break;
        }
        else
        {
            USE_SERIAL.print(data[j]);
        }
    }

                // wait for WiFi connection
        if((WiFiMulti.run() == WL_CONNECTED)) {

          HTTPClient http;
  
          USE_SERIAL.print("[HTTP] begin...\n");
          // configure traged server and url
  
          USE_SERIAL.print("[HTTP] POST...\n");
          // start connection and send HTTP header
  
          http.begin("http://www.ukhas.net/api/upload");
          http.addHeader("Content-Type", "application/x-www-form-urlencoded");
          
          //Build up the string to upload to server
          String uploadPacket = "origin=";
          uploadPacket += id; //gateway nodes ID
          uploadPacket += "&data=";
          
          for (int i = 0; i < packet_len; i++){ 
            uploadPacket += char(data[i]); //copy the packet from the buffer we got from rf69.recv into our upload string. There may be neater ways of doing this.
          }
          
          uploadPacket += "&rssi=";      
          uploadPacket += "0";
          uploadPacket += "\0"; //null terminate the string for safe keeping
          
          http.POST(uploadPacket);
          
          http.writeToStream(&Serial);
          http.end();
          USE_SERIAL.println();
        }
        
    data_interval = random(BEACON_INTERVAL, BEACON_INTERVAL+20) + count;
  }

  if (count % 10 ==  0){
    USE_SERIAL.println(count);
  }
  
  if (count > 1000) {
    USE_SERIAL.println("Restarting...");
    delay(100);
    ESP.restart();
  }
}
