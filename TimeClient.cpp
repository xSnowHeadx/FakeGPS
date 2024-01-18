//=== WIFI MANAGER ===
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager
#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "NTPCLient.h"

char wifiManagerAPName[] = "FakeGPS";
char wifiManagerAPPassword[] = "FakeGPS";

//== DOUBLE-RESET DETECTOR ==
#include <DoubleResetDetector.h>
#define DRD_TIMEOUT 10 // Second-reset must happen within 10 seconds of first reset to be considered a double-reset
#define DRD_ADDRESS 0 // RTC Memory Address for the DoubleResetDetector to use
DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

//=== NTP CLIENT ===
#include "TimeClient.h"

#define DEBUG 1

const char *geo_location_api = "http://www.ip-api.com/line/?fields=offset";

HTTPClient http;
WiFiClient wifiClient;
WiFiUDP ntpUDP;
NTPClient ntpClient(ntpUDP);
String payload;

unsigned long askFrequency = 60 * 60 * 1000; // How frequent should we get current time? in miliseconds. 60minutes = 60*60s = 60*60*1000ms
unsigned long timeToAsk;
unsigned long timeToRead;
unsigned long lastEpoch; // We don't want to continually ask for epoch from time server, so this is the last epoch we received (could be up to an hour ago based on askFrequency)
unsigned long lastEpochTimeStamp; // What was millis() when asked server for Epoch we are currently using?
unsigned long nextEpochTimeStamp; // What was millis() when we asked server for the upcoming epoch
unsigned long currentTime;
struct tm local_tm;

//== PREFERENCES == (Fill these appropriately if you could not connect to the ESP via your phone)
char homeWifiName[] = ""; // PREFERENCE: The name of the home WiFi access point that you normally connect to.
char homeWifiPassword[] = ""; // PREFERENCE: The password to the home WiFi access point that you normally connect to.
bool error_getTime = false;

unsigned long date_time = 0;

TimeClient::TimeClient()
{
}

void TimeClient::Setup(void)
{
	//-- WiFiManager --
	//Local intialization. Once its business is done, there is no need to keep it around
	WiFiManager wifiManager;
	//wifiManager.resetSettings(); // Uncomment this to reset saved WiFi credentials.  Comment it back after you run once.
	//wifiManager.setBreakAfterConfig(true); // Get out of WiFiManager even if we fail to connect after config.  So our Hail Mary pass could take care of it.
	//wifiManager.setSaveConfigCallback(saveConfigCallback);

	int connectionStatus = WL_IDLE_STATUS;

	if (strlen(homeWifiName) > 0)
	{
		Serial.println("USING IN SKETCH CREDENTIALS:");
		Serial.println(homeWifiName);
		Serial.println(homeWifiPassword);
		connectionStatus = WiFi.begin(homeWifiName, homeWifiPassword);
		Serial.print("WiFi.begin returned ");
		Serial.println(connectionStatus);
	}
	else
	{

		//-- Double-Reset --
		if (drd.detectDoubleReset())
		{
			Serial.println("DOUBLE Reset Detected");
			digitalWrite(LED_BUILTIN, LOW);
			WiFi.disconnect();
			connectionStatus = wifiManager.startConfigPortal(wifiManagerAPName, wifiManagerAPPassword);
			Serial.print("startConfigPortal returned ");
			Serial.println(connectionStatus);
		}
		else
		{
			Serial.println("SINGLE reset Detected");
			digitalWrite(LED_BUILTIN, HIGH);
			//fetches ssid and pass from eeprom and tries to connect
			//if it does not connect it starts an access point with the specified name wifiManagerAPName
			//and goes into a blocking loop awaiting configuration
			connectionStatus = wifiManager.autoConnect(); //wifiManagerAPName, wifiManagerAPPassword);
			Serial.print("autoConnect returned ");
			Serial.println(connectionStatus);
		}
	}

	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		Serial.print(".");
	}

	// Hail Mary pass. If WiFiManager fail to connect user to home wifi, connect manually :-(
	//  if (WiFi.status() != WL_CONNECTED) {
	//     Serial.println("Hail Mary!");
	//
	//     ETS_UART_INTR_DISABLE();
	//      wifi_station_disconnect();
	//      ETS_UART_INTR_ENABLE();
	//
	//     WiFi.begin(homeWifiName, homeWifiPassword);
	//     Serial.println("Connected?");
	//  }

	//-- Status --
	Serial.print("WiFi.status() = ");
	Serial.println(WiFi.status());

	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());

	drd.stop();
	delay(3000);
}

