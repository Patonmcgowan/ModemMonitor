This project fits quite nicely in an Uno.  It requires the following libraries:
  <Arduino.h>       - General requirements
  <stdint.h>        - General requirements
  <avr/pgmspace.h>  - This facilitates lots of string storage in flash memory
  <SPI.h>           - Required to allow the Uno to talk to the Ethernet shield
  <Ethernet.h>      - Must be V1.1.2 of the Ethernet library (code wouldn't compile with V2.0.0 through V2.0.2)
  <EthernetUdp.h>   - Needed for the NTP requests
  <Dns.h>           - Has functions to go out and resolve the URLs in the code to IP addresses

Outages are recorded on the EEPROM in a circular list to retain the outage history if new code is uploaded or the Arduino is powered down.

Default speed for the serial port is 115200 baud

Upon startup, a help menu will be displayed for serial commmand which is self explanatory - it allows reinitialisation of the EEPROM, simulation of poll failure etc etc
