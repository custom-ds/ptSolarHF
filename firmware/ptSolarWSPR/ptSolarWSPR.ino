/*  WSPR_Si5351_wspr_enc_demo.ino
    Minimal WSPR beacon for ATmega328 (Uno/Nano) + Si5351 using:
      - Etherkit Si5351 library to generate the RF tones
      - wspr_enc.c (with nhash.c) to encode a WSPR message into 162 channel symbols

    References:
      - Etherkit Si5351 example shows centi-Hz use and WSPR spacing = 146 cHz. [ref]
      - WSPR protocol timing: 162 symbols, 1.4648 Hz spacing, 8192/12000 s per symbol. [ref]
*/


#define FIRMWARE_VERSION "1.6.0"
#define CONFIG_PROMPT "\n\n# "
#include "BoardDef.h"   //defines if this is a ptFlex, ptSolar, or ptSolarHF PCB board


#include <avr/wdt.h>

#include <si5351.h>
#include "wspr_enc.h"
#include "nhash.h"

#include <int.h>
#include <string.h>

#include "GPS.h"


#include "Wire.h"





// -------- USER CONFIG --------
static char    CALL[] = "W0ZC";
static char    GRID[] = "EM18";
static uint8_t PWR_dBm = 23;

static const uint32_t WSPR_BASE_FREQ_HZ = 14097200UL;   // adjust for your band/region
static const int32_t  SI5351_CORRECTION_PPB = 0;        // e.g. -1200 = -1.2 ppm
static const si5351_drive SI5351_CLK0_DRIVE = SI5351_DRIVE_8MA;
// ------ END USER CONFIG ------

static const uint16_t WSPR_TONE_SPACING_CENTI_HZ = 146; // ≈1.46 Hz
static const uint16_t WSPR_SYMBOL_COUNT           = 162;
static const uint16_t WSPR_TONE_MS                = 683; // 8192/12000 s


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

Si5351 si5351;
static uint8_t wspr_symbols[WSPR_SYMBOL_COUNT];
GPS GPSParser(PIN_GPS_RX, PIN_GPS_TX, PIN_GPS_EN);                                      //Object that parses the GPS strings


static void build_wspr_symbols() {
  char pwr_str[4];
  itoa(PWR_dBm, pwr_str, 10);
  uint8_t mtype = wspr_enc(CALL, GRID, pwr_str, wspr_symbols); // fills 162 symbols

  // quick blink = message type
  for (uint8_t i = 0; i < mtype; i++) { 
    digitalWrite(PIN_PTT_OUT, HIGH); 
    delay(80);
    digitalWrite(PIN_PTT_OUT, LOW);  
    delay(80); 
    }
}

static void transmit_wspr_once() {
  const uint64_t base_cHz = (uint64_t)WSPR_BASE_FREQ_HZ * 100ULL;

  si5351.output_enable(SI5351_CLK0, 1);
  digitalWrite(PIN_PTT_OUT, HIGH);

  for (uint16_t i = 0; i < WSPR_SYMBOL_COUNT; i++) {
    const uint8_t  sym   = wspr_symbols[i] & 0x03;
    const uint64_t f_cHz = base_cHz + (uint64_t)sym * (uint64_t)WSPR_TONE_SPACING_CENTI_HZ;
    si5351.set_freq(f_cHz, SI5351_CLK0);
    delay(WSPR_TONE_MS);
  }

  si5351.output_enable(SI5351_CLK0, 0);
  digitalWrite(PIN_PTT_OUT, LOW);
}

void setup() {
    wdt_disable();    //disable the watchdog timer by default
    wdt_reset();
    //Shut the GPS down
    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, HIGH);    //shut the GPS back down (active low)


    Serial.begin(19200);
    Serial.println("WSPR Si5351 Transmitter Demo");

  pinMode(PIN_PTT_OUT, OUTPUT); 
  digitalWrite(PIN_PTT_OUT, HIGH);
  delay(100);
  wdt_reset();

  Serial.println("wire begin");
  Wire.begin();
  delay(100);


  Serial.println(F("Initializing Si5351..."));
  delay(100);
  wdt_reset();
  //si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);

  Serial.println("Correction");
  delay(100);
  wdt_reset();
  si5351.set_correction(SI5351_CORRECTION_PPB, SI5351_PLL_INPUT_XO);

  Serial.println("Drive");
  delay(100);
  si5351.drive_strength(SI5351_CLK0, SI5351_CLK0_DRIVE);
  
  Serial.println("Output");
  delay(100);
  si5351.output_enable(SI5351_CLK0, 0);
  

  Serial.println("Done");
    delay(1000);

    Serial.println(F("Building WSPR symbols..."));
    delay(1000);
  build_wspr_symbols();
  Serial.println(F("Got them"));
  delay(1000);
  // Optional: wait for even minute before calling transmit_wspr_once()
  Serial.println("Press p to transmit WSPR once");

    GPSParser.setDebugNEMA(true);    ///TODO: Need to pull this from Configuration
    GPSParser.setDebugLevel(2);    //Get full verbose output from the GPS
}

void loop() {
    byte byTemp;

    Serial.println("Press p to transmit WSPR once");
      if (Serial.available()) {
    byTemp = Serial.read();
    if (byTemp == 'p') {
Serial.println("Transmitting");
      transmit_wspr_once();         // one full WSPR frame (~110.6 s)
  
    }
  }
  delay(1000);
  
}
