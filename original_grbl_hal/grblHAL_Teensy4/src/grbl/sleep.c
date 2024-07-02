/*
  sleep.c - determines and executes sleep procedures

  Part of grblHAL

  Copyright (c) 2018-2024 Terje Io
  Copyright (c) 2016 Sungeun K. Jeon

  grblHAL is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  grblHAL is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with grblHAL. If not, see <http://www.gnu.org/licenses/>.
*/

#include "hal.h"
#include "task.h"
#include "state_machine.h"

static volatile bool slumber;

static void fall_asleep (void *data)
{
    slumber = false;
}

// Starts sleep timer if running conditions are satisfied. When elapsed, sleep mode is executed.
static void sleep_execute (void)
{
    // Enable sleep timeout
    if(!(slumber = task_add_delayed(fall_asleep, NULL, (uint32_t)(SLEEP_DURATION * 1000.0f * 60.0f))))
        return;

    // Fetch current number of buffered characters in input stream buffer.
    uint16_t rx_initial = hal.stream.get_rx_buffer_free();

    do {
        grbl.on_execute_realtime(state_get());
        // Monitor for any new input stream data or external events (queries, buttons, alarms) to exit.
        if((hal.stream.get_rx_buffer_free() != rx_initial) || sys.rt_exec_state || sys.rt_exec_alarm ) {
            // Disable sleep timeout and return to normal operation.
            slumber = false;
            task_delete(fall_asleep, NULL);
            return;
        }
    } while(slumber);

    // If reached, sleep counter has expired. Execute sleep procedures.
    // Notify user that Grbl has timed out and will be parking.
    // To exit sleep, resume or reset. Either way, the job will not be recoverable.
    system_set_exec_state_flag(EXEC_SLEEP);
}

// Checks running conditions for sleep. If satisfied, enables sleep timeout and executes
// sleep mode upon elapse.
// NOTE: Sleep procedures can be blocking, since grblHAL isn't receiving any commands, nor moving.
// Hence, make sure any valid running state that executes the sleep timer is not one that is moving.
void sleep_check (void)
{
    static uint32_t last_check = 0;

    if(hal.get_elapsed_ticks() - last_check < 50)
        return;

    last_check = hal.get_elapsed_ticks();

    // The sleep execution feature will continue only if the machine is in an IDLE or HOLD state and
    // has any powered components enabled.
    // NOTE: With overrides or in laser mode, modal spindle and coolant state are not guaranteed. Need
    // to directly monitor and record running state during parking to ensure proper function.
    if (!(slumber || sys.steppers_deenergize || sys.flags.auto_reporting) && (gc_state.modal.spindle.state.value || gc_state.modal.coolant.value)) {
        switch(state_get()) {

            case STATE_IDLE:
                sleep_execute();
                break;

            case STATE_HOLD:
                if(sys.holding_state == Hold_Complete)
                    sleep_execute();
                break;

            case STATE_SAFETY_DOOR:
                if(sys.parking_state == Parking_DoorAjar)
                    sleep_execute();
                break;
        }
    }
}
