//
// ModemMonitor.ino
// 
// Periodically contacts NTP time servers to validate connection to the internet.
// If the connection drops, the modem is power cycled to force renegotiation with
// the ISP.  We wait an arbitrary amount of time within which the handshakes 
// should have occured, then we reboot our modem again ... in this way, if there
// is a network outage, we will keep trying to renegotiate.
//
// The startup time of the Arduino is annunciated every hour.
//
// Hardware: 
//   Arduino Uno
//   Ethernet shield
//   External 5V power supply (not required if always connected to the USB port)
//
// On larger networks, if this device cannot be plugged directly into the modem,
// it should be as upstream as possible (to prevent other hardware failures from
// causing a modem hard restart).
//
// NOTE:
//   We use V1.1.2 of the Ethernet library. At time of writing, V2.0.0, V2.0.1
//   and V2.0.2 were available, but the function names had changed, preventing 
//   successful compilation.
//
//   *** If feeding an external voltage into the VIN terminal on the Uno or ***
//   *** the ethernet shield, regulate it to 5V to prevent excessive        *** 
//   *** heating of the W5500 ethernet chip                                 ***
//
//------------------------------------------------------------------------------
//  Revision History
//  ~~~~~~~~~~~~~~~~
//    12 Oct 2024 MDS Original
//    10 Dec 2024 MDS Working version
//
//------------------------------------------------------------------------------
#include <SPI.h>     
#include <Ethernet.h>
#include "ModemMonitor.h"
#include "EEPROMRecordClass.h"
#include "NTPClass.h"

const uint32_t BAUD_RATE = 115200;           // Serial port baud rate

const uint16_t NTP_SERVER_POLL_TIME = 40000; // Normal polling interval in ms
const int8_t POLL_NO_RESPONSE = -1;
const int8_t POLL_SUCCESS = 0;

const uint8_t MODEM_ARBITRATION_TIME = 15;   // Time in minutes in which the modem would be guaranteed to
                                             // successfully arbitrate with a functional external network

// Pin assignments
// Notes 
//   - Can't use PB5, (used by the inbuilt LED on the Uno board) because this is used for the SCK for the ethernet shield
//   - Can't use PD0, because this is used as RXD for serial comms 
//   - Can't use PD1, because this is used as TXD for serial comms 
//   - The pin number refers to the number silk screened on the Arduino board and the Ethernet shield in the DIGITAL header section
//   - On the Arduino Uno SMD schematic, the pin number is preceeded by 'IO', so IO9 would be pin number 9 in the software, or PB1(OC1A)
// 
const uint8_t  statusLEDPin = 2; // PD2 - The external status LED 
                                 //     - Arduino Uno SMD schematic IO2
                                 //     - Arduino Uno DIGITAL header terminal '2' (third terminal)
                                 //     - Ethernet shield schematic JLOW connector pin 2
                                 //     - Ethernet shield DIGITAL header terminal '2' (third terminal)
const uint8_t  relayPin = 3;     // PD3 - The N/C relay switching modem power
                                 //     - Arduino Uno SMD schematic IO3
                                 //     - Arduino Uno DIGITAL header terminal '3' (fourth terminal)
                                 //     - Ethernet shield schematic JLOW connector pin 3
                                 //     - Ethernet shield DIGITAL header terminal 3 (fourth terminal)

uint8_t  retryNo = 0;            // Number of times that we have tried to ping and failed
const uint8_t   MAX_RETRIES = 3; // Number of failed polls before resetting modem under normal conditions. 
                                 // We keep this low because the DNS Client library has three retries hard
                                 // coded every time we try to resolve the NTP server URL into an IP address


// Flash on times for the status LED in ms.  All flashing has a 50% duty cycle
const unsigned int SLOW_FLASH   = 1500; // Waiting for modem arbitration period
const unsigned int MEDIUM_FLASH = 550;  // Normal operation
const unsigned int FAST_FLASH   = 80;   // Retries exceeded, time to reboot the modem

