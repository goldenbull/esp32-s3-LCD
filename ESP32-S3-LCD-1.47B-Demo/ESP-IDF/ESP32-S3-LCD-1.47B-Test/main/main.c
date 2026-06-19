/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "ST7789.h"
#include "SD_MMC.h"
#include "RGB.h"
#include "Wireless.h"
#include "LVGL_Example.h"
#include "QMI8658.h"
#include "BAT_Driver.h"

void Driver_Loop(void *parameter)
{
    Wireless_Init();
    while(1)
    {
        QMI8658_Loop();
        BAT_Get_Volts();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}
void app_main(void)
{
    Flash_Searching();
    button_Init();
    BAT_Init();
    I2C_Init();
    QMI8658_Init();
    RGB_Init();
    RGB_Example();
    SD_Init();
    LCD_Init();
    LVGL_Init();   // returns the screen object

/********************* Demo *********************/
    Lvgl_Example1();

    // lv_demo_widgets();
    // lv_demo_keypad_encoder();
    // lv_demo_benchmark();
    // lv_demo_stress();
    // lv_demo_music();

    Simulated_Touch_Init();
    xTaskCreatePinnedToCore(
        Driver_Loop, 
        "Other Driver task",
        4096, 
        NULL, 
        3, 
        NULL, 
        0);
    while (1) {
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        vTaskDelay(pdMS_TO_TICKS(10));
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        lv_timer_handler();
    }
}
