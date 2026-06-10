 /*
Project: Traveler ptSolarHF Firmware
Copyright 2011-2026 - Zack Clobes (W0ZC), Custom Digital Services, LLC


This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.  If not, see <http://www.gnu.org/licenses/>.


Before programming for the first time, the ATmega fuses must be set.
 Low:      0xDF
 High:     0xD6
 Extended: 0xFD


This firmware requires the following libraries to be installed with the Library Manager inside of the Arduino IDE:
  - Si5351 Library by Etherkit

*/


#define FIRMWARE_VERSION "1.0.2"
#define CONFIG_PROMPT "\n\n# "
#include "BoardDef.h"   //defines if this is a ptFlex, ptSolar, or ptSolarHF PCB board



#define __PROG_TYPES_COMPAT__
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <si5351.h>
#include <string.h>
#include <Wire.h>

#include "wspr_enc.h"
#include "nhash.h"
#include "ptConfig.h"
#include "GPS.h"
#include "ptTracker.h"


//PD0 is Serial Port RX
//PD1 is Serial Port TX
#define PIN_PTT_OUT 2     //PD2
//PD3 is not used
//PD4 is not used
#define PIN_AUDIO 5       //PD5   - Audio Annunciation
#define PIN_GPS_EN 6      //PD6
#define PIN_GPS_TX 7      //PD7

#define PIN_GPS_RX 8      //PB0
//PB1 is not used
//PB2 is not used
//PB3 is MOSI
//PB4 is MISO
#define PIN_LED 13        //PB5

//Analog Pins
//PC0 is not used
#define PIN_ANALOG_BATTERY A1   //PC1
//PC2 is not used
//PC3 is not used
//PC4 is SDA
//PC5 is SCL
//PC6 is reset and not available


//Debugging options
#define CONFIG_TIMEOUT 600000

#define WSPR_TONE_SPACING_CENTI_HZ 146
#define WSPR_SYMBOL_COUNT 162
#define WSPR_TONE_MS 683


Si5351 si5351;
static uint8_t wspr_symbols[WSPR_SYMBOL_COUNT];
ptConfig Config;                                                                        //Configuration object
ptTracker Tracker(PIN_LED, PIN_AUDIO, PIN_ANALOG_BATTERY, Config.getAnnounceMode());    //Object that manages the board-specific functions
GPS GPSParser(PIN_GPS_RX, PIN_GPS_TX, PIN_GPS_EN);                                      //Object that parses the GPS strings

//Keep track of the grid square and altitude between transmissions
uint8_t coarseAlt, fineAlt;
char szGrid[7];

enum XmitState{
    NO_XMIT,
    XMIT_TYPE1,
    XMIT_TYPE2,
    XMIT_TYPE3
};
XmitState xmitState = NO_XMIT;
XmitState nextXmitState = NO_XMIT;

/**
 * @brief  The function that runs first before the main loop() function is called indefinitely.
 * @note   This function is called once at startup and is used to initialize the board and set up the hardware.
 */
void setup() {
    Serial.begin(19200);

    pinMode(PIN_PTT_OUT, OUTPUT);
    digitalWrite(PIN_PTT_OUT, LOW);    //Set PTT to low (not transmitting)

    wdt_disable();    //disable the watchdog timer by default
    wdt_enable(WDTO_8S);    //Enable the Watchdog if configured

    wdt_reset();    //reset the watchdog timer (even if we're not using it)
    showVersion();    //show the version of the firmware that we're running

    Tracker.annunciate('k');

    wdt_reset();

    Serial.println(F("Init Si5351"));
    si5351.init(SI5351_CRYSTAL_LOAD_10PF, 27000000, 0);
    si5351.set_correction(Config.getCorrection(), SI5351_PLL_INPUT_XO);
    si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);
    si5351.output_enable(SI5351_CLK0, 0);       //Turn off any outputs for now

    GPSParser.setDebugNEMA(true);    ///TODO: Need to pull this from Configuration
    GPSParser.setDebugLevel(1);    //Get full verbose output from the GPS

    //GPSParser.testWSPRAltitude();    //Test the WSPR altitude calculation function with some known values to make sure it's working correctly
}


