//
// NTPClass.cpp
// 
// Functions to call and decrypt responses from NTP Time Servers
//
//------------------------------------------------------------------------------
//  Revision History
//  ~~~~~~~~~~~~~~~~
//    2 Dec 2024 MDS Original
//
//------------------------------------------------------------------------------

#include "NTPClass.h"

// #define VERBOSE_MODE // Don't define it if we don't want the serial stuff out

//
//-----------------------------------------------------------------------------
// Constructor 
NTPClass::NTPClass() {
  t.secsSince1900 = 0;
  return;
};

//
//-----------------------------------------------------------------------------
//
void NTPClass::begin(IPAddress *dnsIP) {
  Udp.begin(LOCAL_PORT);
  dnsC.begin(*dnsIP);
};

//
//-----------------------------------------------------------------------------
// Trys once to poll the server presently pointyed to from the listin the 
// NTPServer array, and modifies the local day, month, year,
// day of week if successful.
//
// Returns:
//   0 on success
//  -1 on failure
int NTPClass::getNTPTime() {
  uint8_t buffer[30];

  strcpy_P(buffer, NTPServer[NTPSrv]);
  while (Udp.parsePacket() > 0) // Discard previously received packets
    ;

  if (sendNTPPacket(buffer) == 0) { // send an NTP packet to a time server

#ifdef VERBOSE_MODE
  Serial.print(F("Contacting "));
  Serial.print(buffer);
  Serial.print(F("...              \r\n"));
#endif

    uint32_t beginWait = millis();
    while ((millis() - beginWait) < NTP_SERVER_RESPONSE_TIME) { // Wait for a response
      if (Udp.parsePacket() >= NTP_PACKET_SIZE) {
        byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

        // We've received a packet, read the data from it
        Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

        // The timestamp starts at byte 40 of the received packet and is four bytes.
        // Combine the four bytes into a long integer. This is NTP time (seconds since Jan 1 1900):
        t.secsSince1900 = (uint32_t)packetBuffer[40];
        t.secsSince1900 = (t.secsSince1900 << 8)| (uint32_t)packetBuffer[41];
        t.secsSince1900 = (t.secsSince1900 << 8)| (uint32_t)packetBuffer[42];
        t.secsSince1900 = (t.secsSince1900 << 8)| (uint32_t)packetBuffer[43];

        t.secsSince1900 += (HOURS_OFFSET_FROM_UTC * 3600);
        getYMDHMS(true);

        return 0;
      }
    }
  }

#ifdef VERBOSE_MODE
  Serial.print(F("\nNo response from "));
  strcpy_P(buffer, NTPServer[NTPSrv]);
  Serial.print(buffer);
  Serial.print(F("         \r\n"));
#endif

  // Try a different server
  NTPSrv++;
  if (strlen_P(NTPServer[NTPSrv]) == 0)
    NTPSrv = 0;

  return -1;
} // NTPClass::getNTPTime()

//
//-----------------------------------------------------------------------------
// Send a formatted list of the NTP servers out through the serial port
//
void NTPClass::printServerList(uint8_t tabSpaces, uint8_t width) {
  char buffer[sizeof(NTPServer[0])];

  if (width < (sizeof(NTPServer[0]) + tabSpaces + 2)) // 2 spaces between entries
    width = sizeof(NTPServer[0]) + tabSpaces + 2;

  // Print first tab
  for (uint8_t i=0; i<tabSpaces; i++)
    Serial.print(F(" "));

  uint8_t widthSoFar = tabSpaces;

  for (uint8_t i = 0; i<(sizeof(NTPServer)/sizeof(NTPServer[0]))-1; i++) {

    if (widthSoFar + sizeof(NTPServer[0]) + 2 > width) {
      // New line
      Serial.print(F("\r\n"));
      // Print first tab
      for (uint8_t j=0; j<tabSpaces; j++)
        Serial.print(F(" "));
      widthSoFar = tabSpaces;
    };

    strcpy_P(buffer, NTPServer[i]);
    Serial.print(buffer);
    // Pad out end of string and add 2 spacers between columns
    for (uint8_t j = strlen_P(NTPServer[i]); j < sizeof(NTPServer[0])+2; j++)
      Serial.print(F(" "));

    widthSoFar += sizeof(NTPServer[0]) + 2;
  };
  Serial.print(F("\r\n"));
};

