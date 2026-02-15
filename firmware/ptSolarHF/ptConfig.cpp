/*
A Configuration object to storing settings for Project: Traveler Flight Controllers
Copyright 2011-2026 - Zack Clobes (W0ZC), Custom Digital Services, LLC

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.  If not, see <http://www.gnu.org/licenses/>.

Version History:
Version 1.2.0 - January 28, 2026 - Added config version 0200 mode for WSPR support.
Version 1.1.1 - July 20, 2025 - Synchronized the ptFlex and ptSolar code bases to be parameterized by the TRACKER_PTFLEX and TRACKER_PTSOLAR defines.
Version 1.1.0 - July 12, 2025 - Updated to PT0101 configuration format, which simplied a few unused parameters.
Version 1.0.0 - March 9, 2025 - Initial Release.

*/

#include "ptConfig.h"


ptConfig::ptConfig() {
  this->readEEPROM();
}



/**
 * @brief  Read the configuration from the EEPROM.
 * @note   This function reads the configuration from the EEPROM.  It is called at startup to load the configuration.
 */
void ptConfig::readEEPROM() {
    for (unsigned int i=0; i<sizeof(this->_config); i++) {
        *((char*)&this->_config + i) = EEPROM.read(i);
    }

    //Check to see if the EEPROM appears to be valid
    unsigned int iCheckSum = 0;
    for (int i=0; i<7; i++) {
        iCheckSum += this->_config.Callsign[i];
    }

    if (iCheckSum != this->_config.CheckSum) {
        //we do NOT have a match - reset the Config variables
        this->setDefaultConfig();
    }
}


/**
 * @brief  Write the default configuration to the EEPROM.
 * @note   Used when the EEPROM is not initialized or has been corrupted. Checksum is based on the string inside of _config.Callsign.
 */
void ptConfig::setDefaultConfig() {

    wdt_reset();    //reset the watchdog timer

    //Set the unit-specific default configurations
    this->_config.VoltThreshGPS = 3500;    //3.5V
    this->_config.VoltThreshXmit = 3600;    //3.6V   
    this->_config.AnnounceMode = 1;    //0=No Annunciations, 1=LED, 2=Piezo, 3=Both
    this->_config.HourlyReboot = 0;    //reboot the system every hour   
    strcpy(this->_config.Callsign, "N0CALL");
    this->_config.FrequencyTx1 = 28126100UL;   //Default to 10m, 28.1261MHz
    this->_config.FrequencyTx2 = 21093000UL;   //Default to 20m, 14.0970MHz
    this->_config.ToneOffset = 1500;    //Default tone offset for WSPR audio generation
    this->_config.Correction = 0;    //No frequency correction by default
    this->_config.WSPRMessageType = 2;    //Type 2/3 messages
    this->_config.TxMod = 6;    //Transmit every 6 minutes (minutes modulus 6)
    this->_config.TxModOffset = 0;    //No offset

    this->_config.CheckSum = 410;		//Checksum for N0CALL
  
    this->writeEEPROM();
}


/**
 * @brief  Write the configuration to the EEPROM.
 * @note   This function writes the configuration to the EEPROM.  It is called after the configuration has been updated.
 */
void ptConfig::writeEEPROM() {
    for (unsigned int i=0; i<sizeof(this->_config); i++) {
        EEPROM.write(i, *((char*)&this->_config + i));
    }
}


/**
 * @brief Reads in serial line data from the PC until it finds a tab (0x09) or an End of Transmission (0x04) character.
 * @param szParam - The array to store the incoming data while it's being collected. This parameter is by reference and will be modified.
 * @param iMaxLen - The maximum length of the incoming data. Any data exceeding the iMaxLen will be discarded.
 * @note  The function will timeout if it doesn't receive anything within 1 second.
 */