/**
 * @brief  Main loop for the program.  This is where the main logic of the program is executed.
 * @note   This function will run continuously until the board is powered off or reset.
 */
void loop()
{

  static unsigned long battMillivolts;
  static uint8_t iSeconds, iMinutes, iHours;
  static byte byTemp;
  static bool bUseFreq1 = true; // Used to alternate between the two frequencies if configured to do so

  wdt_reset();

  // Check to see if we have a command from the serial port to indicate that we need to enter config mode
  if (Serial.available())
  {
    byTemp = Serial.read();
    if (byTemp == '!')
    {
      doConfigMode();
    }
  }

  Serial.println("");
  Serial.println("");
  for (int i = 0; i < 40; i++)
    Serial.print("=");
  Serial.println("");

  // Reboot the system hourly if configured to do so
  if (Config.getRebootHourly())
  {
    // Reboot if we've been running for 60 minutes
    if (millis() > 3600000)
    {
      // we've been running for 60 minutes - reboot the system
      Serial.println(F("60min Reboot"));
      delay(1000);
      Tracker.reboot();
    }
  }

  battMillivolts = (unsigned long)(Tracker.readBatteryVoltage(true) * 1000); // read the battery voltage and spit it out to the serial port

  // check to see if we have sufficient battery to run the GPS
  if (battMillivolts >= Config.getVoltThreshGPS())
  {
    GPSParser.enableGPS(true); // enable the GPS module if it's not already. If it wasn't enabled, this will also initialize it.
    GPSParser.collectGPSStrings();
  }
  else
  {
    // See if the Battery has dropped 100mV below the threshold.  If so, disable the GPS until the battery comes back up
    if (battMillivolts < (Config.getVoltThreshGPS() - 100))
    {
      // we don't have enough battery to run the GPS - disable it
      Serial.println(F("Disable GPS"));
      GPSParser.disableGPS();
    }
    Serial.println(F("Low Batt, no GPS"));
    delay(750); // wait for about the amount of time that we'd normally spend grabbing a GPS reading
  }

  xmitState = NO_XMIT; // Need to assume we're not transmitting even if it was skipped last time around. Otherwise it may transmit beyond top of the minute.
  GPSParser.getGPSTime(&iHours, &iMinutes, &iSeconds);
  if (iSeconds <= 2 && iMinutes % 2 == 0)
  {
    Serial.println(F("TOM")); // Top of minute

    if (nextXmitState != NO_XMIT)
    {
      // We have a pending transmit state from the last time we checked - use that instead
      xmitState = nextXmitState;
      nextXmitState = NO_XMIT;
      // No need to get the Grid/altitude since it was already captures when sending the Type2.
      Serial.println(F("Xmit next"));
    }
    else
    {

      // preload the position and altitude so that we don't have to worry about the GPS loosing lock during transmissions
      GPSParser.getWSPRAltitude(coarseAlt, fineAlt);
      GPSParser.getGridSquare(szGrid, 6);

      if ((!GPSParser.FixQuality() || GPSParser.NumSats() < 4) || GPSParser.isRFBlackoutZone())
      {
        if (GPSParser.isRFBlackoutZone())
        {
          Serial.print(F("Blackout"));
        }
        else
        {
          Serial.print(F("GPS"));
        }
        Serial.println(F("-No Xmit"));
        wdt_reset();
      }
      else
      {
        //We weren't set to transmit a Type 3 message, and we're not in a blackout zone, and we have a valid GPS lock - see if it's time to transmit
        
        if ((iMinutes + Config.getTxModOffset()) % Config.getTxMod() == 0)
        {
          Serial.print(F("Xmit "));
          // It's time to transmit on this cycle - see what we're configured to send
          if (Config.getWSPRMessageType() & 0x01)
          {
            // Type 1 message
            xmitState = XMIT_TYPE1;
            nextXmitState = NO_XMIT;

            Serial.println(F("1"));
          }
          else if (Config.getWSPRMessageType() & 0x02)
          {
            // Type 2/3 message
            xmitState = XMIT_TYPE2;
            nextXmitState = XMIT_TYPE3;

            Serial.println(F("2/3"));
          }
        }
      }
    }
  }

  if (xmitState != NO_XMIT)
  {

    // If there's still a valid transmit state, go ahead and build and send the WSPR packet
    if (xmitState == XMIT_TYPE1)
    {
      buildWSPRSymbols(1);
      sendWSPR(bUseFreq1);

      if (Config.getWSPRMessageType() & 0x80)
        bUseFreq1 = !bUseFreq1; // Alternate frequencies between packets
      else
        bUseFreq1 = true; // Always use the primary frequency if we're not configured to alternate
    }
    else if (xmitState == XMIT_TYPE2)
    {
      buildWSPRSymbols(2);
      sendWSPR(bUseFreq1);

      // don't need to alternate after the type 2. Wait for the type 3
    }
    else if (xmitState == XMIT_TYPE3)
    {
      buildWSPRSymbols(3);
      sendWSPR(bUseFreq1);

      if (Config.getWSPRMessageType() & 0x80)
        bUseFreq1 = !bUseFreq1; // Alternate frequencies between packets
      else
        bUseFreq1 = true; // Always use the primary frequency if we're not configured to alternate
    }

    xmitState = NO_XMIT; // we're done transmitting - reset
  }
}