// Timing variables
uint32_t currentMillis;
uint32_t previousRelayMillis;            // Timing variable for powering down the relay
uint16_t pollDelayMillis =  1;           // Remembers the delay between NTP server polls.  A value of 1 signals the first pass through loop()

// State machine for the modem
const uint8_t S_ARDUINO_POWERUP          = 0; // We have just powered up the Arduino and are looking for the first modem response
const uint8_t S_LOOKING_FOR_MODEM_ONLINE = 1; // We are looking for a connection to the ISP after modem restart (modem is arbitrating)
const uint8_t S_MODEM_IS_ONLINE          = 2; // We have successfully pinged, and 
const uint8_t S_MODEM_RESTART            = 3; // Modem has been online, but MAX_RETRIES have been exceeded so we are powering down the modem
uint8_t state = S_ARDUINO_POWERUP;

// States for the outputs (which can be manually overridden)
const uint8_t OUTPUT_ON      = 0;
const uint8_t OUTPUT_OFF     = 1;
const uint8_t OUTPUT_DEFAULT = 2;

const uint16_t MODEM_POWER_OFF_TIME = 3000; // Time which we hold the modem power off to do a hard reset in ms

char buffer[200];

struct modemRecord_t modem;        // Working record for modem uptime data
EEPROMRecordClass m;               // Class which contains all of the stuff to work on the modem outage records in EEPROM
NTPClass NTP;                      // This does all of the NTP stuff

uint8_t verboseMode = false;           
uint8_t statusLEDMode = OUTPUT_DEFAULT;
uint8_t relayMode = OUTPUT_DEFAULT;
uint8_t simulateNoResponse = false;    // Allows simulation of timeout when set to true
bool clearEEPROMFlag = false;

//
//-----------------------------------------------------------------------------
// Update the status LED
// 
// This TIMER1 Compare Interrupt A occurs every 40ms:
//   Clock runs at 16MHz, prescaler is set to 1024 which causes a tick every 64us
//   Advance the compare register 625 ticks, which equates to every 40ms
//
ISR(TIMER1_COMPA_vect) {
  static uint32_t previousStatusLEDMillis;        // Timing variable for the external status LED

  OCR1A += 625; // Advance The COMPA Register

  int t = MEDIUM_FLASH;

  currentMillis = millis();

  switch (statusLEDMode) {
    case OUTPUT_ON:
      digitalWrite(statusLEDPin, HIGH);
      break;
    case OUTPUT_OFF:
      digitalWrite(statusLEDPin, LOW);
      break;
    default: // default case is OUTPUT_DEFAULT

      // LED status:
      //   LED always runs at 50% duty cycle - the following define the time (in ms) that the LED is illuminated
      //
      //   Waiting for arbitration : SLOW_FLASH
      //   Normal ping : MEDIUM_FLASH (default case upon startup)
      //   As retryNo expire : Increasing flash rate from MEDIUM_FLASH to FAST_FLASH
      //   Modem power out : FAST_FLASH
      if (state == S_MODEM_RESTART) {
        t = FAST_FLASH;
      } else if ((state == S_LOOKING_FOR_MODEM_ONLINE) || (state == S_ARDUINO_POWERUP)) {
        t = SLOW_FLASH;
      } else if (retryNo > 0) {
        t = FAST_FLASH + ((MAX_RETRIES - retryNo) * (MEDIUM_FLASH - FAST_FLASH))/MAX_RETRIES;
      } else {
        t = MEDIUM_FLASH; // Normal operation
      };

      if ((currentMillis - previousStatusLEDMillis) <= t) {
        digitalWrite(statusLEDPin, HIGH);  // First part of LED cycle is turning the LED on
      } else {
        digitalWrite(statusLEDPin, LOW);   // Second part of LED cycle is turing the LED off
      }
      break;
  };
  // If we have done LED on and LED off, then reset the timing
  if ((currentMillis - previousStatusLEDMillis) >= (t * 2))
    previousStatusLEDMillis = currentMillis;

  // Handle rollover - this is not correct but near enough for what we are doing
  if (currentMillis < previousStatusLEDMillis)
    previousStatusLEDMillis = 0;

  return;
}

