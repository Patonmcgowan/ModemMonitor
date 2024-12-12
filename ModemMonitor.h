//
// ModemMonitor.h
// 
// Data definition and function prototype file for ModemMonitor.ino
//
//------------------------------------------------------------------------------
//  Revision History
//  ~~~~~~~~~~~~~~~~
//    12 Oct 2024 MDS Original
//
//------------------------------------------------------------------------------

#ifndef __MODEM_MONITOR_H
#define __MODEM_MONITOR_H

// Working record for modem uptime data
struct modemRecord_t {
  uint32_t secsSince1900;       // Time of event
  uint16_t downMins;            // Minutes that the modem was down
  uint16_t waitSecs;            // How long have we been waiting after the last restart for the modem to come online ?
};

#endif

//-----------------------------------------------------------------------------
// End of file
//-----------------------------------------------------------------------------

