 /*
Project: Traveler ptSolarHF Firmware
Copyright 2011-2025 - Zack Clobes (W0ZC), Custom Digital Services, LLC


This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.  If not, see <http://www.gnu.org/licenses/>.


Before programming for the first time, the ATmega fuses must be set.
 Low:      0xDF
 High:     0xD6
 Extended: 0xFD

*/


#define FIRMWARE_VERSION "1.6.0"
#define CONFIG_PROMPT "\n\n# "
#include "BoardDef.h"   //defines if this is a ptFlex, ptSolar, or ptSolarHF PCB board



#define __PROG_TYPES_COMPAT__
#include <avr/pgmspace.h>
#include <avr/wdt.h>

#include "MemoryFree.h"


#include <si5351.h>
#include "wspr_enc.h"
#include "nhash.h"

#include <int.h>
#include <string.h>

#include "ptConfig.h"
#include "GPS.h"
#include "ptTracker.h"

#include <Wire.h>

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

//How many MS to delay between subsequent packets (as in between GPGGA and GPRMC strings
#define DELAY_MS_BETWEEN_XMITS 1250
#define METERS_TO_FEET 3.2808399

//Debugging options
#define XMIT_MILLIS true
#define WATCHDOG

static const int32_t  SI5351_CORRECTION_PPB = 0;        // e.g. -1200 = -1.2 ppm
static const si5351_drive SI5351_CLK0_DRIVE = SI5351_DRIVE_8MA;

static const uint16_t WSPR_TONE_SPACING_CENTI_HZ = 146; // ≈1.46 Hz
static const uint16_t WSPR_SYMBOL_COUNT           = 162;
static const uint16_t WSPR_TONE_MS                = 683; // 8192/12000 s


Si5351 si5351;
static uint8_t wspr_symbols[WSPR_SYMBOL_COUNT];
ptConfig Config;                                                                        //Configuration object
ptTracker Tracker(PIN_LED, PIN_AUDIO, PIN_ANALOG_BATTERY, Config.getAnnounceMode());    //Object that manages the board-specific functions
GPS GPSParser(PIN_GPS_RX, PIN_GPS_TX, PIN_GPS_EN);                                      //Object that parses the GPS strings


bool bHasBurst;
float fMaxAlt;


/**
 * @brief  The function that runs first before the main loop() function is called indefinitely.
 * @note   This function is called once at startup and is used to initialize the board and set up the hardware.
 */
void setup() {
  Serial.begin(19200);

  wdt_disable();    //disable the watchdog timer by default
  #ifdef WATCHDOG
    wdt_enable(WDTO_8S);    //Enable the Watchdog if configured
  #endif

  wdt_reset();    //reset the watchdog timer (even if we're not using it)
  showVersion();    //show the version of the firmware that we're running


  //Init some variables
  fMaxAlt = 0;
  bHasBurst = false;

  Tracker.annunciate('k');

    wdt_reset();

    Serial.println(F("Initializing Si5351..."));
    si5351.init(SI5351_CRYSTAL_LOAD_10PF, 27000000, 0);
    si5351.set_correction(Config.getCorrection(), SI5351_PLL_INPUT_XO);
    si5351.drive_strength(SI5351_CLK0, SI5351_CLK0_DRIVE);
    si5351.output_enable(SI5351_CLK0, 0);


 
  
  GPSParser.setDebugNEMA(true);    ///TODO: Need to pull this from Configuration
  GPSParser.setDebugLevel(2);    //Get full verbose output from the GPS
}


/**
 * @brief  Main loop for the program.  This is where the main logic of the program is executed.
 * @note   This function will run continuously until the board is powered off or reset.
 */
