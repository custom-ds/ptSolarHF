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
  
      void readConfigParam(char *szParam, int iMaxLen);
      bool getConfigFromPC();
      void sendConfigToPC();

  
      //Getters and Setters
      const char* getCallsign() const { return _config.Callsign; }
      //Overload that allows copying the callsign into a provided buffer, with specified length
      void getCallsign(char* out, size_t outLen) const {
        if (out && outLen > 0) {
          strncpy(out, _config.Callsign, outLen);
          out[outLen - 1] = '\0';
        }
      }
      void setCallsign(char* callsign) { strcpy(_config.Callsign, callsign); }
    
      unsigned int getVoltThreshGPS() { return _config.VoltThreshGPS; }
      void setVoltThreshGPS(unsigned int thresh) { _config.VoltThreshGPS = thresh; }
  
      unsigned int getVoltThreshXmit() { return _config.VoltThreshXmit; }
      void setVoltThreshXmit(unsigned int thresh) { _config.VoltThreshXmit = thresh; }
  
      uint32_t getFrequencyTx1() { return _config.FrequencyTx1; }
      void setFrequencyTx1(uint32_t freq) { _config.FrequencyTx1 = freq; }

      uint32_t getFrequencyTx2() { return _config.FrequencyTx2; }
      void setFrequencyTx2(uint32_t freq) { _config.FrequencyTx2 = freq; }

      int32_t getToneOffset() { return _config.ToneOffset; }
      void setToneOffset(int32_t offset) { _config.ToneOffset = offset; }

      bool getFineAltitudeModulation() { return _config.FineAltitudeModulation; }
      void setFineAltitudeModulation(bool fineAlt) { _config.FineAltitudeModulation = fineAlt; }

      int32_t getCorrection() { return _config.Correction; }
      void setCorrection(int32_t corr) { _config.Correction = corr; }

      uint8_t getAnnounceMode() { return _config.AnnounceMode; }
      void setAnnounceMode(uint8_t mode) { _config.AnnounceMode = mode; }

      uint8_t getWSPRMessageType() { return _config.WSPRMessageType; }
      void setWSPRMessageType(uint8_t type) { _config.WSPRMessageType = type; }

      uint8_t getTxMod() { return _config.TxMod; }
      void setTxMod(uint8_t mod) { _config.TxMod = mod; }

      uint8_t getTxModOffset() { return _config.TxModOffset; }
      void setTxModOffset(uint8_t offset) { _config.TxModOffset = offset; }

      bool getRebootHourly() { return _config.HourlyReboot; }
      void setRebootHourly(bool reboot) { _config.HourlyReboot = reboot; }

      unsigned int getCheckSum() { return _config.CheckSum; }
      void setCheckSum(unsigned int sum) { _config.CheckSum = sum; }
  
  private:
    uint32_t atou32(const char* str);
    int32_t atoi32(const char *s);

    // Private Variables
    struct udtConfig {
        char Callsign[11];    //10 digit callsign + Null
        unsigned int VoltThreshGPS;    //The voltage threshold to activate the GPS and read a position (in millivolts)
        unsigned int VoltThreshXmit;    //The voltage threshold to transmit a packet (in millivolts)
        uint32_t FrequencyTx1;    //The transmit frequency in Hz
        uint32_t FrequencyTx2;    //The transmit frequency in Hz (secondary, for dual frequency operation)
        int32_t ToneOffset;    //Offset to apply to the FrequencyTx1/2 to generate the audio tones. Normally around 1600, but can be tweaked for spacing.
        bool FineAltitudeModulation;    //Whether to use fine altitude modulation for WSPR encoding, where 1400Hz tone offset is zero, and 1600Hz tone offset is 100% of the fine altitude
        int32_t Correction;    //Frequency correction in parts per billion
        uint8_t AnnounceMode;    //0=No annunciator, 1=LED only
        uint8_t WSPRMessageType;    //Type of WSPR message to send - 
                        // 1 = Type 1 - Standard WSPR message with callsign, 4-digit grid square, and altitude
                        // 2 = Type 2/Type 3 - Compressed WSPR message with callsign, 6-digit grid square, and altitude pair
                        // 129 = Type 1 message, same as 0 except alternating between the two transmit frequencies
                        // 130 = Type 2/Type 3 message, same as 1 except alternating between the two transmit frequencies
        uint8_t TxMod;    //How often to transmit = 2=every 2 minutes, 4=every 4 minutes, etc. Must be a multiple of 2
        uint8_t TxModOffset;    //Offset within the TxMod to transmit on.  For example, if TxMod=4 and TxModOffset=2, it will transmit at minutes 2, 6, 10, etc.
        bool HourlyReboot;

        unsigned int CheckSum;    //sum of the callsign element.  If it doesn't match, then it reinitializes the EEPROM
      } _config;
};
#endif