/**
 * @brief   Constructs the WSPR symbols (packet) to be transmitted based on the current GPS data and configuration.
 * @note    Once the symbols are built, they are stored in the global wspr_symbols array.
 */
static void buildWSPRSymbols(uint8_t msgType) {
    char szAltitude[4];
    char szCallsign[11];      //WSPR supports 10 digit callsigns
    char cGridFithDigit = '\0';

    wdt_reset();

    Serial.print(F("WSPR "));
    Serial.println(msgType);
    //Calculate the coarse altitude for WSPR encoding
    itoa(coarseAlt, szAltitude, 10);

    Config.getCallsign(szCallsign, sizeof(szCallsign));

    //Message type gets infered from the following logic:
    // Type 1 - If a standard 4-character grid square is used and a "normal" callsign which is <= 6 characters and no slashes
    // Type 2 - If a standard 4-character grid square is used and a "special" callsign which is > 6 characters or has a slash
    // Type 3 - If a 6-character grid square is used
    //
    // Convention states that a type 2 and type 3 message should be used together to convey more information. If both packets aren't sent
    //  together WSPRNet.org won't display them correctly, and will use the prior six-digit grid square with a newer Type 2 location.


    
    Serial.print(F("Cal "));
    Serial.println(szCallsign);
    Serial.print(F("Grd "));
    Serial.println(szGrid);
    Serial.print(F("Alt "));
    Serial.println(szAltitude);


    //Build the WSPR symbols
    if (msgType < 3)
    {
      //Convert this into a 4 digit grid square
      cGridFithDigit = szGrid[4];
      szGrid[4] = '\0';
      wspr_enc(szCallsign, szGrid, szAltitude, wspr_symbols); 

      //replace the fifth digit
      szGrid[4] = cGridFithDigit;
    } 
    else 
    {
       wspr_enc(szCallsign, szGrid, szAltitude, wspr_symbols);
    }
}


/**
 * @brief   Transmits the WSPR symbols that were previously built using the Si5351 clock generator.
 * @note    This function will transmit the WSPR symbols stored in the global wspr_symbols array. It takes about 111 seconds to transmit the entire packet.
 */