void loop() {

  float fCurrentAlt, fSpeed, fMaxSpeed;
  unsigned long battMillivolts;
  bool bXmit;             //Flag to indicate whether or not we should transmit this time around
  bool bXmitPermitted;    //Flag that can be set to false to prevent transmission, such as when we're in a country that doesn't allow APRS
  int iSeconds;
  unsigned long msDelay;    //calculate the number of milliseconds to delay
  byte byTemp;
  char szFreq[9];    //The frequency to transmit on


  wdt_reset();
  
  //Check to see if we have a command from the serial port to indicate that we need to enter config mode
  if (Serial.available()) {
    byTemp = Serial.read();
    if (byTemp == '!') {
      doConfigMode();
    }
  }

  //Reboot the system hourly if configured to do so
  if (Config.getRebootHourly()) {
    //Reboot if we've been running for 60 minutes
    if (millis() > 3600000) {
      //we've been running for 60 minutes - reboot the system
      Serial.println(F("60min Reboot"));
      delay(1000);
      Tracker.reboot();
    }
  }

  battMillivolts = (unsigned long)(Tracker.readBatteryVoltage(true) * 1000);  //read the battery voltage and spit it out to the serial port

  //check to see if we have sufficient battery to run the GPS
  if (battMillivolts >= Config.getVoltThreshGPS()) {
    GPSParser.enableGPS(true);    //enable the GPS module if it's not already. If it wasn't enabled, this will also initialize it.

    GPSParser.collectGPSStrings();
    fCurrentAlt = GPSParser.Altitude();        //get the current altitude
    if (fCurrentAlt > fMaxAlt) {
      fMaxAlt = fCurrentAlt;
    } else {
      if (fMaxAlt > 10000 && (fCurrentAlt < (fMaxAlt - 250))) {
        //Check for burst.  The Burst must be at least 10,000m MSL.
        // To sense a burst, the controller must have fallen at least 250m from the max altitude
  
        bHasBurst = true;
      }
    }
  } else {
    //See if the Battery has dropped 100mV below the threshold.  If so, disable the GPS until the battery comes back up
    if (battMillivolts < (Config.getVoltThreshGPS() - 100)) {
      //we don't have enough battery to run the GPS - disable it
      Serial.println(F("Disabling GPS"));
      GPSParser.disableGPS();
    }
    Serial.println(F("Low Batt, no GPS"));
    delay(750);   //wait for about the amount of time that we'd normally spend grabbing a GPS reading
  }

  bXmit = false;    //assume that we won't transmit this time around

 

  
  if (bXmit) {
    buildWSPRSymbols();
    sendWSPR();

    if (!GPSParser.FixQuality() || GPSParser.NumSats() < 4) {
      //we are having GPS fix issues - issue an annunciation
      Tracker.annunciate('l');
    }
  }


}



static void buildWSPRSymbols() {
    char pwr_str[4];

    wdt_reset();
    static uint8_t PWR_dBm = 23;
    itoa(PWR_dBm, pwr_str, 10);

    char GRID[7];
    GPSParser.getGridSquare(GRID);
    uint8_t mtype = wspr_enc(Config.getCallsign(), GRID, pwr_str, wspr_symbols); // fills 162 symbols

}

static void sendWSPR() {
    const uint64_t base_cHz = (uint64_t)Config.getFrequencyTx() * 100ULL;

    Serial.println(F("Xmitting WSPR"));

    si5351.output_enable(SI5351_CLK0, 1);
    digitalWrite(PIN_PTT_OUT, HIGH);

    for (uint16_t i = 0; i < WSPR_SYMBOL_COUNT; i++)
    {
        wdt_reset();
        const uint8_t sym = wspr_symbols[i] & 0x03;
        const uint64_t f_cHz = base_cHz + (uint64_t)sym * (uint64_t)WSPR_TONE_SPACING_CENTI_HZ;
        si5351.set_freq(f_cHz, SI5351_CLK0);
        delay(WSPR_TONE_MS);
    }

    si5351.output_enable(SI5351_CLK0, 0);
    digitalWrite(PIN_PTT_OUT, LOW);
    Serial.println(F("Xmit complete"));
}