//
//-----------------------------------------------------------------------------
// Update the seconds counters
// 
// This TIMER1 Compare Interrupt B occurs every 1,000ms:
//   Clock runs at 16MHz, prescaler is set to 1024 which causes a tick every 64us
//   Advance the compare register 15,625 ticks, which equates to every 1000ms
//
uint8_t secs = 0; 
uint8_t mins = 0;

ISR(TIMER1_COMPB_vect) {
  OCR1B += 15625; // Advance The COMPB Register

  // --------------------------------------------------------------------------
  // Update the minutes since restart, and other stats as required on 
  // seconds/minutes rollover
  secs++;
  // if (state == S_LOOKING_FOR_MODEM_ONLINE) // Add this in and test once rest of code is working properly
  modem.waitSecs++;

  if (secs > 60) { // Update stats every minute
    secs = secs - 60;

    mins++;
    if (mins > 240)
      mins = mins - 240;

    // Update the duration of the present outage
    if ((retryNo > 0) || (state == S_LOOKING_FOR_MODEM_ONLINE) || (state == S_ARDUINO_POWERUP))
      modem.downMins++;

    // Record restart information to EEPROM every 15 minutes
    if (mins%15 == 0) {
      m.convertToEEPROMBlock(&modem);
      m.setEEPROMUptimeStats();
    };
  }
  return;
}

//
//-----------------------------------------------------------------------------
// setup()
//
void setup() {
  int i;

  static const uint8_t mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED}; // MAC address for ethernet shield

  IPAddress myIP(192,0,0,10);                        // IP address for my ethernet shield - Change this to suit your network
  IPAddress gatewayIP(192, 0,  0, 1);                // The network's internal gateway address - Change this to siut your network
  IPAddress dnsIP(220, 233,  0, 3);                  // The DNS address that the network uses (not used in this code except in initialisation)
  IPAddress subnetMask(255, 255, 255, 0);            // The ethernet shield's subnet mask

  Ethernet.begin(mac, myIP, dnsIP, gatewayIP, subnetMask);
  NTP.begin(&dnsIP);

  Serial.begin(BAUD_RATE);

  pinMode(statusLEDPin, OUTPUT);
  pinMode(relayPin, OUTPUT);

  Serial.print(F(
    "\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n"
    "\r\n\r\n\r\n\r\n\r\n\r\n\r\n\r\n"
    "Mike's Little Modem Monitor\r\n"
    "~~~~~~~~~~~~~~~~~~~~~~~~~~~\r\n"
    "Michael Scott 2024\r\n\r\n"));

  Serial.println(F("NOTE  Use V1.1.2 of the Ethernet library.  DO NOT upgrade - versions 2.0.0 to 2.0.2 won't compile."));

  Serial.println(__FILE__);
  Serial.print(F("Last compiled and downloaded on "));
  Serial.print(__DATE__);
  Serial.print(F(" at "));
  Serial.println(__TIME__);

  Serial.print(F("\r\nEthernet hardware ready:                                               Serial Commands:\r\n"));

  Serial.print(F("  My IP Address is "));
  Serial.print(Ethernet.localIP());
  Serial.print(F(                   "                                         C - Clear outage history (initialise EEPROM)\r\n"));
  Serial.print(F("  Gateway IP Address is "));
  Serial.print(Ethernet.gatewayIP());
  Serial.print(F(                        "                                    D - Dump EEPROM contents to serial port\r\n"));
  Serial.print(F("  DNS Server IP Address is "));
  Serial.print(Ethernet.dnsServerIP());
  Serial.print(F(                         "                                   F - Simulate internet failure (ENABLE/DISABLE)\r\n"));
  Serial.print(F("  Subnet mask is "));
  Serial.print(Ethernet.subnetMask());
  Serial.print(F(                 "                                           H - Show command options (help)\r\n"
    "                                                                         L - Toggle external status LED (ON/OFF/Default)\r\n"
    "Connected to serial port at "));
  sprintf(buffer,
    "%6lu", BAUD_RATE);
  Serial.print(buffer);
  Serial.print(F(                     " baud                                  R - Toggle output relay (ON/OFF/Default)\r\n"));
  Serial.print(F(
    "                                                                         S - Show outage history\r\n"));
  Serial.print(F(
    "                                                                         V - Toggle verbose mode (ON/OFF)\r\n"));

  Serial.print(F(
    "\r\nI'm gonna contact the following NTP Servers to check that I have internet connectivity:\r\n"));
  NTP.printServerList(2, 120);
  Serial.println();

  m.getEEPROMUptimeStats();
  m.convertFromEEPROMBlock(&modem);

  digitalWrite(relayPin, LOW);

 // currentMillis = millis();
  modem.waitSecs = 0;

  // TIMER0 is used for millis() so we can't use it here ... we use TIMER1
  // The flashing of the LED is fastest at 80ms on time, so we therefore set TIMER1 
  // prescaler to be 1024 to allow the compare interrupt to fire every 40ms 
  TCCR1A = 0;           // Init Timer1
  TCCR1B = B00000101;   // Prescalar = 1024. At a clock speed of 16MHz, this gives a tick rate of 64us per tick
  OCR1A = 625;          // Timer 1 CompareA Register - this gives a compare interrupt every 40ms (625 x 64us)
  OCR1B = 15625;        // Timer 1 CompareB Register - this gives a compare interrupt every 1,000ms (15,625 x 64us)
  TIMSK1 |= B00000110;  // Enable Timer COMPA Interrupt and Timer COMPB Interrupt
  return;
}  // setup()

