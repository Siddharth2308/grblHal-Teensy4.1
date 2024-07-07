# Optimizing GRBL Hal for Teensy 4.1

## Structure
* [grblHAL_Teensy4_inMachines](./grblHAL_Teensy4_inMachines/) is the firmware provided my InMachines.
* [original_grbl_hal](./original_grbl_hal/) is the upstream version of grbl-Hal for Teensy 4.X .
* [Teensy_Barebone_Test](./Teensy_Barebone_Test/) is the Hardware testing script.

## Issues
* G-Code Sender cant connect to Teensy 4.1
    * Tried two different g-code senders, same issue.
    * Issue only occurs with inMachines custom firmware.
    * Current grblHal project for Teensy is able to connect.

* Controller won't exit 'Alarm Mode' 
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

* Compilation Issue (Solved)- <br>
    * Unable to find Teensy 4.16 version in platform IO registry. <br>
    * Solved by making the following changes to the 'platformio.ini' file.
    ```
    [env:teensy41]
    board = teensy41
    platform = https://github.com/platformio/platform-teensy.git#v4.16.0
    upload_protocol = ${common_teensy.upload_protocol}
    build_flags = ${common_teensy.build_flags}
    lib_deps = ${common_teensy.lib_deps}
    ```
* VisiCut, generates empty gcode files
    * Switched to 'LaserGrbl' for the time being



## Current Setup
* Teensy 4.1 
* TB6600 Stepper Driver
* Nema 17 Stepper Motor
