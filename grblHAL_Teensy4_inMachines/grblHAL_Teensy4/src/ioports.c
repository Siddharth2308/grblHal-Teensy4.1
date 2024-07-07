/*
  ioports.c - driver code for IMXRT1062 processor (on Teensy 4.1 board)

  Part of grblHAL

  Board by Phil Barrett: https://github.com/phil-barrett/grblHAL-teensy-4.x

  Copyright (c) 2020-2024 Terje Io

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "driver.h"
#include "mcp3221.h"

//#include "Arduino.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "grbl/protocol.h"

static io_ports_data_t digital;
static input_signal_t *aux_in;
static output_signal_t *aux_out;
static ioport_bus_t invert_digital_out;
static volatile uint32_t event_bits;
static volatile bool spin_lock = false;
#if MCP3221_ENABLE
static xbar_t analog_in;
static uint_fast8_t analog_n_in;
static enumerate_pins_ptr on_enumerate_pins;
#endif

static void digital_out (uint8_t port, bool on)
{
    if(port < digital.out.n_ports) {
        port = ioports_map(digital.out, port);
        DIGITAL_OUT((*(aux_out[port].port)), ((settings.ioport.invert_out.mask >> port) & 0x01) ? !on : on);
    }
}

static float digital_out_state (xbar_t *pin)
{
    float value = -1.0f;

    uint8_t port = pin->function - Output_Aux0;
    if(port < digital.out.n_ports)
        value = (float)(DIGITAL_IN((*(aux_out[port].port))) ^ aux_out[port].mode.inverted);

    return value;
}

static float digital_in_state (xbar_t *pin)
{
    float value = -1.0f;

    uint8_t port = pin->function - Input_Aux0;
    if(port < digital.in.n_ports)
        value = (float)(DIGITAL_IN(aux_in[port].gpio) ^ aux_in[port].mode.inverted);

    return value;
}


inline static __attribute__((always_inline)) int32_t get_input (const input_signal_t *input, bool invert, wait_mode_t wait_mode, float timeout)
{
    if(wait_mode == WaitMode_Immediate)
        return DIGITAL_IN(input->gpio) ^ invert;

    int32_t value = -1;
    uint_fast16_t delay = (uint_fast16_t)ceilf((1000.0f / 50.0f) * timeout) + 1;

    if(wait_mode == WaitMode_Rise || wait_mode == WaitMode_Fall) {

        pin_irq_mode_t irq_mode = wait_mode == WaitMode_Rise ? IRQ_Mode_Rising : IRQ_Mode_Falling;

        if(input->cap.irq_mode & irq_mode) {

            event_bits &= ~input->gpio.bit;
            pinEnableIRQ(input, irq_mode);

            do {
                if(event_bits & input->gpio.bit) {
                    value = DIGITAL_IN(input->gpio) ^ invert;
                    break;
                }
                if(delay) {
                    protocol_execute_realtime();
                    hal.delay_ms(50, NULL);
                } else
                    break;
            } while(--delay && !sys.abort);

            pinEnableIRQ(input, IRQ_Mode_None);    // Restore pin interrupt status
        }

    } else {

        bool wait_for = wait_mode != WaitMode_Low;

        do {
            if((DIGITAL_IN(input->gpio) ^ invert) == wait_for) {
                value = DIGITAL_IN(input->gpio) ^ invert;
                break;
            }
            if(delay) {
                protocol_execute_realtime();
                hal.delay_ms(50, NULL);
            } else
                break;
        } while(--delay && !sys.abort);
    }

    return value;
}

void ioports_event (input_signal_t *input)
{
    spin_lock = true;
    event_bits |= input->gpio.bit;

    if(input->interrupt_callback)
        input->interrupt_callback(ioports_map_reverse(&digital.in, input->id - Input_Aux0), !!(input->port->reg->DR & input->port->bit));

    spin_lock = false;
}

static int32_t wait_on_input (io_port_type_t type, uint8_t port, wait_mode_t wait_mode, float timeout)
{
    int32_t value = -1;

    if(type == Port_Digital) {
        if(port < digital.in.n_ports) {
            port = ioports_map(digital.in, port);
            value = get_input(&aux_in[port], (settings.ioport.invert_in.mask >> port) & 0x01, wait_mode, timeout);
        }
    }
#if MCP3221_ENABLE
    else if(port < analog_n_in)
        value = (int32_t)MCP3221_read();
#endif

    return value;
}

static bool register_interrupt_handler (uint8_t port, pin_irq_mode_t irq_mode, ioport_interrupt_callback_ptr interrupt_callback)
{
    bool ok;

    port = ioports_map(digital.in, port);

    if((ok = port < digital.in.n_ports && aux_in[port].cap.irq_mode != IRQ_Mode_None)) {

        input_signal_t *input = &aux_in[port];

        if((ok = (irq_mode & aux_in[port].cap.irq_mode) == irq_mode && interrupt_callback != NULL)) {
            input->mode.irq_mode = irq_mode;
            input->interrupt_callback = interrupt_callback;
            pinEnableIRQ(input, irq_mode);
        }

        if(irq_mode == IRQ_Mode_None || !ok) {
            while(spin_lock);
            pinEnableIRQ(input, IRQ_Mode_None);
            input->mode.irq_mode = IRQ_Mode_None;
            input->interrupt_callback = NULL;
        }
    }

    return ok;
}

static xbar_t *get_pin_info (io_port_type_t type, io_port_direction_t dir, uint8_t port)
{
    static xbar_t pin;
    xbar_t *info = NULL;

    memset(&pin, 0, sizeof(xbar_t));

    if(type == Port_Digital) {

        memset(&pin, 0, sizeof(xbar_t));

        if(dir == Port_Input && port < digital.in.n_ports) {
            port = ioports_map(digital.in, port);
            pin.mode = aux_in[port].mode;
            pin.cap = aux_in[port].cap;
            pin.cap.invert = On;
            pin.cap.claimable = !pin.mode.claimed;
            pin.function = aux_in[port].id;
            pin.group = aux_in[port].group;
            pin.pin = aux_in[port].pin;
            pin.port = (void *)aux_in[port].port;
            pin.description = aux_in[port].description;
            pin.get_value = digital_in_state;
            info = &pin;
        }

        if(dir == Port_Output && port < digital.out.n_ports) {
            port = ioports_map(digital.out, port);
            pin.mode = aux_out[port].mode;
            XBAR_SET_CAP(pin.cap, pin.mode);
            pin.cap.invert = On;
            pin.mode.inverted = (settings.ioport.invert_out.mask >> port) & 0x01;
            pin.function = aux_out[port].id;
            pin.group = aux_out[port].group;
            pin.pin = aux_out[port].pin;
            pin.bit = 1 << aux_out[port].pin;
            pin.port = (void *)aux_out[port].port;
            pin.description = aux_out[port].description;
            pin.get_value = digital_out_state;
            info = &pin;
        }
    }
#if MCP3221_ENABLE
    else if(dir == Port_Input && port == 0)
        info = &analog_in;
#endif

    return info;
}

static void set_pin_description (io_port_type_t type, io_port_direction_t dir, uint8_t port, const char *s)
{
    if(type == Port_Digital) {
        if(dir == Port_Input && port < digital.in.n_ports)
            aux_in[ioports_map(digital.in, port)].description = s;

        if(dir == Port_Output && port < digital.out.n_ports)
            aux_out[ioports_map(digital.out, port)].description = s;
    }
#if AUX_ANALOG
    else {
        if(dir == Port_Output && port < analog.out.n_ports)
            aux_out[ioports_map(analog.out, port)].description = s;
    }
#endif
}

static bool claim (io_port_type_t type, io_port_direction_t dir, uint8_t *port, const char *description)
{
    bool ok = false;

    if(type == Port_Digital) {

        if(dir == Port_Input) {

            if((ok = digital.in.map && *port < digital.in.n_ports && !aux_in[*port].mode.claimed)) {

                uint8_t i;

                hal.port.num_digital_in--;

                for(i = ioports_map_reverse(&digital.in, *port); i < hal.port.num_digital_in ; i++) {
                    digital.in.map[i] = digital.in.map[i + 1];
                    aux_in[digital.in.map[i]].description = iports_get_pnum(digital, i);
                }

                aux_in[*port].mode.claimed = On;
                aux_in[*port].description = description;

                digital.in.map[hal.port.num_digital_in] = *port;
                *port = hal.port.num_digital_in;
            }

        } else if((ok = digital.out.map && *port < digital.out.n_ports && !aux_out[*port].mode.claimed)) {

            uint8_t i;

            hal.port.num_digital_out--;

            for(i = ioports_map_reverse(&digital.out, *port); i < hal.port.num_digital_out; i++) {
                digital.out.map[i] = digital.out.map[i + 1];
                aux_out[digital.out.map[i]].description = iports_get_pnum(digital, i);
            }

            aux_out[*port].mode.claimed = On;
            aux_out[*port].description = description;

            digital.out.map[hal.port.num_digital_out] = *port;
            *port = hal.port.num_digital_out;
        }
    }
#if MCP3221_ENABLE
    else if(dir == Port_Input && (ok = *port == 0 && analog_in.mode.analog && !analog_in.mode.claimed)) {
        hal.port.num_analog_in--;
        analog_in.mode.claimed = On;
        analog_in.description = description;
    }
#endif

    return ok;
}

bool swap_pins (io_port_type_t type, io_port_direction_t dir, uint8_t port_a, uint8_t port_b)
{
    bool ok = port_a == port_b;

    if(!ok && type == Port_Digital) {

        if((ok = dir == Port_Input && port_a < digital.in.n_ports && port_b < digital.in.n_ports &&
                   aux_in[port_a].interrupt_callback == NULL &&
                    aux_in[port_b].interrupt_callback == NULL)) {

            input_signal_t tmp;

            memcpy(&tmp, &aux_in[port_a], sizeof(input_signal_t));
            memcpy(&aux_in[port_a], &aux_in[port_b], sizeof(input_signal_t));
            aux_in[port_a].description = tmp.description;
            tmp.description = aux_in[port_b].description;
            memcpy(&aux_in[port_b], &tmp, sizeof(input_signal_t));
        }

        if((ok = dir == Port_Output && port_a < digital.out.n_ports && port_b < digital.out.n_ports)) {

            output_signal_t tmp;

            memcpy(&tmp, &aux_out[port_a], sizeof(output_signal_t));
            memcpy(&aux_out[port_a], &aux_out[port_b], sizeof(output_signal_t));
            aux_out[port_a].description = tmp.description;
            tmp.description = aux_out[port_b].description;
            memcpy(&aux_out[port_b], &tmp, sizeof(output_signal_t));
        }
    }

    return ok;
}

#if MCP3221_ENABLE

static void enumerate_pins (bool low_level, pin_info_ptr pin_info, void *data)
{
    on_enumerate_pins(low_level, pin_info, data);

    pin_info(&analog_in, data);
}

#endif

static void on_settings_loaded (void)
{
    bool write = false;
    uint_fast8_t port = digital.out.n_ports;

    invert_digital_out = settings.ioport.invert_out;

    if(digital.out.n_ports) do {
        port--;
        pinModeOutput(aux_out[port].port, aux_out[port].pin);
        aux_out[port].mode.inverted = !!(settings.ioport.invert_out.mask & (1 << port));
        DIGITAL_OUT((*(aux_out[port].port)), aux_out[port].mode.inverted);
    } while(port);

    port = digital.in.n_ports;
    do {
        if(aux_in[--port].aux_ctrl &&
            !!(settings.control_invert.mask & aux_in[port].aux_ctrl->cap.mask) !=
             !!(settings.ioport.invert_in.mask & (1 << port))) {
            write = true;
            if(settings.control_invert.mask & aux_in[port].aux_ctrl->cap.mask)
                settings.ioport.invert_in.mask |= (1 << port);
            else
                settings.ioport.invert_in.mask &= ~(1 << port);
        }
        aux_in[port].mode.inverted = !!(settings.ioport.invert_in.mask & (1 << port));
    } while(port);

    if(write)
        settings_write_global();
}

static void on_setting_changed (setting_id_t id)
{
    bool write = false;
    uint_fast8_t port;

    switch(id) {

        case Settings_IoPort_InvertIn:
            port = digital.in.n_ports;
            do {
                port--;
                aux_in[port].mode.inverted = !!(settings.ioport.invert_in.mask & (1 << port));
                if(aux_in[port].aux_ctrl) {
                    write = true;
                    if(aux_in[port].mode.inverted)
                        settings.control_invert.mask |= aux_in[port].aux_ctrl->cap.mask;
                    else
                        settings.control_invert.mask &= ~aux_in[port].aux_ctrl->cap.mask;
                }
            } while(port);
            break;

        case Settings_IoPort_InvertOut:
            if(invert_digital_out.mask != settings.ioport.invert_out.mask) {
                port = digital.out.n_ports;
                do {
                    port--;
                    aux_out[port].mode.inverted = !!(settings.ioport.invert_out.mask & (1 << port));
                    if(((settings.ioport.invert_out.mask >> port) & 0x01) != ((invert_digital_out.mask >> port) & 0x01))
                        DIGITAL_OUT((*(aux_out[port].port)), !DIGITAL_IN((*(aux_out[port].port))));
                } while(port);

                invert_digital_out = settings.ioport.invert_out;
            }
            break;

        case Setting_ControlInvertMask:
            port = digital.in.n_ports;
            do {
                if(aux_in[--port].aux_ctrl) {
                    write = true;
                    if(settings.control_invert.mask & aux_in[port].aux_ctrl->cap.mask)
                        settings.ioport.invert_in.mask |= (1 << port);
                    else
                        settings.ioport.invert_in.mask &= ~(1 << port);
                    aux_in[port].mode.inverted = !!(settings.ioport.invert_in.mask & (1 << port));
                }
            } while(port);
            break;

        default:
            break;
    }

    if(write)
        settings_write_global();
}

void ioports_init (pin_group_pins_t *aux_inputs, pin_group_pins_t *aux_outputs)
{
    aux_in = aux_inputs->pins.inputs;
    aux_out = aux_outputs->pins.outputs;

    hal.port.set_pin_description = set_pin_description;

    if(ioports_add(&digital, Port_Digital, aux_inputs->n_pins, aux_outputs->n_pins))  {

        if(digital.in.n_ports) {
            hal.port.wait_on_input = wait_on_input;
            hal.port.register_interrupt_handler = register_interrupt_handler;
        }

        if(digital.out.n_ports)
            hal.port.digital_out = digital_out;

        hal.port.claim = claim;
        hal.port.swap_pins = swap_pins;
        hal.port.get_pin_info = get_pin_info;

        ioports_add_settings(on_settings_loaded, on_setting_changed);
    }

#if MCP3221_ENABLE

    analog_in.function = Input_Analog_Aux0;
    analog_in.group = PinGroup_AuxInput;
    analog_in.pin = 0;
    analog_in.port = "MCP3221:";

    if(MCP3221_init()) {
        analog_in.mode.analog = On;
        hal.port.num_analog_in = analog_n_in = 1;
    };

    analog_in.description = analog_in.mode.analog ? "E0" : "No power";

    on_enumerate_pins = hal.enumerate_pins;
    hal.enumerate_pins = enumerate_pins;

#endif

}
