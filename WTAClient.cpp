//=== WIFI MANAGER ===
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

char wifiManagerAPName[] = "FakeGPS-AP";
char wifiManagerAPPassword[] = "";

//== DOUBLE-RESET DETECTOR ==
#include <DoubleResetDetector.h>
#define DRD_TIMEOUT 10 // Second-reset must happen within 10 seconds of first reset to be considered a double-reset
#define DRD_ADDRESS 0 // RTC Memory Address for the DoubleResetDetector to use
DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

//=== WTA CLIENT ===
#include "WTAClient.h"
#include "WiFiClient.h"

#define WTAServerName  "http://worldtimeapi.org/api/"
HTTPClient http;
WiFiClient wifiClient;
String payload;
bool military = true;

unsigned long askFrequency = 60 * 60 * 1000; // How frequent should we get current time? in miliseconds. 60minutes = 60*60s = 60*60*1000ms
unsigned long timeToAsk;
unsigned long timeToRead;
unsigned long lastEpoch; // We don't want to continually ask for epoch from time server, so this is the last epoch we received (could be up to an hour ago based on askFrequency)
unsigned long lastEpochTimeStamp; // What was millis() when asked server for Epoch we are currently using?
unsigned long nextEpochTimeStamp; // What was millis() when we asked server for the upcoming epoch
unsigned long currentTime;

//== PREFERENCES == (Fill these appropriately if you could not connect to the ESP via your phone)
char homeWifiName[] = ""; // PREFERENCE: The name of the home WiFi access point that you normally connect to.
char homeWifiPassword[] = ""; // PREFERENCE: The password to the home WiFi access point that you normally connect to.
char timezone[] = "ip"; // PREFERENCE: TimeZone. Go to http://worldtimeapi.org/api/timezone to find your timezone string or chose "ip" to use IP-localisation for timezone detection
bool error_getTime = false;

WTAClient::WTAClient()
{
}

void WTAClient::Setup(void)
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
			Serial.println("DOUBLE reset Detected");
			digitalWrite(LED_BUILTIN, HIGH);
			//fetches ssid and pass from eeprom and tries to connect
			//if it does not connect it starts an access point with the specified name wifiManagerAPName
			//and goes into a blocking loop awaiting configuration
			connectionStatus = wifiManager.autoConnect(); //wifiManagerAPName, wifiManagerAPPassword);
			Serial.print("autoConnect returned ");
			Serial.println(connectionStatus);
		}
		else
		{
			Serial.println("SINGLE Reset Detected");
			digitalWrite(LED_BUILTIN, LOW);
			WiFi.disconnect();
			connectionStatus = wifiManager.startConfigPortal(wifiManagerAPName, wifiManagerAPPassword);
			Serial.print("startConfigPortal returned ");
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

void WTAClient::AskCurrentEpoch()
{
	char url[64] = WTAServerName;
	int httpCode;

	//if (DEBUG) Serial.println("AskCurrentEpoch called");
	Serial.println("AskCurrentEpoch called");

	strcat(url, timezone);
	http.begin(wifiClient, url);
	httpCode = http.GET();

	payload = "";
	if (httpCode > 0)
	{
		payload = http.getString();
	}
	http.end();
}

unsigned long WTAClient::ReadCurrentEpoch()
{
	int cb = payload.length();

#if (DEBUG)
	Serial.println("ReadCurrentEpoch called");
#endif

	if (!cb)
	{
		error_getTime = false;
#if (DEBUG)
		Serial.println("no packet yet");
#endif
	}
	else
	{
		const size_t capacity = JSON_OBJECT_SIZE(15) + 400;
		DynamicJsonDocument jsonDocument(capacity);

		error_getTime = true;
		Serial.print("time answer received, length=");
		Serial.println(cb);
		// We've received a time, read the data from it
		// the timedate are JSON values

		auto error = deserializeJson(jsonDocument, payload.c_str());
		if (error)
		{
			Serial.println("parseObject() failed");
		}
		else
		{
			//      int week_number = jsonDocument["week_number"]; // 31
			//      const char* utc_offset = jsonDocument["utc_offset"]; // "-04:00"
			//      const char* utc_datetime = jsonDocument["utc_datetime"]; // "2019-08-01T16:58:40.68279+00:00"
						long unixtime = jsonDocument["unixtime"]; // 1564678720
			//      const char* timezone = jsonDocument["timezone"]; // "America/New_York"
						int raw_offset = jsonDocument["raw_offset"]; // -18000
			//      const char* dst_until = jsonDocument["dst_until"]; // "2019-11-03T06:00:00+00:00"
						int dst_offset = jsonDocument["dst_offset"]; // 3600
			//      const char* dst_from = jsonDocument["dst_from"]; // "2019-03-10T07:00:00+00:00"
			//      bool dst = jsonDocument["dst"]; // true
			//      int day_of_year = jsonDocument["day_of_year"]; // 213
			//      int day_of_week = jsonDocument["day_of_week"]; // 4
			//      const char* datetime = jsonDocument["datetime"]; // "2019-08-01T12:58:40.682790-04:00"
			//      const char* client_ip = jsonDocument["client_ip"]; // "23.235.227.109"
			//      const char* abbreviation = jsonDocument["abbreviation"]; // "EDT"

			// now convert WTA time into everyday time:
#if (DEBUG)
			Serial.print("Unix time = ");
#endif
			lastEpoch = unixtime + raw_offset + dst_offset;
			lastEpochTimeStamp = nextEpochTimeStamp;
#if (DEBUG)
			Serial.println(lastEpoch);
#endif
		}
		return lastEpoch;
	}
	return 0;
}

unsigned long WTAClient::GetCurrentTime()
{
	//if (DEBUG) Serial.println("GetCurrentTime called");
	unsigned long timeNow = millis();
	if (timeNow > timeToAsk || !error_getTime)
	{ // Is it time to ask server for current time?
#if (DEBUG)
		Serial.println(" Time to ask");
#endif
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

#if (DEBUG)
	if (digitalRead(0) == LOW)
		currentTime -= 3600;
#endif
	return currentTime;
}

byte WTAClient::GetHours()
{
	int hours = (currentTime % 86400L) / 3600;

	// Convert to AM/PM if military time option is off
	if (!military)
	{
		if (hours == 0)
			hours = 12; // Midnight in military time is 0:mm, but we want midnight to be 12:mm
		if (hours > 12)
			hours -= 12; // After noon 13:mm should show as 01:mm, etc...
	}
	return hours;
}

byte WTAClient::GetMinutes()
{
	return (currentTime % 3600) / 60;
}

byte WTAClient::GetSeconds()
{
	return currentTime % 60;
}

void WTAClient::PrintTime()
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
