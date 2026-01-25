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
#include <Adafruit_SI5351.h>

Adafruit_SI5351 clockgen = Adafruit_SI5351();

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
    bXmitPermitted = true;    //assume that we can transmit

    delay(DELAY_MS_BETWEEN_XMITS);    //delay about a second - if you don't you can run into multiple packets inside of a 2 second window

    if (!GPSParser.FixQuality() || GPSParser.NumSats() < 4) {
      //we are having GPS fix issues - issue an annunciation
      Tracker.annunciate('l');
    }
  }


}


/**
 * @brief sendWSPR - This function sends the position of the tracker in a single line format. It includes information such as GPS time, latitude, longitude, course, speed, altitude, and other telemetry data.
 * @param bISSPath A boolean indicating whether or not to use the alternate path for communicating via the ISS space station.
 * @return void
 */
void sendWSPR() {

  char szTemp[15];    //largest string held should be the longitude
  int i;
  
  float fTemp;    //temporary variable

  char statusIAT = 0;
  
  
  //      /155146h3842.00N/09655.55WO301/017/A=058239
  int hh = 0, mm = 0, ss = 0;
  GPSParser.getGPSTime(&hh, &mm, &ss);

    digitalWrite(PIN_PTT_OUT, HIGH);   //key the transmitter
    

    /* Initialise the sensor */
  if (clockgen.begin() != ERROR_NONE)
  {
    /* There was a problem detecting the IC ... check your connections */
    Serial.print("Ooops, no Si5351 detected ... Check your wiring or I2C ADDR!");
    while(1);
  }

  Serial.println("OK!");


  /* FRACTIONAL MODE --> More flexible but introduce clock jitter */
  /* Setup PLLB to fractional mode @616.66667MHz (XTAL * 24 + 2/3) */
  /* Setup Multisynth 1 to 13.55311MHz (PLLB/45.5) */
//   clockgen.setupPLL(SI5351_PLL_A, 33, 0, 1);       //set PLLA to 891MHz for use with Multisynth 0 if needed
//   Serial.println("Set Output #1 to 13.553115MHz");
//   //clockgen.setupMultisynth(1, SI5351_PLL_A, 31, 372135, 1000000);   
//   clockgen.setupMultisynth(1, SI5351_PLL_A, 3503, 633280, 1000000);   
//     clockgen.enableOutputs(true);

    uint32_t freq = 8000;
    uint32_t freqdem = 1000000;
    Serial.println("Set PLLA to 900MHz");
    clockgen.setupPLLInt(SI5351_PLL_A, 36);
    //Serial.println("Set Output #0 to 112.5MHz");
    //clockgen.setupMultisynthInt(0, SI5351_PLL_A, SI5351_MULTISYNTH_DIV_8);

    /* FRACTIONAL MODE --> More flexible but introduce clock jitter */
    /* Setup PLLB to fractional mode @616.66667MHz (XTAL * 24 + 2/3) */
    /* Setup Multisynth 1 to 13.55311MHz (PLLB/45.5) */
    clockgen.setupPLL(SI5351_PLL_B, 24, 0, 3);        //27MHz * (24 + 0/3) = 648MHz
    Serial.println("Set Output #1 to 13.553115MHz");

    

  

  /* Multisynth 2 is not yet used and won't be enabled, but can be */
  /* Use PLLB @ 616.66667MHz, then divide by 900 -> 685.185 KHz */
  /* then divide by 64 for 10.706 KHz */
  /* configured using either PLL in either integer or fractional mode */

//   Serial.println("Set Output #2 to 10.706 KHz");
//   clockgen.setupMultisynth(2, SI5351_PLL_B, 900, 0, 1);
//   clockgen.setupRdiv(2, SI5351_R_DIV_64);

    /* Enable the clocks */
    setFrequency(14231000);
    clockgen.enableOutputs(true);
    wdt_reset();    //reset the watchdog timer
    delay(2000);

    setFrequency(14341000);
    wdt_reset();    //reset the watchdog timer
    delay(2000);

    setFrequency(21320000);
    wdt_reset();    //reset the watchdog timer
    delay(2000);

    setFrequency(28401000);
    wdt_reset();    //reset the watchdog timer
    delay(2000);

    wdt_reset();    //reset the watchdog timer

    clockgen.enableOutputs(false);
    digitalWrite(PIN_PTT_OUT, LOW);    //unkey the transmitter
  

  //Normally seeing about 280mV of drop during the transmission with a 0.5F supercap - Correction: seeing about 800mV with .5F as of 5/16/2025
  Tracker.readBatteryVoltage(true);  //read the battery voltage after the transmission
}

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
