/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "sysinit/sysinit.h"
#include "console/console.h"
#include "os/os.h"
#include "bsp/bsp.h"
#include "hal/hal_gpio.h"
#include "hal/hal_timer.h"

#define IR_PIN (4)

#define BLINK_TASK_PRI         (10)
#define BLINK_STACK_SIZE       (256)
struct os_task blink_task;
os_stack_t blink_task_stack[BLINK_STACK_SIZE];

typedef struct {
    uint32_t time;
    uint8_t value;
} pulse_t;

static pulse_t pulses[256];
static uint16_t pulse_index;

// static bool receiving = false;
static uint8_t previous_val;

struct hal_timer ir_timer;

struct os_callout ir_callout;

static void ir_irq(void *arg) {
    os_callout_reset(&ir_callout, OS_TICKS_PER_SEC/32);
    pulses[pulse_index].time = hal_timer_read(1);
    pulses[pulse_index].value = hal_gpio_read(IR_PIN);
    pulse_index++;
}

typedef enum {
    LEADING_PULSE,
    LEADING_SPACE,
    BIT_PULSE,
    BIT_SPACE,
} decode_state_t;

static inline bool is_leading_pulse(int32_t duration, uint8_t value) {
    return (value == 1 && duration > 8500 && duration < 9500);
}

static inline bool is_leading_space(int32_t duration, uint8_t value) {
    return (value == 0 && duration > 4000 && duration < 5000);
}

static inline bool is_bit_pulse(int32_t duration, uint8_t value) {
    return (value == 1 && duration > 510 && duration < 610);
}

static inline bool is_bit_space_0(int32_t duration, uint8_t value) {
    return (value == 0 && duration > 510 && duration < 610);
}

static inline bool is_bit_space_1(int32_t duration, uint8_t value) {
    return (value == 0 && duration > 1580 && duration < 1780);
}

uint32_t decode() {
    decode_state_t state = LEADING_PULSE;
    bool decoding = true;
    uint32_t index = 1;
    uint32_t result = 0;

    console_printf("Decoding: ");

    while (decoding && index < pulse_index) {
        int32_t duration = pulses[index].time - pulses[index - 1].time;
        uint8_t value = pulses[index].value;
        switch(state) {
            case LEADING_PULSE: {
                if(is_leading_pulse(duration, value)) {
                    state = LEADING_SPACE;
                    index++;
                } else {
                    decoding = false;
                }
                break;
            }

            case LEADING_SPACE: {
                if(is_leading_space(duration, value)) {
                    state = BIT_PULSE;
                    index++;
                } else {
                    decoding = false;
                }
                break;
            }

            case BIT_PULSE: {
                if(is_bit_pulse(duration, value)) {
                    state = BIT_SPACE;
                    index++;
                } else {
                    decoding = false;
                }

                break;
            }

            case BIT_SPACE: {
                if(is_bit_space_0(duration, value)) {
                    result <<= 1;
                    state = BIT_PULSE;
                    index++;
                } else if(is_bit_space_1(duration, value)) {
                    result <<= 1;
                    result |= 1;
                    state = BIT_PULSE;
                    index++;
                } else {
                    decoding=false;
                }

                break;
            }
        }
    }

    return result;
}

typedef enum {
  AUX_VOL_DOWN = 0x4BB6C03F,
  AUX_VOL_UP = 0x4BB640BF,
  AUX_MUTE = 0x4BB6A05F
} commands_t;

void ir_callout_cb(struct os_event *ev) {
    console_printf("pulses: %d\n", pulse_index);
    for(uint16_t index = 1; index < pulse_index; index++) {
        console_printf("%ld [%d]\n",
            pulses[index].time - pulses[index - 1].time, pulses[index].value);
    }
    uint32_t result = decode();

    switch (result) {
        case AUX_VOL_DOWN: {
            console_printf("Volume down!\n");
            break;
        }
        case AUX_VOL_UP: {
            console_printf("Volume up!\n");
            break;
        }
        case AUX_MUTE: {
            console_printf("Mute!\n");
            break;
        }
    }

    pulse_index = 0;
}

void blink_task_func(void *arg) {

    hal_gpio_init_out(LED_BLINK_PIN, 1);

    hal_gpio_irq_init(IR_PIN, ir_irq, NULL,
        HAL_GPIO_TRIG_BOTH, HAL_GPIO_PULL_NONE);

    previous_val = hal_gpio_read(IR_PIN);
    pulse_index = 0;

    os_callout_init(&ir_callout, os_eventq_dflt_get(), ir_callout_cb, NULL);
    hal_timer_config(1, 1000000);

    hal_gpio_irq_enable(IR_PIN);

    while (1) {
        os_time_delay(OS_TICKS_PER_SEC);
        hal_gpio_toggle(LED_BLINK_PIN);
    }
}

int main(int argc, char **argv) {
    int rc;

    sysinit();

    os_task_init(
        &blink_task,
        "blink_task",
        blink_task_func,
        NULL,
        BLINK_TASK_PRI,
        OS_WAIT_FOREVER,
        blink_task_stack,
        BLINK_STACK_SIZE);

    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }
    assert(0);
    return rc;
}
