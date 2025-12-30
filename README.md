# ptSolarHF
The ptSolar is the newest version of Project: Traveler's series of APRS trackers. The ptSolar has been designed and optimized for low-power operation, especially when using a solar panel.

![3D rendering of the ptSolar circuit board.](/assets/pcb-board-rendering.png)

Under the hood, the ptSolar runs on the same __Microchip ATMega328p__ microcontroller that the other trackers have been using for years, although this board runs at a lower 3.3 volt level, and a slower 8MHz clock speed.

A 2m VHF transmitter is provided by the __SA818S-V__ transmitter-on-a-chip system, which means it's frequency agile and can be used throughout the world.

The GPS receiver is utilizes the __ATGM336H__ GPS engine which has a light-weight footprint while being a cost-effective solution. The ATGM336H is capable of position reports in excess of 60,000', making it a suitable solution for high altitude (latex) balloons traveling to over 100,000'.

![ptSolar schematic diagram.](/assets/schematic.png)

## The Circuit Board
The circuit board is designed in Kicad and features a 4-layer design with castellated solder points for the attached solar panels.

The ptSolar is currently in the prototyping stages. Version 1.1.0 of the PCB was the initial design that was first built in early 2025, and includes the ATMega328p microcontroller, an ATGM336H GPS receiver, and the SA818S-V VHF transmitter. VHF signal filtering is provided by a 3-pole pi filter, reducing spurious harmonic output to well within Part 97 specifications.

## The Breakout Board
A Breakout board has been developed that can be temporarily or permanently paired to the top of the ptSolar tracker. The breakout includes a second ISCP programming port, a second serial data interface for configuring the tracker, a Sparkfun Qwiic connector for interfacing with other I2C sensors.

There is also a 2.1mm barrel jack for accepting power from a battery pack, along with the required 5V regulator when operating at higher supplied voltages. 

The Breakout Board mates to the tracker with a female .1" header connecting into the 6-pin (ISCP) and 8-pin (Serial, I2C) plugs on the ptSolar.

## Preparing the IDE for the Firmware
In the Arduino IDE and go to File &rarr; Preferences.  In the Additional Boards Manager URLs field, add the URL to the Project: Traveler website .json file:

```
https://www.projecttraveler.org/downloads/package_projecttraveler_index.json
```

Note that if there's already something in that field, you can click the small icon to the right of the text field where you are allowed to enter multiple entries on multiple lines.

![Setting the custom download URL for the Boards Manager.](/assets/arduino-boards-manager-url.png)

Click OK to close the preferences dialog.  Then go to Tools &rarr; Boards &rarr; Boards Manager.

When the Boards Manager opens, select "Contributed" from the Type drop-down.  In the resulting list, you should find the Project Traveler Boards by Project Traveler option.  Click the Install button in the lower-right corner.

![Using the Boards Manager in Arduino IDE.](/assets/arduino-boards-manager2.png)

Close out of the Boards Manager dialog, and the back under Tools &rarr; Board, you should see "ptSolar Tracker (Arduino as ISP)" near the bottom of the list. 

## Setting the Controller Flags
If this is a brand new ptSolar board that has never been programmed before, there are several commands that must be run from a DOS command prompt. If the ptSolar was purchased as an assembled and tested kit, you can skip these steps.

The fuses are to be set as follows:
* Low: 0xFF
* High: 0xD6
* Extended: 0xFD

To program the fuses using the AVRDude tool (which is included in the Arduino IDE, and works with the Arduino As ISP programmer), open a command prompt on the PC.

```
C:\> cd \Program Files (x86)\Arduino\hardware\tools\avr\bin
C:\...> avrdude.exe -p m328p -b 19200 -c avrisp -C ..\etc\avrdude.conf -P comX* -U lfuse:w:0xDF:m -U hfuse:w:0xD6:m -U efuse:w:0xFD:m
```
Depending on your installation of the Arduino IDE, your avrdude may also be found in:

```
C:\Users\{Username}\AppData\Local\Arduino15\packages\arduino\tools\avrdude\6.3.0-arduino17\bin
```

This will write the fuses to the new processor.  To verify that the fuses have been written, run these commands:

```
avrdude.exe -p m328p -b 19200 -c avrisp -C ..\etc\avrdude.conf -P comX* -U lfuse:w:0xFF:m -U hfuse:w:0xD6:m -U efuse:w:0xFD:m 
```

\* Replace "comX" with whatever Com port your ArduinoISP is enumerating on.


### Verifying the Fuses
```
C:\...> avrdude.exe -p m328p -b 19200 -c avrisp -C ..\etc\avrdude.conf -P comX* -U lfuse:r:lowfuse:h -U hfuse:r:highfuse:h -U efuse:r:exfuse:h
C:\...> type lowfuse
C:\...> type highfuse
C:\...> type exfuse
```

\* Replace "comX" with whatever Com port your ArduinoISP is enumerating on.

## Loading the Firmware
To program the ptSolar, open the firmware Sketch in this GitHub Repository in the Arduino IDE. 

Go to the Tools &rarr; Board menu, select the "ptSolar Tracker (Arduino as ISP)" from the list of targets.  

The ptSolar boards are automatically configured to use the Arduino as ISP programmer to load the firmware onto the tracker boards, so no further configuration is necessary. Click the Upload button which will cause the Sketch to compile and will program to the ATmega328 processor similarly to how a normal Arduino board is programmed.

Sometimes it is necessary to apply external power to the ptSolar Tracker, depending on how much current the Programmer board is capable of sourcing. 