static void sendWSPR(bool useFreq1) {
    if (Config.getFineAltitudeModulation()) {
      if (fineAlt > 200) fineAlt = 100;
    } else {
      fineAlt = 0;    // If we're not using fine altitude modulation, set the fine altitude to zero so that it doesn't affect the tone frequencies  
    }
    const uint64_t base_cHz = (uint64_t)((useFreq1 ? Config.getFrequencyTx1() : Config.getFrequencyTx2()) + Config.getToneOffset() + fineAlt) * 100ULL;
    byte byTemp;
    Serial.print(F("Xmit "));
    Serial.println((useFreq1 ? "freq 1" : "freq 2"));

    //make sure the correction has been set before we transmit
    si5351.set_correction(Config.getCorrection(), SI5351_PLL_INPUT_XO);
    si5351.output_enable(SI5351_CLK0, 1);
    digitalWrite(PIN_PTT_OUT, HIGH);

    for (uint16_t i = 0; i < WSPR_SYMBOL_COUNT; i++)
    {
        wdt_reset();
        const uint8_t sym = wspr_symbols[i] & 0x03;
        const uint64_t f_cHz = base_cHz + (uint64_t)sym * (uint64_t)WSPR_TONE_SPACING_CENTI_HZ;
        si5351.set_freq(f_cHz, SI5351_CLK0);

        //Check if we're trying to interrupt the transmission to enter config mode
        if (Serial.available())
        {
            byTemp = Serial.read();
            if (byTemp == '!') 
            {
                //Shut the transmitter down
                si5351.output_enable(SI5351_CLK0, 0);
                digitalWrite(PIN_PTT_OUT, LOW);

                //Go into config mode
                doConfigMode();
            }
        }
        delay(WSPR_TONE_MS);
    }

    si5351.output_enable(SI5351_CLK0, 0);
    digitalWrite(PIN_PTT_OUT, LOW);
    
    //The GPS will turn back on at the top of the loop(), assuming the battery voltage is sufficient, so we don't need to turn it back on here.

    Serial.println(F("Done"));
}


/**
 * @brief showVersion - Displays the version of the firmware and configuration
 * @return void
 */
void showVersion() {
  Serial.println(F("ptSolarHF Tracker"));
  Serial.print(F("Firmware Version: "));
  Serial.println((char *)FIRMWARE_VERSION);
  Serial.print(F("Config Version: "));
  Serial.println(CONFIG_VERSION);
  Serial.flush();
}


/**
 * @brief doConfigMode - This function is used to enter the configuration mode of the tracker. There are diagnostic routines, and the ability to read/write the EEPROM settings.
 * @return void
 */
