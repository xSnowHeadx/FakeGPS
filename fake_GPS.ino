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
#include "WTAClient.h"			  // https://github.com/arduino-libraries/NTPClient

WTAClient wtaClient;
WiFiManager wifiManager;
time_t locEpoch = 0, netEpoch = 0;

int firstrun = 1;

void setup()
{
	pinMode(LED_BUILTIN, OUTPUT);   // Initialize the LED_BUILTIN pin as an output
	digitalWrite(LED_BUILTIN, HIGH);
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

	wtaClient.Setup();
	askFrequency = 50;
}

void loop()
{
	char tstr[128];
	unsigned char cs;
	unsigned int i;
	struct tm *tmtime;
	unsigned long amicros, umicros = 0;

	for (;;)
	{
		amicros = micros();
		askFrequency = 60 * 60 * 1000;
		while (((netEpoch = wtaClient.GetCurrentTime()) == locEpoch) || (!netEpoch))
		{
			delay(100);
		}
		if (netEpoch)
		{
			umicros = amicros;
			tmtime = localtime(&netEpoch);

			if ((!tmtime->tm_sec) || firstrun)			// full minute or first cycle
			{
				digitalWrite(LED_BUILTIN, HIGH);		// blink for sync
				sprintf(tstr, "$GPRMC,%02d%02d%02d,A,0000.0000,N,00000.0000,E,0.0,0.0,%02d%02d%02d,0.0,E,S",
					tmtime->tm_hour, tmtime->tm_min, tmtime->tm_sec, tmtime->tm_mday, tmtime->tm_mon + 1, tmtime->tm_year);
				cs = 0;
				for (i = 1; i < strlen(tstr); i++)		// calculate checksum
					cs ^= tstr[i];
				sprintf(tstr + strlen(tstr), "*%02X", cs);
				Serial.println(tstr);					// send to console
				Serial1.println(tstr);					// send to clock
				delay(100);
				digitalWrite(LED_BUILTIN, LOW);
				delay(58000 - ((micros() - amicros) / 1000) - (tmtime->tm_sec * 1000)); // wait for end of minute
				firstrun = 0;
			}
		}
		delay(200);
		if (((amicros - umicros) / 1000000L) > 3600)	// if no sync for more than one hour
			digitalWrite(LED_BUILTIN, HIGH);			// switch off LED
	}
}

