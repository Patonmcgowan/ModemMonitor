//
// NTPClass.h
// 
// Data definition and function prototype file for the NTPClass class
//
//------------------------------------------------------------------------------
//  Revision History
//  ~~~~~~~~~~~~~~~~
//    2 Dec 2024 MDS Original
//
//------------------------------------------------------------------------------

#ifndef __NTPCLASS_H
#define __NTPCLASS_H

#include <Arduino.h>
#include <stdint.h>
#include <avr/pgmspace.h>
#include <SPI.h>     
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <Dns.h>

struct NTPTime_t {
    uint32_t secsSince1900; // Seconds since 1/1/1900.  This will rollover in 2036
    uint8_t hour;            // Hours, 0-23
    uint8_t min;             // Minutes, 0-59
    uint8_t sec;             // Seconds, 0-59
    uint8_t year;            // Years from 1900
    uint8_t mon;             // Months since January, 0-11
    uint8_t wday;            // Days since Sunday, 0-6
    uint8_t mday;            // Day of the month, 1-31
};

const char NTPServer[][20] PROGMEM = {
  "pool.ntp.org",   "time.google.com", "time.cloudflare.com", "time.facebook.com", "time.windows.com",   
  "time.apple.com", "ntp.time.in.ua",  "time.nist.gov",       ""
};

const char dayName[][4]   PROGMEM = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", ""
};

const char monthName[][4] PROGMEM = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", ""
};

class NTPClass {
  private:

    uint8_t NTPSrv = 0; // Indexes into the NTPServer[][] array to remember which server we are presently polling

    const unsigned int LOCAL_PORT = 8888;           // local port to listen for UDP packets

    const int NTP_PACKET_SIZE = 48;                 // NTP time stamp is in the first 48 bytes of the message

    const uint32_t NSW_OFFSET_FROM_UTC = 10;   // Sydney, Melbourne, Hobart, Canberra are UTC + 10 hours
    const uint32_t HOURS_OFFSET_FROM_UTC = NSW_OFFSET_FROM_UTC;

    const int NTP_SERVER_RESPONSE_TIME = 200;      // Maximum time to wait for NTP server response in ms

    DNSClient dnsC;

    void getYMD();
    int adjustForDST();
    int sendNTPPacket(char*);
    void getYMDHMS(bool);



  public:
    // A UDP instance to let us send and receive packets over UDP.  Ethernet connection already needs to be established
    EthernetUDP Udp;
    struct NTPTime_t t;

    NTPClass();
    void begin(IPAddress *);
    void printServerList(uint8_t, uint8_t);
    int getNTPTime();
    void getYMDHMS();
    void getPresentServer(uint8_t*);
    void printTimeDateInfo();
  
}; // class NTPClass

#endif

//-----------------------------------------------------------------------------
// End of file
//-----------------------------------------------------------------------------

