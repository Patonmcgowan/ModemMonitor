//
// EEPROMRecordClass.cpp
// 
// Contains the methods for the EEPROMRecordClass, which reads and writes
// modem uptime records to/from the EEPROM.
// 
// Note that the data access functions are overloaded, meaning that a pointer 
// to a result structure can be passed in the function calls, or the functions 
// can be called and the structure members can be accessed indivisually from 
// there.
//------------------------------------------------------------------------------
//  Revision History
//  ~~~~~~~~~~~~~~~~
//    29 Oct 2024 MDS Original
//
//------------------------------------------------------------------------------
#include "EEPROMRecordClass.h"

//
//-----------------------------------------------------------------------------
// Constructor
EEPROMRecordClass::EEPROMRecordClass() {

  // Look for the latest record and point to it
  getNewestCompletedRecord();
  return;
};

//
//-----------------------------------------------------------------------------
// Return a dataset based upon the passed index
int EEPROMRecordClass::getDataFromIndex(int ind) {

  EEPROM.get(ind, EEPROMBlock);
  return 0;
}

//
//-----------------------------------------------------------------------------
// Overloaded version : return the record pointed to by _modemRecordIndex
int EEPROMRecordClass::getDataFromIndex() {
  getDataFromIndex(_modemRecordIndex);
  return 0;
}

//
//-----------------------------------------------------------------------------
// Get the index of the least recent modem record on the EEPROM (if it exists).
// If no records exist then -1 is returned.
//
int EEPROMRecordClass::getOldestCompletedRecord() {
  int i, emptyEEPROM = true, numRecords, count;
  uint8_t flags;

  numRecords = EEPROM.length()/sizeof(EEPROMRecord_t);

  // The oldest record is the first one found working forward through the EEPROM 
  // past the record presently being built
  i = 0;  // Look from start of EEPROM for the record being built
  EEPROM.get(i+7, flags);

  while ((flags != MODEM_RECORD_IN_PROGRESS) && 
      (count <= numRecords)) {                // Have we looked at all records ?

    _modemRecordIndex = i;
    i += sizeof(EEPROMRecord_t);
    if (i+7 >= EEPROM.length())
      return -1;

    EEPROM.get(i+7, flags);
  };

  // Couldn't find record being built
  if (count > numRecords)
    return -1;


  // We found a valid record being built, so oldest complete record is the first 
  // record encountered past that
  count = 1; // Start at 1 checked because we know one record is the 'IN_PROGRESS' record
  while ((count <= numRecords) && (flags != MODEM_RECORD_COMPLETE)) {
    i += sizeof(EEPROMRecord_t);
    if (i >= EEPROM.length())
      i = 0;
    EEPROM.get(i+7, flags);
    count++;
  };
  if (count <= numRecords) {
    _modemRecordIndex = i;
    return _modemRecordIndex;
  }

  return -1;
}; // getOldestCompletedRecord()

//
//-----------------------------------------------------------------------------
// Get the index of the next oldest modem record to the current record on the 
// EEPROM (if it exists).  If no further records exist (ie end of records) then 
// -1 is returned.
//
int EEPROMRecordClass::getNextCompletedRecord() {
  int i;
  uint8_t flags;

  i = getIndexOfNextCompletedRecord();
  if (i<0)
    return -1;

  EEPROM.get(i+7, flags);

  if (flags == MODEM_RECORD_COMPLETE) {
    _modemRecordIndex = i;
    return _modemRecordIndex;
  };

  return -1;
};

//
//-----------------------------------------------------------------------------
// Searches the EEPROM to find the index of the modem record being built (if it
// exists).  If no records exist then -1 is returned.
//
int EEPROMRecordClass::getRecordInProgress() {
  int i, count, numRecords;
  uint8_t flags;

  count = 0;
  numRecords = EEPROM.length()/sizeof(EEPROMRecord_t);

  i = 0;  // Look from start of EEPROM for valid entries
  EEPROM.get(i+7, flags);
  while ((flags != MODEM_RECORD_IN_PROGRESS) &&  
      (count <= numRecords)) {                // Have we looked at all records ?
    _modemRecordIndex = i;

    i = _modemRecordIndex + sizeof(EEPROMRecord_t);
    if (i >= EEPROM.length())
      i -= EEPROM.length();

    EEPROM.get(i+7, flags);
    count++;
  };

  if (count > numRecords)
    return -1;

  _modemRecordIndex = i;
  return _modemRecordIndex;
};

