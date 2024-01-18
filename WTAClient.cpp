//=== WIFI MANAGER ===
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> //https://github.com/tzapu/WiFiManager
#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

char wifiManagerAPName[] = "FakeGPS";
char wifiManagerAPPassword[] = "FakeGPS";

//== DOUBLE-RESET DETECTOR ==
#include <DoubleResetDetector.h>
#define DRD_TIMEOUT 10 // Second-reset must happen within 10 seconds of first reset to be considered a double-reset
#define DRD_ADDRESS 0 // RTC Memory Address for the DoubleResetDetector to use
DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

//=== WTA CLIENT ===
#include "WTAClient.h"

#define DEBUG 1

// your private API-Keys here
const char *tz_api_key =	   	"your_timezone_API_key"; 		// see https://www.abstractapi.com/api/time-date-timezone-api
const char *gl_api_key =		"your_geolocatiion_API_key"; 	// see https://www.abstractapi.com/api/ip-geolocation-api

char fix_tz[32] =				"";								// fill out for a fix local timezone
const char *default_location =	"Berlin";						// fallback location in case of failed geolocation retrieving

const char *geo_location_api =	"http://ipgeolocation.abstractapi.com/v1/?";
const char *time_zone_api = 	"http://timezone.abstractapi.com/v1/current_time/?";

HTTPClient http;
WiFiClient wifiClient;
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

void WTAClient::AskCurrentEpoch()
{
	char request[256];
	int httpCode;
	char local_tz[32] = "";

	if(!strlen(fix_tz))
	{
		DynamicJsonDocument jsonIPDocument(JSON_OBJECT_SIZE(42) + 1024);

		if (DEBUG) Serial.println("Retrieving public IP");
		sprintf(request, "%sapi_key=%s", geo_location_api, gl_api_key);
	//	strcpy(request, geo_location_api);
		if (DEBUG) Serial.println(request);
		http.begin(wifiClient, request);
		httpCode = http.GET();

		payload = "";
		if (httpCode > 0)
		{
			payload = http.getString();
		}
		http.end();

		if (DEBUG) Serial.println(payload.c_str());

		if(payload.length())
		{
			auto error = deserializeJson(jsonIPDocument, payload.c_str());
			if (error)
			{
				if (DEBUG) Serial.println("parseObject() IP failed");
			}
			else
			{
				const char *ip_got = jsonIPDocument["timezone"]["name"];
				strcpy(local_tz, ip_got);
			}
		}
	}
	else
	{
		strcpy(local_tz, fix_tz);
	}
	if(!strlen(local_tz))
	{
		strcpy(local_tz, default_location);
		if (DEBUG) Serial.print("no TZ retrieved. Use timezone ");
	}
	else
	{
		if (DEBUG) Serial.print("retrieved TZ: ");
	}

	if (DEBUG) Serial.println(local_tz);

	sprintf(request, "%sapi_key=%s&location=%s", time_zone_api, tz_api_key, local_tz);

//	sprintf(request, "%sapiKey=%s", time_zone_api, api_key);
	if (DEBUG) Serial.println(request);
	http.begin(wifiClient, request);
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
	const char *date_time = NULL;

	if (DEBUG) Serial.println("ReadCurrentEpoch called");

	if (!cb)
	{
		error_getTime = false;
		if (DEBUG) Serial.println("no packet yet");
	}
	else
	{
		const size_t capacity = JSON_OBJECT_SIZE(15) + 200;
		DynamicJsonDocument jsonDocument(capacity);

		error_getTime = true;
		if (DEBUG) Serial.print("time answer received, length=");
		if (DEBUG) Serial.println(cb);
		// We've received a time, read the data from it
		// the timedate are JSON values

		auto error = deserializeJson(jsonDocument, payload.c_str());
//		Serial.println(payload.c_str());
		if (error)
		{
			if (DEBUG) Serial.println("parseObject() failed");
		}
		else
		{
			date_time = jsonDocument["datetime"];
		}
		if(date_time)
		{
			int d, m, y, H, M, S;

			if (DEBUG)
			{
				Serial.print("local time = ");
				Serial.println(date_time);
				if(sscanf(date_time, "%d-%d-%d %d:%d:%d", &y, &m, &d, &H, &M, &S) == 6)
				{
					local_tm.tm_year = y -1900;
					local_tm.tm_mon = m - 1;
					local_tm.tm_mday = d;
					local_tm.tm_hour = H;
					local_tm.tm_min = M;
					local_tm.tm_sec = S;
					lastEpoch = mktime(&local_tm);
				}
			}
			lastEpochTimeStamp = nextEpochTimeStamp;
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
		if (DEBUG) Serial.println(" Time to ask");
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

	if (DEBUG && digitalRead(0) == LOW) currentTime -= 3600;
	return currentTime;
}

byte WTAClient::GetHours()
{
	return (currentTime % 86400L) / 3600;
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
