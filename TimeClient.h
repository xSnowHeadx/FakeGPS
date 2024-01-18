#ifndef __TIMECLIENT_H__
#define __TIMECLIENT_H__
/*

 WTA Client

 Get the time from the worldtimeapi-server to prevent timezone and DST-mess
 Demonstrates use http-client and json-parser

 created 4 Sep 2010
 by Michael Margolis
 modified 9 Apr 2012
 by Tom Igoe
 updated for the ESP8266 12 Apr 2015 
 by Ivan Grokhotkov
 Refactored into NTPClient class by Hari Wiguna, 2018
 changed from NTP to worldtimeapi by SnowHead, 2019

 This code is in the public domain.

 */
#include <Arduino.h>
#include <ESP8266WiFi.h>

extern unsigned long askFrequency;

class TimeClient {
  public:
    TimeClient();
    void Setup(void);
    unsigned long GetCurrentTime();
    byte GetHours();
    byte GetMinutes();
    byte GetSeconds();
    void PrintTime();
    bool SaveConfig(void);
    bool LoadConfig(void);
    
  private:
    void AskCurrentEpoch();
    unsigned long ReadCurrentEpoch();
 };
#endif // __TIMECLIENT_H__