//
//-----------------------------------------------------------------------------
// Get year month, day, day of week from the time (seconds since 1 
// Jan 1900), and return them in the date time struct
//
// Each leap year has 366 days instead of 365. This extra leap day occurs in 
// each year that is a multiple of 4, except for years evenly divisible by 100 
// but not by 400 (which we can ignore because this boundary issue won't arise
// again until 2100). 
//
// Note that to speed up the processing, since we are looking at the current 
// date/time, we start our counting at midnight on 1/1/2024.
// 

// *** Change this to the commented code if we reuse this code for any general date
// #define IS_LEAP_YEAR (((dt->year+1900)%4 == 0) && ((dt->yr+1900)%400 != 0) && ((dt->yr+1900)%100 == 0))
#define IS_LEAP_YEAR (t.year%4 == 0)
void NTPClass::getYMD() {
  uint32_t daysLeft = (t.secsSince1900 / 86400) - 45291 + 1; // 45291 days between 1/1/1900 and 1/1/2024, and roundup for the present incomplete day
  uint8_t  daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};   //days in month

  // Check that secsSince1900 is valid - if it is too small, this method will 
  // take an extraordinate amount of time since it will underflow daysLeft to 
  // a big number.
  // We cap at about 16 years (5,840 days) on from 1/1/2024; if the date in 
  // the structure is beyond that, we set YMD to the Unix Epoch - 1/1/1970.
  // We need this test because external software can write secsSince1900 (to
  // allow external software to make use of the getYMD and getYMDHMS methods)
  if (daysLeft > 5840) { 
    // Set to the Unix Epoch; 1/1/1970
    daysLeft = 0; 
    t.wday = 4;   // Thursday
    t.mon = 0;    // January
    t.mday = 1;   // 1st
    t.year = 70;  // 1970
  } else {
    // Initialise to 1/1/2024
    t.wday = 1;   // Monday
    t.mon = 0;    // January
    t.mday = 1;   // 1st
    t.year = 124; // 2024
  };

  // Decrement by years until we get into this year
  while (daysLeft > 365) {
   
    if (IS_LEAP_YEAR) {
      daysLeft--; // Take an extra day off for leap years
      t.wday++; // Advance the day of the week by one extra
    }

    daysLeft -= 365;
    t.wday++; // In every year, the day of the week indexes by one day each year; in a leap year, it advances by a further day
    t.year++;
  }

  // We now have the correct year; get the correct month
  if (IS_LEAP_YEAR)
    daysInMonth[1]++; // February has 29 days in a leap year

  while (daysLeft >= daysInMonth[t.mon]) {
    daysLeft -= daysInMonth[t.mon];
    t.wday += daysInMonth[t.mon]%7;
   t.mon++;
  };

  // Finally, handle the day of the week
  t.mday = daysLeft + 1;
  t.wday += daysLeft%7;

  while (t.wday > 6)
    t.wday -= 7;

  /*  
    This was in the code I used as a basis to code this - I don't understand why this should be so

    if((dt->mday==0)&&(dt->mon==0)){                //glitch on 31/12 of leap years
      dt->year--;
      dt-mday = 31;
      dt->mon = 11;
    }
  */

  return;
}; // NTPClass::getYMD()

//
//-----------------------------------------------------------------------------
// Gets the year, month, date, day of week, hour, minute, second from the 
// seconds since 1900 conainted in the passed NTPTime_t structure, and write
// the resultant year, month, date, day of week, hour, minute, second into the
// same structure struct
//
// Accepts adjustIt as an optional true/false parameter to allow adjustment for 
// daylight savings if true - the default (if no second parameter is specified 
// or is set to FALSE) is to not adjust for daylight savings
// 
// This overloaded version is the public function which just performs the 
// conversion and doesn't adjust for daylight savings
void NTPClass::getYMDHMS() {
  getYMDHMS(false);
};

void NTPClass::getYMDHMS(bool adjustIt = false) {

  // Get year, month, day
  getYMD();

  if (adjustIt == true) {
    if (adjustForDST() != 0)
      getYMD(); 
  };

  t.hour = (t.secsSince1900  % 86400) / 3600; // 86400 equals secs per day
  t.min = (t.secsSince1900 % 3600) / 60;
  t.sec = (t.secsSince1900 % 60);
  return;
}; // NTPClass::getYMDHMS(uint8_t adjustIt = false)