void TimeClient::AskCurrentEpoch()
{
	int httpCode;
	int offset = 0;

	if (DEBUG)
		Serial.println("Retrieving timezone offset");

	http.begin(wifiClient, geo_location_api);
	httpCode = http.GET();

	payload = "";
	if (httpCode > 0)
	{
		payload = http.getString();
	}
	http.end();

	if (DEBUG)
		Serial.println(payload.c_str());

	if (payload.length())
	{
		auto error = (sscanf(payload.c_str(), "%d", &offset) != 1);
		if (error)
		{
			if (DEBUG)
				Serial.println("scan timezone offset failed");
			offset = 0;
		}
		else
		{
//			ntpClient.offset = offset;
			ntpClient.update();
			if (date_time = ntpClient.getEpochTime()) // get NTP-time
			{
				date_time += offset + 1;
				error_getTime = true;
			}
			else
			{
				error_getTime = false;
			}
			if (DEBUG)
				Serial.println(date_time);
		}
	}
}

unsigned long TimeClient::ReadCurrentEpoch()
{
	lastEpoch = date_time;
	lastEpochTimeStamp = nextEpochTimeStamp;
	return lastEpoch;
}

unsigned long TimeClient::GetCurrentTime()
{
	//if (DEBUG) Serial.println("GetCurrentTime called");
	unsigned long timeNow = millis();
	if (timeNow > timeToAsk || !error_getTime)
	{ // Is it time to ask server for current time?
		if (DEBUG)
			Serial.println(" Time to ask");
		timeToAsk = timeNow + askFrequency; // Don't ask again for a while
		if (timeToRead == 0)
		{ // If we have not asked...
			timeToRead = timeNow + 1000; // Wait one second for server to respond
			AskCurrentEpoch(); // Ask time server what is the current time?
			nextEpochTimeStamp = millis(); // next epoch we receive is for "now".
		}
	}

	if (timeToRead > 0 && timeNow > timeToRead) // Is it time to read the answer of our AskCurrentEpoch?
	{
		// Yes, it is time to read the answer.
		ReadCurrentEpoch(); // Read the server response
		timeToRead = 0; // We have read the response, so reset for next time we need to ask for time.
	}

	if (lastEpoch != 0)
	{  // If we don't have lastEpoch yet, return zero so we won't try to display millis on the clock
		unsigned long elapsedMillis = millis() - lastEpochTimeStamp;
		currentTime = lastEpoch + (elapsedMillis / 1000);
	}

	if (DEBUG && digitalRead(0) == LOW)
		currentTime -= 3600;
	return currentTime;
}

byte TimeClient::GetHours()
{
	return (currentTime % 86400L) / 3600;
}

byte TimeClient::GetMinutes()
{
	return (currentTime % 3600) / 60;
}

byte TimeClient::GetSeconds()
{
	return currentTime % 60;
}

void TimeClient::PrintTime()
{
#if (DEBUG)
	// print the hour, minute and second:
	Serial.print("The local time is: ");
	byte hh = GetHours();
	byte mm = GetMinutes();
	byte ss = GetSeconds();

	Serial.print(hh); // print the hour (86400 equals secs per day)
	Serial.print(':');
	if (mm < 10)
	{
		// In the first 10 minutes of each hour, we'll want a leading '0'
		Serial.print('0');
	}
	Serial.print(mm); // print the minute (3600 equals secs per minute)
	Serial.print(':');
	if (ss < 10)
	{
		// In the first 10 seconds of each minute, we'll want a leading '0'
		Serial.print('0');
	}
	Serial.println(ss); // print the second
#endif
}