//
//-----------------------------------------------------------------------------
// loop()
//
void loop() {
  static uint8_t powerUpFlag = true;            // Used to remember if we have we had a modem dropout since power up of the Arduino
  static int8_t pollResult;

  currentMillis = millis();

  handleSerialInput();

  // --------------------------------------------------------------------------
  // Do the poll if required
  if ((currentMillis % pollDelayMillis == 0) && (state != S_MODEM_RESTART)) {
    // pollDelayMillis == 1 signals the first time through the loop function after restart
    if (pollDelayMillis == 1) {
      pollDelayMillis = NTP_SERVER_POLL_TIME;
      Serial.print(F("Polling "));
      NTP.getPresentServer(buffer);
      Serial.print(buffer);
    };

    if (simulateNoResponse != true) {
      NTP.getPresentServer(buffer);  // Remember which server we are polling for the diagnostics after the poll
      pollResult = NTP.getNTPTime();
    } else {
      strcpy_P(buffer, PSTR("simulated server"));
      delay(3000); // Simulate waiting for response
      pollResult = POLL_NO_RESPONSE;
    }

    if (pollResult == POLL_SUCCESS) {
      pollDelayMillis = NTP_SERVER_POLL_TIME;
      modem.secsSince1900 = NTP.t.secsSince1900;
    };

    clearLine();
    if (pollResult == POLL_SUCCESS) {
      NTP.printTimeDateInfo();
      Serial.print(F(" "));
      if ((state == S_LOOKING_FOR_MODEM_ONLINE) || (state == S_ARDUINO_POWERUP)) {
        Serial.print(F("Connection with the ISP node device has been validated\r\n"));

        if (state != S_ARDUINO_POWERUP) {
          m.convertToEEPROMBlock(&modem);
          m.completeLogEntry();
        };
      } else {
        Serial.print(F("Poll success"));
      };

      state = S_MODEM_IS_ONLINE;
      pollDelayMillis = NTP_SERVER_POLL_TIME;
      modem.downMins = 0;
      retryNo = 0;
    } else {
      Serial.print(F("No response from "));
      Serial.print(buffer);

      // Only increment the retry counter once the modem reconnects to the ISP after a power restart
      // Also allow retryNo after the autonegotiation should have finished (in case the network goes 
      // down for some time before becoming available - this will reforce power reboot)
      if ((state == S_MODEM_IS_ONLINE) || (modem.waitSecs/60 >= MODEM_ARBITRATION_TIME)) {
        retryNo++;
        pollDelayMillis = 2; // Retry time is hard coded in Dns.h which resolves the URL to an IP address, so make our one tiny
      }

      if ((state == S_LOOKING_FOR_MODEM_ONLINE) && (modem.waitSecs/60 < MODEM_ARBITRATION_TIME))
        pollDelayMillis = NTP_SERVER_POLL_TIME;

      if (retryNo > MAX_RETRIES) {
        state = S_MODEM_RESTART;
        previousRelayMillis = currentMillis; // Reset the modem power OFF timer
      } else {
        clearLine();
        if (simulateNoResponse != true) {
          Serial.print(F("Polling "));
          NTP.getPresentServer(buffer);
        } else
          strcpy_P(buffer, PSTR("Simulation of server polling will happen"));
        Serial.print(buffer);

        if (pollDelayMillis > 10) {
          // This is not a retry, since retry poll time == 2
          Serial.print(F(" at "));
          Serial.print(((float)pollDelayMillis/1000), 0);
          Serial.print(F(" second intervals"));
        }
        if ((state == S_MODEM_IS_ONLINE) || (modem.waitSecs/60 >= MODEM_ARBITRATION_TIME)) {
          Serial.print(F(" (retry "));
          Serial.print(retryNo);
          Serial.print(F(" of "));
          Serial.print(MAX_RETRIES);
          Serial.print(F(")\r\n"));
        } else {
          Serial.print(F(" ("));
          Serial.print(((float)modem.waitSecs*100/(60*MODEM_ARBITRATION_TIME)), 0);
          Serial.print(F("% of arbitration period has passed)"));
        }
      }
    }
  }; // if ((currentMillis % pollDelayMillis == 0) && (state != S_MODEM_RESTART))

  // --------------------------------------------------------------------------
  // Hold power off the modem for a time if maximum retryNo have been exceeded
  if (state == S_MODEM_RESTART) {

    if ((currentMillis - previousRelayMillis) <= MODEM_POWER_OFF_TIME) {
      if (retryNo > 0) { // This forces a one shot of the below code block since retryNo is reset to zero inside
        sprintf(buffer,"\r\n%d", MAX_RETRIES);
        Serial.print(buffer);
        Serial.print(F(
          " retries failed\r\n"
          "\r\n"
          "    *************************************\r\n"
          "    *************************************\r\n"
          "    *****                           *****\r\n"
          "    *****    Power cycling modem    *****\r\n"));
        retryNo = 0;
        if (relayMode == OUTPUT_OFF)
          Serial.print(F("Unable to switch relay - it has been forced off\r\n"));
        powerUpFlag = false;
      };
      // Reset modem by removing power (ie energising the relay to open the N/C contacts)
      switch (relayMode) {
        case OUTPUT_OFF:
          break;
        case OUTPUT_ON:
        default:
          digitalWrite(relayPin, HIGH);
          break;
      };
    } else {
      switch (relayMode) {
        case OUTPUT_ON:
          break;
        case OUTPUT_OFF:
          break;
        default:
          switchRelayOff();
          state = S_LOOKING_FOR_MODEM_ONLINE;
          modem.waitSecs = 0;
          break;
      };
    };
  } else {
    previousRelayMillis = currentMillis;
  };

  // Handle rollover - this is not correct but near enough for what we are doing
  if (currentMillis < previousRelayMillis)
    previousRelayMillis = 0;

  return;
}  // loop()

