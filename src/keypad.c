#include "pico/stdlib.h"
#include <hardware/gpio.h>
#include <stdio.h>
#include "queue.h"

// Global column variable
int col = -1;

// Global key state
static bool state[16]; // Are keys pressed/released

// Keymap for the keypad
const char keymap[17] = "DCBA#9630852*741";

// Defined here to avoid circular dependency issues with autotest
// You can see the struct definition in queue.h
/*KeyEvents kev = { 
    .head = 0, 
    .tail = 0 
};*/

void keypad_drive_column();
void keypad_isr();

/********************************************************* */
// Implement the functions below.

void keypad_init_pins() {
    for (int pin = 2; pin <= 5; ++pin) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_down(pin);
    }
    for (int pin = 6; pin <= 9; ++pin) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, 0);
    }
}

void keypad_init_timer() {
    timer_hw->intr = 0xF;

    hw_set_bits(&timer_hw->inte, (1u << 0) | (1u << 1));

    irq_set_exclusive_handler(TIMER0_IRQ_0, keypad_drive_column);
    irq_set_enabled(TIMER0_IRQ_0, true);
    irq_set_exclusive_handler(TIMER0_IRQ_1, keypad_isr);
    irq_set_enabled(TIMER0_IRQ_1, true);

    uint32_t now = timer_hw->timerawl;
    timer_hw->alarm[0] = now + 1000000u;
    timer_hw->alarm[1] = now + 1100000u;
}

void keypad_drive_column() {
    timer_hw->intr = 1u << 0;

    col = (col + 1) & 3;
    sio_hw->gpio_clr=0xf<<6;
    for (int i = 0; i < 4; i++) {
        int pin = i + 6;
        gpio_put(pin, (i == col) ? 1 : 0);
    }

    timer_hw->alarm[0] = timer_hw->timerawl + 25000u;
}

uint8_t keypad_read_rows() {
    return (uint8_t)((sio_hw->gpio_in >> 2) & 0xF);
}

void keypad_isr() {
    timer_hw->intr = 1u << 1;

    uint8_t rows = keypad_read_rows();

    for (int row = 0; row < 4; row++) {
        int index = col * 4 + row;
        char k = keymap[index];
        bool pressed_now = ((rows >> row) & 1u) != 0;

        if (pressed_now && !state[index]) {
            state[index] = true;
            uint16_t evt = (1u << 8) | (uint8_t)k;
            key_push(evt);
        } else if (!pressed_now && state[index]) {
            state[index] = false;
            uint16_t evt = (uint8_t)k;
            key_push(evt);
        }
    }
    timer_hw->alarm[1] = timer_hw->timerawl + 25000u;
}
