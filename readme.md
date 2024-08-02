# Optimizing GRBL Hal for Teensy 4.1

## Progress
* Tested the setup at a max Feed Rate of 75000 mm/min i.e. 1250 mm/sec. (Can go faster ?)
* In order to acheive, high speeds while printing job, the acceleration needs to me high. Else it won't be able to acheive highest feed rate on execution.
* At high speeds stepper won't change the direction during job execution, solved by tuning the ```driver.c``` file
* Need to find ways to validate the results.

## Structure
* [grblHAL_Teensy4_inMachines](./grblHAL_Teensy4_inMachines/) is the firmware provided my InMachines.
* [original_grbl_hal](./original_grbl_hal/) is the upstream version of grbl-Hal for Teensy 4.X .
* [Teensy_Barebone_Test](./Teensy_Barebone_Test/) is the Hardware testing script.

## Issues
* G-Code Sender can't connect to Teensy 4.1 (Solved)
    * Issue only occurs with inMachines custom firmware.
    * Current grblHal project for Teensy is able to connect.
    * __Solution!__ setting ```USB_SERIAL_CDC``` to 1 one in [my_machine.h](./grblHAL_Teensy4_inMachines/grblHAL_Teensy4/src/my_machine.h)

* Controller won't exit 'Alarm Mode' (Solved)
    * Once connected controller won't exit 'Alarm Mode'
    * Tried disabling, limit switch IOs and "$X" commands.
    * Probable Cause, E-Stop or Limit Switch Pins expect input or the controller tries to achieve "Homing".

    ```
        >>> G10 P0 L20 X0
        An error was detected while sending 'G10 P0 L20 X0': (error:9) G-code commands are locked out during alarm or jog state.
        >>> $X
        An error was detected while sending '$X': (error:79) An unknown error has occurred. Streaming has been paused.
        >>> ?
    ```
    * __Solution!__ grblHAL defaults to normally closed (NC) switches for inputs, if none are connected when testing it is likely that the controller will start in alarm mode.
    Temporarily short the Reset, E-Stop and Safety Door4 inputs to ground or invert the corresponding inputs by setting ```$14=73``` to avoid that.

    * If the issue persists, enable (by ucommenting) the macro ```T4.1_BAREBONE``` in [state_machine.c](./grblHAL_Teensy4_inMachines/grblHAL_Teensy4/src/grbl/state_machine.c)

* Compilation Issue (Solved)- <br>
    * Unable to find Teensy 4.16 version in platform IO registry. <br>
    * __Solution!__ making the following changes to the [platform.ini](./grblHAL_Teensy4_inMachines/grblHAL_Teensy4/platformio.ini) file.
    ```
    [env:teensy41]
    board = teensy41
    platform = https://github.com/platformio/platform-teensy.git#v4.16.0
    upload_protocol = ${common_teensy.upload_protocol}
    build_flags = ${common_teensy.build_flags}
    lib_deps = ${common_teensy.lib_deps}
    ```
* VisiCut, generates empty gcode files (Solved)
    * __Solution!__ Need to create a custom Laser Cutter inside VisiCut.



## Current Setup
* Teensy 4.1 
* TB6600 Stepper Driver
* Nema 17 Stepper Motor