//
// --------------------------------------------------------------------------
// Service any serial input
void handleSerialInput() {

  while (Serial.available() > 0) {
    uint8_t ch = toUpperCase(Serial.read());

    if ((clearEEPROMFlag == true) && (ch != 'Y')) {
      // User responded with something other than 'Y' to the clear EEPROM confirmation
      Serial.print(F(
        "\r\n"
        "\r\n"
        "Aborted\r\n"));
      clearEEPROMFlag = false;
    } else {
      switch (ch) {
        // Clear uptime history - writes the EEPROM with 255's
        case 'C':
          Serial.print(F(
            "\r\n"
            "\r\n"
            "ALL OUTAGE DATA WILL BE DELETED. DO YOU WANT TO CONTINUE ? "));
          clearEEPROMFlag = true;
          break;

        // Dump EEPROM content to serial port
        case 'D':
          m.dumpEEPROM();
          Serial.print(F(
            "\r\n"
            "\r\n"
            "Software has been running since ")) ;
          Serial.print(__DATE__);
          Serial.print(F(" at "));
          Serial.println(__TIME__);
          break;

        // Toggle the simulation of timeout of the remote IP address
        case 'F':
          Serial.print(F(
            "\r\n"
            "Simulation of internet failure "));
          if (simulateNoResponse == true) {
            simulateNoResponse = false;
            Serial.print(F("disabled"));
          } else {
            simulateNoResponse = true;
            Serial.print(F("enabled"));
          };
          Serial.print(F("\r\n"));
          break;

        case 'H':
          Serial.print(F(
            "\r\n"
            "\r\n"
            "  Help Menu\r\n"
            "  ~~~~~~~~~\r\n"
            "  C - Clear outage history (initialise EEPROM)\r\n"
            "  D - Dump EEPROM contents to serial port\r\n"
            "  F - Simulate internet failure (ENABLE/DISABLE)\r\n"
            "  H - Display this menu\r\n"
            "  L - Toggle external status LED (ON/OFF/Default)\r\n"
            "  R - Toggle output relay (ON/OFF/Default)\r\n"
            "  S - Show outage history\r\n"
            "  V - Toggle verbose mode (ON/OFF)\r\n"
            "\r\n"));
          break;

        // Toggle the state of the external status LED
        case 'L':
          Serial.print(F("\r\n"));
          switch (statusLEDMode) {
            case OUTPUT_ON:
              statusLEDMode = OUTPUT_OFF;
              if (verboseMode == true)
                Serial.print(F("Status LED turned off\r\n"));
              break;
            case OUTPUT_OFF:
              statusLEDMode = OUTPUT_DEFAULT;
              if (verboseMode == true)
                Serial.print(F("Status LED reset to default\r\n"));
              break;
            default: // default case is OUTPUT_DEFAULT
              statusLEDMode = OUTPUT_ON;
              if (verboseMode == true)
                Serial.print(F("Status LED turned on\r\n"));
              break;
          };
          break;

        // Toggle the state of the onboard LED
        case 'R':
          Serial.print(F("\r\n"));
          switch (relayMode) {
            case OUTPUT_ON:
              relayMode = OUTPUT_OFF;
              if (verboseMode == true)
                Serial.print(F("Output relay turned off (modem energised)\r\n"));
              Serial.print(F(
                "    *************************************\r\n"
                "    *************************************\r\n"));
              switchRelayOff();
              break;
            case OUTPUT_OFF:
              relayMode = OUTPUT_DEFAULT;
              if (verboseMode == true)
                Serial.print(F("Output relay reset to default\r\n"));
              break;
            default: // default case is OUTPUT_DEFAULT
              relayMode = OUTPUT_ON;
              if (verboseMode == true)
                Serial.print(F("Output relay turned on (modem de-energised)\r\n"));
              break;
          };
          break;

        // Show uptime/outage history - send info through Serial port in a formatted fashion
        case 'S':
          Serial.print(F(
            "\r\n"
            "\r\n"
            "                        --- MODEM OUTAGE HISTORY ---\r\n"
            "\r\n"));

          if (m.getOldestCompletedRecord() != -1) {
            Serial.print(F("  On:\r\n"));
            dumpOutageRecord();
            while (m.getNextCompletedRecord() != -1)
              dumpOutageRecord();
          } else {
            Serial.print(F("  No outages to report\r\n"));
          };

          Serial.print(F(
            "\r\n"
            "                           --- End Of History ---\r\n"
            "\r\n"
            "Software has been running since "));
          Serial.print(__DATE__);
          Serial.print(F(" at "));
          Serial.println(__TIME__);
          break;

        // Toggle verbose mode
        case 'V':
          Serial.print(F(
            "\r\n"
            "Verbose mode turned "));
          if (verboseMode == true) {
            verboseMode = false;
            Serial.print(F("off"));
          } else {
            verboseMode = true;
            Serial.print(F("on"));
          };
          Serial.print(F("\r\n"));
          break;
        case 'Y':
          if (clearEEPROMFlag == true) {
            modem.downMins = 0;
            m.convertToEEPROMBlock(&modem);
            m.clearLog();
            Serial.print(F(
              "\r\n" 
              "Outage log has been cleared\r\n"));
            clearEEPROMFlag = false;
          };
          break;
        default:
          break;
      }; // switch (ch)
    };
  };
  return;
};

