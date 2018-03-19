extern "C"
{
#include "user_interface.h"  	  // Required for wifi_station_connect() to work
}

#include <ESP8266WiFi.h>          // https://github.com/esp8266/Arduino
#include <WiFiUdp.h>
#include <time.h>

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager
#include <NTPClient.h>			  // https://github.com/arduino-libraries/NTPClient
#include <Timezone.h>    		  // https://github.com/JChristensen/Timezone

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
WiFiManager wifiManager;

int firstrun = 1;

// configure your timezone rules here how described on https://github.com/JChristensen/Timezone
TimeChangeRule myDST =
{ "CEST", Last, Sun, Mar, 2, +120 };    //Daylight time = UTC + 2 hours
TimeChangeRule mySTD =
{ "CET", Last, Sun, Oct, 2, +60 };      //Standard time = UTC + 1 hours
Timezone myTZ(myDST, mySTD);

void setup()
{
	pinMode(BUILTIN_LED, OUTPUT);   // Initialize the BUILTIN_LED pin as an output
	digitalWrite(BUILTIN_LED, HIGH);
	Serial.begin(115200);

	wifiManager.setTimeout(180);

	//fetches ssid and pass and tries to connect
	//if it does not connect it starts an access point with the specified name
	//here  "NixieAP"
	//and goes into a blocking loop awaiting configuration

	if (!wifiManager.autoConnect("NixieAP"))
	{
		Serial.println("failed to connect and hit timeout");
		delay(3000);
		//reset and try again, or maybe put it to deep sleep
		ESP.reset();
		delay(5000);
	}

	//if you get here you have connected to the WiFi
	Serial.println("connected...yeey :)");

	Serial1.begin(9600);

	timeClient.update();
}

void loop()
{
	char tstr[128];
	unsigned char cs;
	unsigned int i;
	time_t rawtime, loctime;
	unsigned long amicros, umicros = 0;

	for (;;)
	{
		amicros = micros();
		if (timeClient.update())						// NTP-update
		{
			umicros = amicros;
			rawtime = timeClient.getEpochTime();		// get NTP-time
			loctime = myTZ.toLocal(rawtime);			// calc local time

			if ((!second(loctime)) || firstrun)			// full minute or first cycle
			{
				digitalWrite(BUILTIN_LED, HIGH);		// blink for sync
				sprintf(tstr, "$GPRMC,%02d%02d%02d,A,0000.0000,N,00000.0000,E,0.0,0.0,%02d%02d%02d,0.0,E,S",
						hour(loctime), minute(loctime), second(loctime), day(loctime), month(loctime), year(loctime));
				cs = 0;
				for (i = 1; i < strlen(tstr); i++)		// calculate checksum
					cs ^= tstr[i];
				sprintf(tstr + strlen(tstr), "*%02X", cs);
				Serial.println(tstr);					// send to console
				Serial1.println(tstr);					// send to clock
				delay(100);
				digitalWrite(BUILTIN_LED, LOW);
				delay(58000 - ((micros() - amicros) / 1000) - (second(loctime) * 1000)); // wait for end of minute
				firstrun = 0;
			}
		}
		delay(200);
		if (((amicros - umicros) / 1000000L) > 3600)	// if no sync for more than one hour
			digitalWrite(BUILTIN_LED, HIGH);			// switch off LED
	}
}