//
//-----------------------------------------------------------------------------
// Get the index of the newest completed modem record on the EEPROM (if it
// exists).  If no records exist then -1 is returned.
//
int EEPROMRecordClass::getNewestCompletedRecord() {
  int i, count = 0, numRecords;
  uint8_t flags;

  numRecords = EEPROM.length()/sizeof(EEPROMRecord_t);

  i = 0;  // Look from start of EEPROM for valid entries
  EEPROM.get(i+7, flags);
  while ((flags != MODEM_RECORD_COMPLETE) &&  // Blank record
      (count <= numRecords)) {                // Have we looked at all records ?
    _modemRecordIndex = i;
    i = getIndexOfNextCompletedRecord();
    if (i<0)
      return _modemRecordIndex;
    EEPROM.get(i+7, flags);
    count++;
  };

  if (count > numRecords) {
    return -1;
  } else {
    // We found a valid record, so now just keep looking through the valid records to the 
    // end of them (i is pointing to a valid record because it fell through from above)
    count = 0;
    while ((count <= numRecords) && (flags == MODEM_RECORD_COMPLETE)) {
      _modemRecordIndex = i;
      i = getIndexOfNextCompletedRecord();
      if (i<0)
        return _modemRecordIndex;
      EEPROM.get(i+1, flags);
      count++;
    };
  }

  return _modemRecordIndex;
};

//
//-----------------------------------------------------------------------------
// Increments _modemRecordIndex to point to the next record, and returns
// _modemRecordIndex if it is a completed record, otherwise returns -1
//
int EEPROMRecordClass::getIndexOfNextCompletedRecord() {
  int i;
  uint8_t flags;

  i = _modemRecordIndex + sizeof(EEPROMRecord_t);
  if (i >= EEPROM.length())
    i = 0;

  EEPROM.get(i+7, flags);

  if (flags == MODEM_RECORD_COMPLETE)
    _modemRecordIndex = i;
  else
    return(-1);

  return _modemRecordIndex;
};

//
//-----------------------------------------------------------------------------
// Return the index of the next newest modem record.
// Return -1 if there is none
//
int EEPROMRecordClass::getIndexOfPrevCompletedRecord() {
  int i;
  uint8_t flags;

  i = _modemRecordIndex - sizeof(EEPROMRecord_t);
  if (i < 0)
    i += EEPROM.length();

  EEPROM.get(i+7, flags);

  if (flags == MODEM_RECORD_COMPLETE)
    _modemRecordIndex = i;
  else
    return -1;

  return _modemRecordIndex;
};

//
//-----------------------------------------------------------------------------
// completeLogEntry()
//   Finalises the record in progress with the passed data on the end of the 
//   EEPROM circular list and starts a new record after that (ie finalises the 
//   temporary record if it exists, otherwise creates a new one).
//   Data is stored big endian (ie MSB first)
//
int EEPROMRecordClass::completeLogEntry() {
  getRecordInProgress(); // Sets up _modemRecordIndex to point to the entry being built

  if (_modemRecordIndex < 0)  // None found
    _modemRecordIndex = 0; // Create a new one at the beginning of the EEPROM

  EEPROM.update(_modemRecordIndex, EEPROMBlock.secsSince1900_4);
  EEPROM.update(_modemRecordIndex+1, EEPROMBlock.secsSince1900_3);
  EEPROM.update(_modemRecordIndex+2, EEPROMBlock.secsSince1900_2);
  EEPROM.update(_modemRecordIndex+3, EEPROMBlock.secsSince1900_1);

  EEPROM.update(_modemRecordIndex+4, EEPROMBlock.downMins2);
  EEPROM.update(_modemRecordIndex+5, EEPROMBlock.downMins1);

  EEPROM.update(_modemRecordIndex+7, MODEM_RECORD_COMPLETE);

  // Point to next record
  _modemRecordIndex += sizeof(EEPROMRecord_t);
  if (_modemRecordIndex >= EEPROM.length())
    _modemRecordIndex -= EEPROM.length();

  // Initalise the new record
  EEPROM.update(_modemRecordIndex, EEPROMBlock.secsSince1900_4);
  EEPROM.update(_modemRecordIndex+1, EEPROMBlock.secsSince1900_3);
  EEPROM.update(_modemRecordIndex+2, EEPROMBlock.secsSince1900_2);
  EEPROM.update(_modemRecordIndex+3, EEPROMBlock.secsSince1900_1);

  EEPROM.update(_modemRecordIndex+4, 0);
  EEPROM.update(_modemRecordIndex+5, 0);

  EEPROM.update(_modemRecordIndex+7, MODEM_RECORD_IN_PROGRESS);

  return;
}; // completeLogEntry()