void ptConfig::readConfigParam(char *szParam, int iMaxLen) {
    byte c;
    int iSize;
    unsigned long iMilliTimeout = millis() + 1000;    //wait up to 1 second for this data
  
    for (iSize=0; iSize<iMaxLen; iSize++) szParam[iSize] = 0x00;    //load the array with nulls just in case we don't find anything
    iSize = 0;    //reset to start counting up for real
  
    while (millis() < iMilliTimeout) {
  
      wdt_reset();    //reset the watchdog timer
      if (Serial.available()) {
        c = Serial.read();
  
        if (c == 0x09 || c == 0x04) {
          //this is the end of a data set
  
          return;
        }
        if (iSize < iMaxLen) {
          //only add to the return array IF there's room.  Even if there's not room, continue to parse the incoming data until a tab is found.
          szParam[iSize] = c;
          iSize++;
        }
      }
    }
    Serial.println(F("Timeout"));
  }
  

  /**
   * @brief Reads in the configuration data from the PC and loads it into the Config UDT.
   * @return True if the configuration data was successfully read in.  False if there was an error.
   * @note  
   */
  bool ptConfig::getConfigFromPC() {
  
    char szParam[45];
    unsigned long iMilliTimeout = millis() + 10000;    //wait up to 10 seconds for this data
  
    while (millis() < iMilliTimeout) {
      wdt_reset();    //reset the watchdog timer
      while (!Serial.available()) {
        //wait
        
      }
      if (Serial.available()) {
        //we have something to read
      
        if (Serial.read() == 0x01) {
    
          //we have the start to a config string
    
          this->readConfigParam(szParam, sizeof(szParam));    //should be PT0200 for the ptSolarHF
          if (strcmp(szParam, CONFIG_VERSION) != 0) {
            //not a config string
            Serial.println(F("No Config Type"));
            Serial.print(F(" : "));
            Serial.println(szParam);
            return false;
          }
    
          this->readConfigParam(szParam, sizeof(this->_config.Callsign));    //Callsign
          strcpy(this->_config.Callsign, szParam);

          this->readConfigParam(szParam, sizeof(szParam));
          this->_config.VoltThreshGPS = atoi(szParam);   //Threshold for voltage before activating the GPS receiver

          this->readConfigParam(szParam, sizeof(szParam));
          this->_config.VoltThreshXmit = atoi(szParam);   //Threshold for voltage before transmitting a packet

          this->readConfigParam(szParam, 9);    //Transmit Frequency for si5351 //Up to 9 digits, which is well within the range of a unsigned 32-bit integer
          this->_config.FrequencyTx1 = this->atou32(szParam);

          this->readConfigParam(szParam, 9);    //Transmit Frequency for si5351 (secondary) //Up to 9 digits, which is well within the range of a unsigned 32-bit integer
          this->_config.FrequencyTx2 = this->atou32(szParam);

          this->readConfigParam(szParam, 9);  //Tone Offset for si5351 //Up to 9 digits, which is well within the range of a signed 32-bit integer
          this->_config.ToneOffset = atoi(szParam);   //Tone offset for generating the WSPR audio

          this->readConfigParam(szParam, 9);    //Up to 9 digits, which is well within the range of a signed 32-bit integer
          this->_config.Correction = this->atoi32(szParam);    //Frequency correction in parts per billion

          this->readConfigParam(szParam, sizeof(szParam));
          this->_config.AnnounceMode = atoi(szParam);   //Annunciator Type

          this->readConfigParam(szParam, sizeof(szParam));
          this->_config.WSPRMessageType = atoi(szParam);    //WSPR Message Type

          this->readConfigParam(szParam, sizeof(szParam));
          this->_config.TxMod = atoi(szParam);    //Transmit Modulus

          this->readConfigParam(szParam, sizeof(szParam));
          this->_config.TxModOffset = atoi(szParam);    //Transmit Modulus Offset

          this->readConfigParam(szParam, sizeof(szParam));
          this->_config.HourlyReboot = (szParam[0] == '1');    //Reboot the system every hour
    
          unsigned int iCheckSum = 0;
          for (int i=0; i<7; i++) {
            iCheckSum += this->_config.Callsign[i];
          }
          this->_config.CheckSum = iCheckSum;
          return true;    //done reading in the file
        }
      }
    }
    return false;
  }
  
  
  /** 
   * @brief Sends the configuration data to the PC so that the configuration can be verified and managed.
   * @note  This function will send the configuration data to the PC in a tab-delimited format. 
   */
  void ptConfig::sendConfigToPC() {
    wdt_reset();    //reset the watchdog timer
    Serial.write(0x01);
    Serial.write(CONFIG_VERSION);
    Serial.write(0x09);

    Serial.write(this->_config.Callsign);
    Serial.write(0x09);

    //Voltage Thresholds
    Serial.print(this->_config.VoltThreshGPS, DEC);
    Serial.write(0x09);
    Serial.print(this->_config.VoltThreshXmit, DEC);
    Serial.write(0x09);

    //Transmit Frequencies
    Serial.print(this->_config.FrequencyTx1, DEC);
    Serial.write(0x09);
    Serial.print(this->_config.FrequencyTx2, DEC);
    Serial.write(0x09);
    Serial.print(this->_config.ToneOffset, DEC);
    Serial.write(0x09);
    Serial.print(this->_config.Correction, DEC);    //Frequency Correction
    Serial.write(0x09);

    Serial.print(this->_config.AnnounceMode, DEC);    //0=No annunciator, 1=LED only, 2=Piezo only, 3=Both
    Serial.write(0x09);

    //Transmit timings
    Serial.print(this->_config.WSPRMessageType, DEC);    //WSPR Message Type
    Serial.write(0x09);
    Serial.print(this->_config.TxMod, DEC);    //Transmit Modulus
    Serial.write(0x09);
    Serial.print(this->_config.TxModOffset, DEC);    //Transmit Modulus Offset
    Serial.write(0x09);
    
    if (this->_config.HourlyReboot) Serial.write("1");    //Hourly Reboot
    else Serial.write("0");
    Serial.write(0x04);      //End of string

    wdt_reset();    //reset the watchdog timer
    Serial.flush();     //Wait for the serial port to finish sending the data

  }


uint32_t ptConfig::atou32(const char *s) {
    uint32_t value = 0;

    while (*s >= '0' && *s <= '9') {
        value = value * 10u + (uint32_t)(*s - '0');
        s++;
    }

    return value;
}

int32_t ptConfig::atoi32(const char *s) {
    int32_t value = 0;
    bool isNegative = false;

    // Check for negative sign
    if (*s == '-') {
        isNegative = true;
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (int32_t)(*s - '0');
        s++;
    }

    return isNegative ? -value : value;
}