//
//-----------------------------------------------------------------------------
// send an NTP request to the time server at the given URL.  We use the DNS 
// Client library to lookup the IP adress for the passed URL - the UDP write 
// functions require an IP Adress rather than a URL
//
int NTPClass::sendNTPPacket(char* URL) {
  IPAddress timeServer;
  byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);

  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now send a packet requesting a timestamp
  if (dnsC.getHostByName(URL, timeServer)) { 
    // getHostByName() has a hardcoded timeout time in DNS.cpp of 5000ms and 3 retries hard coded
    Udp.beginPacket(timeServer, 123); //NTP requests are to port 123
    Udp.write(packetBuffer, NTP_PACKET_SIZE);
    Udp.endPacket();
    return 0;
  } else {
#ifdef VERBOSE_MODE
  Serial.print(F("Unable to resolve "));
  Serial.print(URL);
  Serial.print(F(" to an IP address\r\n"));
#endif
  };
  return -1;
} // sendNTPPacket(char* URL)

//
//-----------------------------------------------------------------------------
// Adjusts the passed UNIX time (seconds since 1 Jan 1970) for daylight savings 
// time if required.
//
// struct dateTime_t dt should already be populated with a first pass
// 
//
// Returns 1 if adjusted, otherwise returns 0
//
int NTPClass::adjustForDST() {

  if (HOURS_OFFSET_FROM_UTC == NSW_OFFSET_FROM_UTC) {
    // DST is observed as follows in ACT, NSW, SA, TAS, VIC
    //  - at 0200 AEST on the first Sunday in October (since 2008–09) time moves forward an hour from AEST.
    //  - at 0200 AEST (0300 DST) on the first Sunday in April, time reverts to AEST
    //
    // Implementation is therefore to add an hour from 0200 AEST on first Sunday in October to
    // 0159 on the first Sunday in April

    // Include Jan - Mar, and Nov - Dec
    if ((t.mon < 3) || (t.mon > 9)) {
      t.secsSince1900 += 3600;
      return 1;
    }

    uint8_t  dayMins = (t.hour * 60) + t.min;
    uint8_t  firstSundayFlag;

    // Check April
    firstSundayFlag = t.wday == 0 ? 7 - t.mday : t.wday - t.mday; // >= 0 before and including first Sunday
    if ((t.mon == 3) && (firstSundayFlag >= 0)) {
      if ((t.wday != 0) || (dayMins < 120)) {
        t.secsSince1900 += 3600;
        return 1;
      };
    };

    // Check October
    firstSundayFlag = t.mday - t.wday; // > 0 after and including first Sunday
    if ((t.mon == 9) && (firstSundayFlag > 0)) {
      if ((t.wday != 0) || (dayMins > 119)) {
        t.secsSince1900 += 3600;
        return 1;
      }
    };
  }; // if (HOURS_OFFSET_FROM_UTC == NSW_OFFSET_FROM_UTC) {

  return 0;
}; // adjustForDST()

//
//-----------------------------------------------------------------------------
// Getter for the name of the server that we are presently polling
//
void NTPClass::getPresentServer(uint8_t *b) {
  strcpy_P(b, NTPServer[NTPSrv]);
}

//
//-----------------------------------------------------------------------------
// Display the time date structure info from any valid NTPTime_t structure on 
// the serial port
//
void NTPClass::printTimeDateInfo() {
  char buffer[4];

  strcpy_P(buffer, dayName[t.wday]);
  Serial.print(buffer);
  Serial.print(F(" "));
  Serial.print(t.mday);
  Serial.print(F(" "));
  strcpy_P(buffer, monthName[t.mon]);
  Serial.print(buffer);
  Serial.print(F(" "));
  Serial.print(t.year+1900);
  Serial.print(F(", "));
  if (t.hour < 10)
    Serial.print(F("0"));
  Serial.print(t.hour);
  Serial.print(F(":"));
  if (t.min < 10)
    Serial.print(F("0"));
  Serial.print(t.min);
  Serial.print(F(":"));
  if (t.sec < 10) 
    Serial.print(F("0"));
  Serial.print(t.sec);
  return;
};

//-----------------------------------------------------------------------------
// End of file
//-----------------------------------------------------------------------------