//
//-----------------------------------------------------------------------------
// Clear log by writing the EEPROM with 0xff values. _modemRecordIndex is left
// unchanged and will contain the first record of the new list (to equalise wear
// on all areas of the EEPROM)
//
int EEPROMRecordClass::clearLog() {

  for (int i = 0; i<EEPROM.length(); i++)
    EEPROM.update(i, MODEM_RECORD_UNUSED);

  EEPROM.update(_modemRecordIndex, EEPROMBlock.secsSince1900_4);
  EEPROM.update(_modemRecordIndex+1, EEPROMBlock.secsSince1900_3);
  EEPROM.update(_modemRecordIndex+2, EEPROMBlock.secsSince1900_2);
  EEPROM.update(_modemRecordIndex+3, EEPROMBlock.secsSince1900_1);

  EEPROM.update(_modemRecordIndex+4, EEPROMBlock.downMins2);
  EEPROM.update(_modemRecordIndex+5, EEPROMBlock.downMins1);

  EEPROM.update(_modemRecordIndex+7, MODEM_RECORD_IN_PROGRESS);

  return 0;
};

//
//-----------------------------------------------------------------------------
// setEEPROMUptimeStats()
//   Adds the passed modem record to the end of the EEPROM circular list
//
//   Data is stored big endian (ie MSB first)
//   The differences between this and completeLogEntry() are:
//     - This method doesn't index _modemRecordIndex
int EEPROMRecordClass::setEEPROMUptimeStats() {
  short EEPROMlength;
  uint8_t flags;

  EEPROMlength = EEPROM.length();

  // Find the entry with the EEPROM_ENTRY_IN_PROGRESS flag
  _modemRecordIndex = 0;
  EEPROM.get(_modemRecordIndex+7, flags);
  while ((flags != MODEM_RECORD_IN_PROGRESS) && (_modemRecordIndex+7 < EEPROMlength)) {
    _modemRecordIndex += sizeof(EEPROMRecord_t);
    if (_modemRecordIndex+7 < EEPROMlength)
      EEPROM.get(_modemRecordIndex+7, flags);
  };

  // If we haven't found the record in progress just write it at the first record position - 
  // This will destroy a log entry
  if (flags != MODEM_RECORD_IN_PROGRESS)
    _modemRecordIndex = 0;

  EEPROM.update(_modemRecordIndex, EEPROMBlock.secsSince1900_4);
  EEPROM.update(_modemRecordIndex+1, EEPROMBlock.secsSince1900_3);
  EEPROM.update(_modemRecordIndex+2, EEPROMBlock.secsSince1900_2);
  EEPROM.update(_modemRecordIndex+3, EEPROMBlock.secsSince1900_1);

  EEPROM.update(_modemRecordIndex+4, EEPROMBlock.downMins2);
  EEPROM.update(_modemRecordIndex+5, EEPROMBlock.downMins1);

  EEPROM.update(_modemRecordIndex+7, MODEM_RECORD_IN_PROGRESS);

  return;
}; // setEEPROMUptimeStats()

