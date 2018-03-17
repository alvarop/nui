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

#include "sysinit/sysinit.h"
#include "console/console.h"
#include "os/os.h"
#include "bsp/bsp.h"
#include "hal/hal_gpio.h"

#define BLINK_TASK_PRI         (10)
#define BLINK_STACK_SIZE       (256)
struct os_task blink_task;
os_stack_t blink_task_stack[BLINK_STACK_SIZE];

void blink_task_func(void *arg) {

    hal_gpio_init_out(LED_BLINK_PIN, 1);

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
