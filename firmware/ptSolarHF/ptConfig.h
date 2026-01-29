/*
A Configuration object to storing settings for Project: Traveler Flight Controllers
Copyright 2011-2026 - Zack Clobes (W0ZC), Custom Digital Services, LLC

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef ptConfig_h
#define ptConfig_h

#include "BoardDef.h"

#include <stdint.h>   //standard data types available, such as uint8_t
#include <arduino.h>
#include <EEPROM.h>
#include <avr/wdt.h>

#define CONFIG_VERSION "PT0200"

class ptConfig {
  // Public Functions
  public:
      // Constructor
      ptConfig();
  
      // Public Functions
      void readEEPROM();
      void setDefaultConfig();
      void writeEEPROM();
  
      void ptConfig::readConfigParam(char *szParam, int iMaxLen);
      bool ptConfig::getConfigFromPC();
      void ptConfig::sendConfigToPC();

  
      //Getters and Setters
      char* getCallsign() { return _config.Callsign; }
      void setCallsign(char* callsign) { strcpy(_config.Callsign, callsign); }
  
      bool getRebootHourly() { return _config.HourlyReboot; }
      void setRebootHourly(bool reboot) { _config.HourlyReboot = reboot; }      
  
      unsigned int getVoltThreshGPS() { return _config.VoltThreshGPS; }
      void setVoltThreshGPS(unsigned int thresh) { _config.VoltThreshGPS = thresh; }
  
      unsigned int getVoltThreshXmit() { return _config.VoltThreshXmit; }
      void setVoltThreshXmit(unsigned int thresh) { _config.VoltThreshXmit = thresh; }
  
      uint32_t getFrequencyTx() { return _config.FrequencyTx; }
      void setFrequencyTx(uint32_t freq) { _config.FrequencyTx = freq; }

      int32_t getCorrection() { return _config.Correction; }
      void setCorrection(int32_t corr) { _config.Correction = corr; }

      uint8_t getAnnounceMode() { return _config.AnnounceMode; }
      void setAnnounceMode(uint8_t mode) { _config.AnnounceMode = mode; }

      unsigned int getCheckSum() { return _config.CheckSum; }
      void setCheckSum(unsigned int sum) { _config.CheckSum = sum; }
  
  private:
    // Private Variables

    struct udtConfig {
        char Callsign[7];    //6 digit callsign + Null
        unsigned int VoltThreshGPS;    //The voltage threshold to activate the GPS and read a position (in millivolts)
        unsigned int VoltThreshXmit;    //The voltage threshold to transmit a packet (in millivolts)
        uint32_t FrequencyTx;    //The transmit frequency in Hz
        int32_t Correction;    //Frequency correction in parts per billion
        uint8_t AnnounceMode;    //0=No annunciator, 1=LED only, 2=Piezo only, 3=Both
        bool HourlyReboot;

        unsigned int CheckSum;    //sum of the callsign element.  If it doesn't match, then it reinitializes the EEPROM
      } _config;
};
#endif
