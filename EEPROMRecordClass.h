//
// EEPROMRecordClass.h
// 
// Data definition and function prototype file for EEPROMRecordClass.cpp, which
// records modem uptime information to the Arduino onboard EEPROM
//
// Data Formats: Each record comprises 8 bytes
//   Completed record : 00 00 00 0F where the first 7 bytes are as per the 
//                      EEPROMModemRecord struct
//   Presently built record : 
//------------------------------------------------------------------------------
//  Revision History
//  ~~~~~~~~~~~~~~~~
//    29 Oct 2024 MDS Original
//
//------------------------------------------------------------------------------
#ifndef __MODEM_RECORD_CLASS_H
#define __MODEM_RECORD_CLASS_H

#include <Arduino.h>
#include <EEPROM.h>
#include "ModemMonitor.h"

// Outages are remembered in a group of 8 bytes in EEPROM as a circular list

// For flags uint8_t in the EEPROM record
#define MODEM_RECORD_COMPLETE      0x01
#define MODEM_RECORD_IN_PROGRESS   0x02
#define MODEM_RECORD_UNUSED        0xFF

class EEPROMRecordClass {
  private:
    unsigned int _modemRecordIndex; // Index to last record in EEPROM circular list

    // The latest entry contains the total uptime to present. Mapping is as follows:
    struct EEPROMRecord_t {

      uint8_t secsSince1900_4; // MSB
      uint8_t secsSince1900_3; 
      uint8_t secsSince1900_2;
      uint8_t secsSince1900_1; // LSB

      // Minutes that the modem was down
      uint8_t downMins2; // MSB
      uint8_t downMins1; // LSB

      uint8_t spare;

      // Various flags:
      //    0x01 = completed record
      //    0x02 = record being built (partial record)
      //    0xff = default (unused spot)
      uint8_t flags;
    } EEPROMBlock;

  public:
    EEPROMRecordClass();
    int convertToEEPROMBlock(struct modemRecord_t *);
    int convertFromEEPROMBlock(struct modemRecord_t *);
    int getOldestCompletedRecord();
    int getNextCompletedRecord();
    int getRecordInProgress();
    int getNewestCompletedRecord();
    int getIndexOfNextCompletedRecord();
    int getIndexOfPrevCompletedRecord();
    int getDataFromIndex(int);
    int getDataFromIndex();
    int completeLogEntry();
    int getEEPROMUptimeStats();
    int setEEPROMUptimeStats();
    int clearLog();
    void dumpEEPROM();
}; // class EEPROMRecordClass

#endif

//-----------------------------------------------------------------------------
// End of file
//-----------------------------------------------------------------------------
