#ifndef LCD_DRIVER_H_
#define LCD_DRIVER_H_

#include "esp_lcd_io_i80.h"

//LCD data lines
#define DISPLAY_PIN_DATA0 (gpio_num_t) 12
#define DISPLAY_PIN_DATA1 (gpio_num_t) 14
#define DISPLAY_PIN_DATA2 (gpio_num_t) 15
#define DISPLAY_PIN_DATA3 (gpio_num_t) 16
#define DISPLAY_PIN_DATA4 (gpio_num_t) 35
#define DISPLAY_PIN_DATA5 (gpio_num_t) 36
#define DISPLAY_PIN_DATA6 (gpio_num_t) 37
#define DISPLAY_PIN_DATA7 (gpio_num_t) 38

//LCD control pins
#define DISPLAY_PIN_REST (gpio_num_t) 6
#define DISPLAY_PIN_CS (gpio_num_t) 7
#define DISPLAY_PIN_DC (gpio_num_t) 8
#define DISPLAY_PIN_WR (gpio_num_t) 9
#define DISPLAY_PIN_RD (gpio_num_t) 10


#endif