//
//-----------------------------------------------------------------------------
// Clear existing line to end and return cursor to start of line
void clearLine() {
  uint8_t i;

  Serial.print(F("\r"));
  for (i=0; i<97; i++)
    Serial.print(F(" "));
  Serial.print(F("\r"));
  return;
};

//
//-----------------------------------------------------------------------------
// switchRelayOff()
// 
// Modem power is wired through N/C contacts, so switching the relay off 
// reapplies power to the modem
//
void switchRelayOff() {
  digitalWrite(relayPin, LOW);
  if (state == S_MODEM_RESTART) {
    // After first time through upon restart, the state will change from 
    // S_MODEM_RESTART to S_LOOKING_FOR_MODEM_ONLINE
    pollDelayMillis = NTP_SERVER_POLL_TIME; // Go to long poll because we will be waiting for modem arbitration
    Serial.print(F(
      "    *****                           *****\r\n"
      "    ***** Power re-applied to modem *****\r\n"
      "    *****                           *****\r\n"
      "    *************************************\r\n"
      "    *************************************\r\n"
      "\r\n"));
    Serial.print(F("\rSoftware has been running since ")) ;
    Serial.print(__DATE__);
    Serial.print(F(" at "));
    Serial.println(__TIME__);
    Serial.print(F("Polling will resume in "));
    Serial.print(((float)pollDelayMillis/1000), 0);
    Serial.print(F(" seconds"));
  }
  return;
};

//
//-----------------------------------------------------------------------------
// Send record presently indexed out through serial port
// Serial port must have already been initialised
//
void dumpOutageRecord() {
  struct modemRecord_t mRec;
  NTPClass n;

  m.getDataFromIndex();
  m.convertFromEEPROMBlock(&mRec);

  Serial.print(F("    "));

  // Use the methods in the NTPClass to convert secsSince1900 into meaningful text and print it out
  n.t.secsSince1900 = mRec.secsSince1900;
  n.getYMDHMS();
  n.printTimeDateInfo();

  *buffer = '\0';
  Serial.print(F(", modem went offline"));

  sprintf(buffer, "%s%S%d%S", buffer, F(" for "), mRec.downMins, F(" minute"));  // %S format specifier is Arduino AVR-GCC specific
  if (mRec.downMins != 1)
    sprintf(buffer, "%s%S", buffer, F("s"));                    // %S format specifier is Arduino AVR-GCC specific

  sprintf(buffer, "%s%S", buffer, F("\r\n"));                       // %S format specifier is Arduino AVR-GCC specific
  Serial.print(buffer);

  return;
};

//-----------------------------------------------------------------------------
// End of file
//-----------------------------------------------------------------------------