void doConfigMode() {
  byte byTemp;

  showVersion();
  Serial.print(F(("Corr: ")));
  Serial.println(Config.getCorrection());
  Serial.print(CONFIG_PROMPT);

  delay(750);
  Tracker.annunciate('c');

  //keep track of how long we can listen to the GPS
  unsigned long ulUntil = millis() + CONFIG_TIMEOUT;
  
  while (millis() < ulUntil ) {
    //Endless loop. Only exit is to reboot with the 'Q', or after 10 minutes of inactivity
    wdt_reset();
    if (Serial.available()) {
      byTemp = Serial.read();


      if (byTemp == '!') {
        showVersion();
      }

      if (byTemp == '0') {
        //used to reset the tracker back to N0CALL defaults
        Serial.println(F("Defaults"));
        Config.setDefaultConfig();        
        Tracker.annunciate('w');
      }


      if (byTemp == 'E' || byTemp == 'e') {
        //exercise mode to check out all of the I/O ports
        
        Serial.println(F("Exercise"));
        
        Serial.println(F(" annun"));
        Config.setAnnounceMode(0x03);    //temporarily set the announce mode to both
        Tracker.annunciate('x');
        
        Serial.println(Tracker.readBatteryVoltage(true));
        GPSParser.collectGPSStrings();   //check the GPS  
      }

      if (byTemp == 'Q' || byTemp == 'q') {
        //Quit the config mode
        reboot();
      }

      if (byTemp == 'R' || byTemp == 'r') {
        Config.readEEPROM();    //pull the configs from eeprom
        Config.sendConfigToPC();

        reboot();    //reboot the system while we're waiting for a new config to be loaded
      }

      //Adjust the Correction factor
      if (byTemp == 'd') {
        //down by 100
        Config.setCorrection(Config.getCorrection() - 100);
        Config.writeEEPROM();   //store the change
      }
      if (byTemp == 'D') {
        //down by 1000
        Config.setCorrection(Config.getCorrection() - 1000);
        Config.writeEEPROM();   //store the change
      }
      if (byTemp == 'u') {
        //up by 100
        Config.setCorrection(Config.getCorrection() + 100);
        Config.writeEEPROM();   //store the change
      }
      if (byTemp == 'U') {
        //up by 1000
        Config.setCorrection(Config.getCorrection() + 1000);
        Config.writeEEPROM();   //store the change
      }
      if (byTemp == 'z' || byTemp == 'Z') {
        //reset to zero
        Config.setCorrection(0);
        Config.writeEEPROM();   //store the change
      }

      //Test the transmitter for frequency accuracy. Lower case 't' gives 5 seconds, upper case 'T' gives 30 seconds
      if (byTemp == 't' || byTemp == 'T') {
        si5351.set_correction(Config.getCorrection(), SI5351_PLL_INPUT_XO);
        si5351.set_freq(1000000000ULL, SI5351_CLK0);
        si5351.output_enable(SI5351_CLK0, 1);
        digitalWrite(PIN_PTT_OUT, HIGH);

        uint8_t seconds = 5;
        if (byTemp == 'T') seconds = 30;    //long test
        //Set to plenty of time so that the analyzer can get a fine reading on it
        for (uint8_t i=0; i<seconds; i++) {
          Serial.print(F("."));
          delay(1000);
          wdt_reset();
        }
        Serial.println("");
        si5351.output_enable(SI5351_CLK0, 0);
        digitalWrite(PIN_PTT_OUT, LOW);
      }

      //Send a WSPR packet. Lower case 'p' send either a type 1 or type 2 on the primary frequency, upper case 'P' transmits on the secondary frequency
      if (byTemp == 'p' || byTemp == 'P') {
        //Send a test packet
        Serial.println(F("Test Packet"));
        if (Config.getWSPRMessageType() & 0x01) {
          // Type 1 message
          buildWSPRSymbols(1);
        }
        else if (Config.getWSPRMessageType() & 0x02) {
          // Type 2 message
          buildWSPRSymbols(2);
        }
        sendWSPR((byTemp == 'p'));    //Use the primary frequency for 'p' and the secondary frequency for 'P'
      }

      //Quit out of configuration mode and reboot the system
      if (byTemp == 'q') {
        //Quit
        reboot();    //reboot the system  
      }

      if (byTemp == 'W' || byTemp == 'w') {
        //take the incoming configs and load them into the Config UDT

        Serial.println(F("Config mode..."));    //NOTE: This wording is critical for the ptConfigurator to know that we're in config mode

        if (Config.getConfigFromPC()) {
          Serial.println(F(" loaded"));

          Config.writeEEPROM();
          Serial.println(F(" saved"));

          Tracker.annunciate('w');
        } else {
          //something failed during the read of the config data
          Serial.println(F(" failed"));
        }

        reboot();    //reboot the system to apply the new configuration
      }

      Serial.println("");
      Serial.print(F(("Corr: ")));
      Serial.println(Config.getCorrection());
      Serial.print(CONFIG_PROMPT);
      ulUntil = millis() + CONFIG_TIMEOUT;    //reset the timer for the config mode
    }
  }
  
  reboot();    //reboot the system if we get here - this is the only way out of the endless loop  
}


/**
 * @brief reboot - This function reboots the system by resetting the ATmega328P microcontroller.
 * @return void
 */
void reboot() {
  //Reboot the system
  Serial.println(F("Rebooting..."));
  delay(1000);
  Tracker.reboot();
}