/*
void setFrequency(uint32_t freq) {
    uint32_t pllFreq = 648000000;
    uint8_t divider = calcDivider(freq, pllFreq);
    uint32_t dividerFractional = calcFractional(freq, pllFreq, divider);
    Serial.print("Setting Freq: ");
    Serial.println(freq);
    Serial.print("Divider: ");
    Serial.println(divider);
    Serial.print("Fractional: ");
    Serial.println(dividerFractional);
    clockgen.setupMultisynth(0, SI5351_PLL_B, divider, dividerFractional, 1000000);
}

uint8_t calcDivider(uint32_t freq, uint32_t pllFreq) {
  uint8_t divider = pllFreq / freq;

  if (divider < 4) {
    divider = 4;
  } else if (divider > 900) {
    divider = 900;
  }

  return divider;
}

uint32_t calcFractional(uint32_t freq, uint32_t pllFreq, uint8_t divider) {
  //Calculate the fractional part of the divider
  //Fractional = ((PLL Frequency / Desired Frequency) - Integer Part) * 1,000,000
  float fFractional = ((float)pllFreq / (float)freq) - (float)divider;
  fFractional = fFractional * 1000000.0;

  return (uint32_t)fFractional;
}
*/

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
  Serial.print(CONFIG_PROMPT);

  delay(750);
  Tracker.annunciate('c');

  //keep track of how long we can listen to the GPS
  unsigned long ulUntil = millis() + 600000;
  
  while (millis() < ulUntil ) {
    //Endless loop. Only exit is to reboot with the 'Q', or after 10 minutes of inactivity
    wdt_reset();
    if (Serial.available()) {
      byTemp = Serial.read();


      if (byTemp == '!') {
        showVersion();
      }

      
      if (byTemp == 'D' || byTemp == 'd') {
        //used to reset the tracker back to N0CALL defaults
        Serial.println(F("Clear config"));
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
  

        char status;
      }


      if (byTemp == 'l' || byTemp == 'L') {
        //Do a long test of the transmitter (useful for spectrum analysis or burn-in testing)
        Serial.println(F("Test Xmit"));
        Serial.println(F("\n1. - 1.5s"));
        Serial.println(F("2. - 10s"));
        Serial.println(F("3. - 30s"));
        Serial.println(F("4. - 60s"));

        while (!Serial.available()) {
          //Wait for an input
          wdt_reset();
        }
        byTemp = Serial.read();

        if (byTemp >= '1' && byTemp <= '5') {
        //   Aprs.setTxFrequency(Config.getRadioFreqTx());    //set the frequency to transmit on
        //   Aprs.setRxFrequency(Config.getRadioFreqRx());    //set the frequency to receive on
        //   Aprs.setTxDelay(Config.getRadioTxDelay());

          Tracker.annunciate('t');
          
        //   Aprs.PTT(true);   //configures the SA818 as part of the transmit process.
          switch (byTemp) {
          case '1':
            Serial.println(F("1.5s"));
            delay(1500);
            break;
          case '2':
            Serial.println(F("10s"));
            delay(10000);
            break;
          case '3':
            Serial.println(F("30s"));
            delay(30000);
            break;
          case '4':
            Serial.println(F("60s"));
            delay(60000);
            break;
          default:
            Serial.println(F("Unk"));
          }

        //   Aprs.PTT(false);
        }
      }

      if (byTemp == 'P' || byTemp == 'p') {
        //Send a test packet
        Serial.println(F("Test Packet"));

        sendWSPR();
        Tracker.readBatteryVoltage(true);  //read the battery voltage after the transmission
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


      if (byTemp == 'T' || byTemp == 't') {
        //exercise the transmitter
        // Aprs.setTxFrequency(Config.getRadioFreqTx());    //set the frequency to transmit on
        // Aprs.setRxFrequency(Config.getRadioFreqRx());    //set the frequency to receive on
        // Aprs.setTxDelay(Config.getRadioTxDelay());
        // Aprs.sendTestDiagnotics();
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

      Serial.print(CONFIG_PROMPT);
      ulUntil = millis() + 600000;    //reset the timer for the config mode
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
