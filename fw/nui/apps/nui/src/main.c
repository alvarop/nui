#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "sysinit/sysinit.h"
#include "console/console.h"
#include "os/os.h"
#include "bsp/bsp.h"
#include "hal/hal_gpio.h"
#include "hal/hal_timer.h"
#include "hal/hal_spi.h"

#include "nrfx.h"
#include "nrfx_spim.h"

#define NUI_TASK_PRI         (10)
#define NUI_STACK_SIZE       (256)
struct os_task nui_task;
os_stack_t nui_task_stack[NUI_STACK_SIZE];

typedef struct {
    uint32_t time;
    uint8_t value;
} edge_t;

static nrfx_spim_t spi_dev = NRFX_SPIM_INSTANCE(0);
static uint8_t current_volume = 164;
static bool mute = false;

// Maximum number of pulse edges to store
#define MAX_EDGES 128

// Timeout (in microseconds) before processing incoming data
#define TIMEOUT_US 11000

static edge_t edges[MAX_EDGES];
static uint16_t edge_index;

struct hal_timer ir_timeout_timer;

static struct os_mutex ir_processing_mutex;

static void ir_irq(void *arg) {
    os_error_t err;

    // If we're currently processing a packet, ignore incoming data
    // Not an issue now, but maybe once BLE is going it could cause problems
    err = os_mutex_pend(&ir_processing_mutex, 0);
    if(err == OS_OK) {
        // Store edge timestamp and current value
        edges[edge_index].time = hal_timer_read(1);
        edges[edge_index].value = hal_gpio_read(IR_IN_PIN);

        edge_index++;

        err = os_mutex_release(&ir_processing_mutex);
        assert(err == OS_OK);

        // Re-set ir timeout
        hal_timer_stop(&ir_timeout_timer);
        hal_timer_start(&ir_timeout_timer, TIMEOUT_US);
    }

}

// NEC Decoding state machine states
typedef enum {
    LEADING_PULSE,
    LEADING_SPACE,
    BIT_PULSE,
    BIT_SPACE,
} decode_state_t;

// Helper functions to determine if a pulse meets certain criteria
static inline bool is_leading_pulse(int32_t duration, uint8_t value) {
    return (value == 1 && duration > 8000 && duration < 10000);
}

static inline bool is_leading_space(int32_t duration, uint8_t value) {
    return (value == 0 && duration > 3500 && duration < 5500);
}

static inline bool is_bit_pulse(int32_t duration, uint8_t value) {
    return (value == 1 && duration > 400 && duration < 700);
}

static inline bool is_bit_space_0(int32_t duration, uint8_t value) {
    return (value == 0 && duration > 400 && duration < 700);
}

static inline bool is_bit_space_1(int32_t duration, uint8_t value) {
    return (value == 0 && duration > 1400 && duration < 1900);
}

// State machine to decode "NEC" IR encoded data
// See:
// http://techdocs.altium.com/display/FPGA/NEC+Infrared+Transmission+Protocol
uint16_t decode(uint32_t *result) {
    decode_state_t state = LEADING_PULSE;
    bool decoding = true;
    uint32_t index = 1;

    assert(result != NULL);

    *result = 0;

    while (decoding && index < edge_index) {
        int32_t duration = edges[index].time - edges[index - 1].time;
        uint8_t value = edges[index].value;
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
                    (*result) <<= 1;
                    state = BIT_PULSE;
                    index++;
                } else if(is_bit_space_1(duration, value)) {
                    (*result) <<= 1;
                    (*result) |= 1;
                    state = BIT_PULSE;
                    index++;
                } else {
                    decoding=false;
                }

                break;
            }
        }
    }

    // Return the total number of edges (could be useful for repeat code, etc)
    return edge_index;
}

// These are the IR codes for the "AUX" setting on my particular Toshiba remote
typedef enum {
  AUX_VOL_DOWN = 0x4BB6C03F,
  AUX_VOL_UP = 0x4BB640BF,
  AUX_MUTE = 0x4BB6A05F
} commands_t;