//
//-----------------------------------------------------------------------------
// Get uptime data as stored in the EEPROM.  Used to reinitalise the uptime 
// upon Arduino restart
int EEPROMRecordClass::getEEPROMUptimeStats() {
  int i = 0;
  int found;
  uint8_t flags;
  short EEPROMlength;

  found = false;
  EEPROMlength = EEPROM.length();

  while ((i+7 < EEPROMlength) && (found != true)) {
    EEPROM.get(i+7, flags);
    if (flags == MODEM_RECORD_IN_PROGRESS)
      found = true;
    else 
      i += sizeof(EEPROMRecord_t);
  }

  if (found) {
    getDataFromIndex(i);
  } else {

    EEPROMBlock.secsSince1900_4 = 0;
    EEPROMBlock.secsSince1900_3 = 0; 
    EEPROMBlock.secsSince1900_2 = 0;
    EEPROMBlock.secsSince1900_1 = 0;

    EEPROMBlock.downMins2 = 0;
    EEPROMBlock.downMins1 = 0;

    EEPROMBlock.flags = MODEM_RECORD_IN_PROGRESS;
  }

  return 0;
};

//
//-----------------------------------------------------------------------------
// Converts the data in the passed local record block to a bytewise EEPROM 
// block and writes it to the passed EEPROM record
//
int EEPROMRecordClass::convertToEEPROMBlock(struct modemRecord_t *src) {

  EEPROMBlock.secsSince1900_4 = (src->secsSince1900 >> 24) & 0xff;
  EEPROMBlock.secsSince1900_3 = (src->secsSince1900 >> 16) & 0xff;
  EEPROMBlock.secsSince1900_2 = (src->secsSince1900 >> 8) & 0xff;
  EEPROMBlock.secsSince1900_1 = src->secsSince1900 & 0xff;

  EEPROMBlock.downMins2 = (src->downMins >> 8) & 0xff;
  EEPROMBlock.downMins1 = src->downMins & 0xff;

  EEPROMBlock.flags = MODEM_RECORD_COMPLETE;

  return 0;
}

//
//-----------------------------------------------------------------------------
// Converts the data in the passed EEPROM record block to a local record block
// and writes it to the passed local record
//
int EEPROMRecordClass::convertFromEEPROMBlock(struct modemRecord_t *dst) {

  dst->secsSince1900 = 
    ((uint32_t)EEPROMBlock.secsSince1900_4 << 24) + 
    ((uint32_t)EEPROMBlock.secsSince1900_3 << 16) + 
    ((uint32_t)EEPROMBlock.secsSince1900_2 << 8) + 
     (uint32_t)EEPROMBlock.secsSince1900_1;

  dst->downMins = 
    ((uint16_t)EEPROMBlock.downMins2 << 8) +
     (uint16_t)EEPROMBlock.downMins1;

  return 0;
}

//
//-----------------------------------------------------------------------------
// Send all EEPROM data out through serial port
// *** Port must have already been initialised
//
void EEPROMRecordClass::dumpEEPROM() {
  int row = 0;
  short EEPROMlength;
  char buffer[124];

  EEPROMlength = EEPROM.length();

  Serial.print(F(
    "\r\n"
    "                                                --- EEPROM DUMP ---\r\n"
    "   Hex  Dec                                                                                                      Dec  Hex\r\n"));

  while (row * 32 < EEPROMlength) {
    sprintf(buffer, "  %04X %04d", row*32, row*32);
    for (int i = 0; i < 32; i++) {
      if (i%8 == 0)
        sprintf(buffer, "%s ", buffer);
      int location = (row * 32) + i;
      if (location < EEPROMlength)
        sprintf(buffer, "%s %02X", buffer, EEPROM.read(location));
      else
        sprintf(buffer, "%s   ", buffer);
    }
    sprintf(buffer, "%s  %04d %04X", buffer, ((row+1)*32)-1, ((row+1)*32)-1);
    Serial.println(buffer);
    row++;
  };

  Serial.print(F(
    "\r\n"
    "                                               --- End Of EEPROM ---\r\n"));
  return;
};

//-----------------------------------------------------------------------------
// End of file
//-----------------------------------------------------------------------------