void set_volume(uint8_t volume) {
    uint8_t vol[2] = {volume, volume};
    nrfx_spim_xfer_desc_t xfer = NRFX_SPIM_XFER_TX(&vol, sizeof(vol));
    hal_gpio_write(CS_N_PIN, 0);
    // hal_spi_txrx(0, vol, NULL, sizeof(vol));
    nrfx_spim_xfer(&spi_dev, &xfer, 0);
    hal_gpio_write(CS_N_PIN, 1);

    console_printf("volume %d\n", volume);
}

// Do something with the received data
static void process_ir_command(uint32_t command, uint16_t num_edges) {
    switch (command) {
        case AUX_VOL_DOWN: {
            console_printf("Volume down!\n");
            if (current_volume > 0) {
                current_volume--;
            }
            set_volume(current_volume);
            break;
        }
        case AUX_VOL_UP: {
            console_printf("Volume up!\n");
            if (current_volume < 255) {
                current_volume++;
            }
            set_volume(current_volume);
            break;
        }
        case AUX_MUTE: {
            console_printf("Mute!\n");
            mute = !mute;
            if(mute) {
                set_volume(0);
            } else {
                set_volume(current_volume);
            }
            break;
        }
        default: {
            // Do something with other codes?
            // console_printf("%08lX - %d\n", command, num_edges);
        }
    }
}

// Called to process data after not receiving IR edges for <TIMEOUT_US>
static void ir_timeout_cb(void *arg) {
    uint32_t result;
    uint16_t num_edges;
    os_error_t err;

    // Make sure we don't process more IR edges while we decode the packet
    err = os_mutex_pend(&ir_processing_mutex, 0xFFFFFFFF);
    assert(err == OS_OK);

    num_edges = decode(&result);

    // Reset edge index so we can receive the next packet!
    edge_index = 0;

    // Let the IR irq do it's thing again
    err = os_mutex_release(&ir_processing_mutex);
    assert(err == OS_OK);

    // Do something with the received data!
    process_ir_command(result, num_edges);
}

void nui_task_func(void *arg) {

    os_error_t err;

    // Use this mutex to make sure IR data isn't corrupted mid-decoding
    err = os_mutex_init(&ir_processing_mutex);
    assert(err == OS_OK);

    hal_gpio_irq_init(IR_IN_PIN, ir_irq, NULL,
        HAL_GPIO_TRIG_BOTH, HAL_GPIO_PULL_NONE);

    // Variable used to keep track of current pulse edge
    edge_index = 0;

    // 1MHz timer to measure incoming pulse edge times
    hal_timer_config(1, 1000000);

    // Timeout used to process a packet after some time without edges
    hal_timer_set_cb(1, &ir_timeout_timer, ir_timeout_cb, NULL);

    hal_gpio_irq_enable(IR_IN_PIN);

    hal_gpio_init_out(MUTE_N_PIN, 1);
    hal_gpio_init_out(SCK_PIN, 0);
    hal_gpio_init_out(MOSI_PIN, 0);
    hal_gpio_init_out(CS_N_PIN, 1);
    hal_gpio_init_out(ZC_EN_PIN, 1);

    nrfx_spim_config_t spi_config = NRFX_SPIM_DEFAULT_CONFIG;

    spi_config.sck_pin = SCK_PIN;
    spi_config.mosi_pin = MOSI_PIN;
    spi_config.miso_pin = MISO_PIN;

    // Workaround for NRFX_SPIM library bug.
    // If NRFX_SPIM_PIN_NOT_USED is used, the library takes it as 'pin 0'
    // and continues using it as a SS pin
    // Pin 2 is not connected on the 32-pin package
    spi_config.ss_pin = 2;

    uint32_t rval = nrfx_spim_init(&spi_dev, &spi_config, NULL, NULL);
    if (rval != NRFX_SUCCESS) {
        console_printf("spim_init error %08lX\n", rval);
    } else {
        console_printf("spim_init successfull\n");
    }

    // Set initial volume
    set_volume(current_volume);

    while (1) {
        os_time_delay(OS_TICKS_PER_SEC*100);
    }
}

int main(int argc, char **argv) {
    int rc;

    sysinit();

    os_task_init(
        &nui_task,
        "nui_task",
        nui_task_func,
        NULL,
        NUI_TASK_PRI,
        OS_WAIT_FOREVER,
        nui_task_stack,
        NUI_STACK_SIZE);

    while (1) {
        os_eventq_run(os_eventq_dflt_get());
    }
    assert(0);
    return rc;
}
