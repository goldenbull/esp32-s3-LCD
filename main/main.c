#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_ops.h"

/* Waveshare ESP32-S3-LCD-1.47B pin assignments */
#define PIN_LED   38
#define PIN_MOSI  45
#define PIN_SCLK  40
#define PIN_CS    42
#define PIN_DC    41
#define PIN_RST   39
#define PIN_BL    46

/* Display geometry */
#define LCD_W     172
#define LCD_H     320
#define LCD_HOST  SPI2_HOST

/* ST7789 runs as a 240×320 chip; the 172-wide panel sits at column 34 */
#define X_OFFSET  34
#define Y_OFFSET   0

/* ── 5×8 bitmap glyphs: 0-9, A-Z, a-z (62 total) ───────────────────────────
   One byte per row. Bit 7 = leftmost pixel; only the top 5 bits are used.  */
static const uint8_t GLYPHS[36][8] = {
    /* 0 */ {0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70, 0x00},
    /* 1 */ {0x20, 0x60, 0x20, 0x20, 0x20, 0x20, 0x70, 0x00},
    /* 2 */ {0x70, 0x88, 0x08, 0x10, 0x20, 0x40, 0xF8, 0x00},
    /* 3 */ {0x70, 0x88, 0x08, 0x30, 0x08, 0x88, 0x70, 0x00},
    /* 4 */ {0x10, 0x30, 0x50, 0x90, 0xF8, 0x10, 0x10, 0x00},
    /* 5 */ {0xF8, 0x80, 0x80, 0xF0, 0x08, 0x08, 0xF0, 0x00},
    /* 6 */ {0x70, 0x80, 0x80, 0xF0, 0x88, 0x88, 0x70, 0x00},
    /* 7 */ {0xF8, 0x08, 0x10, 0x20, 0x40, 0x40, 0x40, 0x00},
    /* 8 */ {0x70, 0x88, 0x88, 0x70, 0x88, 0x88, 0x70, 0x00},
    /* 9 */ {0x70, 0x88, 0x88, 0x78, 0x08, 0x08, 0x70, 0x00},
    /* a */ {0x00, 0x00, 0x70, 0x08, 0x78, 0x88, 0x78, 0x00},
    /* b */ {0x80, 0x80, 0xF0, 0x88, 0x88, 0x88, 0xF0, 0x00},
    /* c */ {0x00, 0x00, 0x70, 0x80, 0x80, 0x80, 0x70, 0x00},
    /* d */ {0x08, 0x08, 0x78, 0x88, 0x88, 0x88, 0x78, 0x00},
    /* e */ {0x00, 0x00, 0x70, 0x88, 0xF8, 0x80, 0x70, 0x00},
    /* f */ {0x30, 0x40, 0xE0, 0x40, 0x40, 0x40, 0x40, 0x00},
    /* g */ {0x00, 0x70, 0x88, 0x88, 0x78, 0x08, 0x88, 0x70},
    /* h */ {0x80, 0x80, 0xF0, 0x88, 0x88, 0x88, 0x88, 0x00},
    /* i */ {0x20, 0x00, 0x60, 0x20, 0x20, 0x20, 0x70, 0x00},
    /* j */ {0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x90, 0x60},
    /* k */ {0x80, 0x80, 0x90, 0xA0, 0xC0, 0xA0, 0x90, 0x00},
    /* l */ {0x60, 0x20, 0x20, 0x20, 0x20, 0x20, 0x70, 0x00},
    /* m */ {0x00, 0x00, 0xD0, 0xA8, 0xA8, 0x88, 0x88, 0x00},
    /* n */ {0x00, 0x00, 0xF0, 0x88, 0x88, 0x88, 0x88, 0x00},
    /* o */ {0x00, 0x00, 0x70, 0x88, 0x88, 0x88, 0x70, 0x00},
    /* p */ {0x00, 0xF0, 0x88, 0x88, 0xF0, 0x80, 0x80, 0x00},
    /* q */ {0x00, 0x78, 0x88, 0x88, 0x78, 0x08, 0x08, 0x00},
    /* r */ {0x00, 0x00, 0xB0, 0xC0, 0x80, 0x80, 0x80, 0x00},
    /* s */ {0x00, 0x00, 0x70, 0x80, 0x70, 0x08, 0x70, 0x00},
    /* t */ {0x40, 0x40, 0xE0, 0x40, 0x40, 0x40, 0x30, 0x00},
    /* u */ {0x00, 0x00, 0x88, 0x88, 0x88, 0x88, 0x78, 0x00},
    /* v */ {0x00, 0x00, 0x88, 0x88, 0x88, 0x50, 0x20, 0x00},
    /* w */ {0x00, 0x00, 0x88, 0x88, 0xA8, 0xA8, 0x50, 0x00},
    /* x */ {0x00, 0x00, 0x88, 0x50, 0x20, 0x50, 0x88, 0x00},
    /* y */ {0x00, 0x00, 0x88, 0x88, 0x88, 0x78, 0x08, 0x70},
    /* z */ {0x00, 0x00, 0xF8, 0x10, 0x20, 0x40, 0xF8, 0x00},
};
#define N_GLYPHS 36
#define GLYPH_W  5
#define GLYPH_H  8

/* ── Matrix rain ──────────────────────────────────────────────────────────── */
#define RAIN_SCALE  3
#define RAIN_GW     (GLYPH_W * RAIN_SCALE)    /* 15 px per glyph */
#define RAIN_GH     (GLYPH_H * RAIN_SCALE)    /* 24 px per glyph */
#define RAIN_COL_W  10                        /* 10 px pitch — columns overlap */
#define N_STREAMS   (LCD_W / RAIN_COL_W)      /* 17 columns */
#define TRAIL_LEN   10                        /* characters in each tail */
#define CHAR_PERIOD  5                        /* frames between character changes */
#define SPEED_MIN    3                        /* slowest stream (px/frame) */
#define SPEED_MAX    8                        /* fastest stream (px/frame) */

/* TRAIL_COLS[0] = oldest/darkest, TRAIL_COLS[TRAIL_LEN] = head (white) */
static const uint16_t TRAIL_COLS[TRAIL_LEN + 1] = {
    0x0040, 0x0080, 0x00E0, 0x0160, 0x01C0,
    0x0260, 0x0360, 0x04A0, 0x0640, 0x07C0,
    0xFFFF,  /* head = white */
};

static struct {
    int y, speed;
    int ticks[TRAIL_LEN + 1];     /* per-slot frame counter */
    uint8_t chars[TRAIL_LEN + 1]; /* per-slot glyph index */
} streams[N_STREAMS];

static void init_stream_chars(int s)
{
    for (int t = 0; t <= TRAIL_LEN; t++) {
        streams[s].ticks[t] = (int)(esp_random() % CHAR_PERIOD);
        streams[s].chars[t] = (uint8_t)(esp_random() % N_GLYPHS);
    }
}

static void draw_glyph_at(uint16_t *fb, int gx, int gy, int gidx, uint16_t color)
{
    const uint8_t *g = GLYPHS[gidx];
    for (int row = 0; row < GLYPH_H; row++)
        for (int col = 0; col < GLYPH_W; col++) {
            if (!((g[row] >> (7 - col)) & 1)) continue;
            for (int sy = 0; sy < RAIN_SCALE; sy++)
                for (int sx = 0; sx < RAIN_SCALE; sx++) {
                    int px = gx + (GLYPH_W - 1 - col) * RAIN_SCALE + sx;
                    int py = gy + (GLYPH_H - 1 - row) * RAIN_SCALE + sy;
                    if ((unsigned)px < LCD_W && (unsigned)py < LCD_H)
                        fb[py * LCD_W + px] = color;
                }
        }
}

static void rain_loop(uint16_t *fb, esp_lcd_panel_handle_t panel)
{
    for (int s = 0; s < N_STREAMS; s++) {
        streams[s].y     = LCD_H + (int)(esp_random() % (uint32_t)LCD_H);
        streams[s].speed = SPEED_MIN + (int)(esp_random() % (SPEED_MAX - SPEED_MIN + 1));
        init_stream_chars(s);
    }

    while (1) {
        memset(fb, 0, LCD_W * LCD_H * sizeof(uint16_t));

        for (int s = 0; s < N_STREAMS; s++) {
            int x = s * RAIN_COL_W;

            /* Draw tail (oldest = darkest green) below head (white), rising upward */
            for (int t = TRAIL_LEN; t >= 0; t--) {
                if (++streams[s].ticks[t] >= CHAR_PERIOD) {
                    streams[s].ticks[t] = 0;
                    streams[s].chars[t] = (uint8_t)(esp_random() % N_GLYPHS);
                }
                int gy = streams[s].y + t * RAIN_GH;
                if (gy <= -RAIN_GH || gy >= LCD_H) continue;
                draw_glyph_at(fb, x, gy, streams[s].chars[t], TRAIL_COLS[TRAIL_LEN - t]);
            }

            streams[s].y -= streams[s].speed;
            if (streams[s].y < -(TRAIL_LEN * RAIN_GH)) {
                streams[s].y     = LCD_H + RAIN_GH + (int)(esp_random() % (uint32_t)LCD_H);
                streams[s].speed = SPEED_MIN + (int)(esp_random() % (SPEED_MAX - SPEED_MIN + 1));
                init_stream_chars(s);
            }
        }

        esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_W, LCD_H, fb);
    }
}

static void led_task(void *arg)
{
    led_strip_handle_t strip;
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = PIN_LED,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip));
    led_strip_clear(strip);

    uint16_t hue = 0;
    while (1) {
        /* Full saturation, moderate brightness to avoid glare */
        led_strip_set_pixel_hsv(strip, 0, hue, 255, 60);
        led_strip_refresh(strip);
        hue = (hue + 1) % 360;
        vTaskDelay(pdMS_TO_TICKS(20));  /* full rainbow cycle ≈ 7 s */
    }
}

void app_main(void)
{
    gpio_set_direction(PIN_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BL, 0);  /* backlight off during init */

    /* ── SPI bus ─────────────────────────────────────────────────────────── */
    spi_bus_config_t bus = {
        .mosi_io_num     = PIN_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_W * LCD_H * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO));

    /* ── Panel IO (SPI → LCD command/data) ───────────────────────────────── */
    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = PIN_DC,
        .cs_gpio_num       = PIN_CS,
        .pclk_hz           = 40 * 1000 * 1000,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, &io));

    /* ── ST7789 panel ────────────────────────────────────────────────────── */
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_dev_config_t dcfg = {
        .reset_gpio_num = PIN_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        /* Tell ST7789 to expect little-endian data, matching ESP32 SPI output */
        .data_endian    = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &dcfg, &panel));

    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_invert_color(panel, true);   /* required for ST7789 */
    esp_lcd_panel_set_gap(panel, X_OFFSET, Y_OFFSET);
    esp_lcd_panel_disp_on_off(panel, true);

    /* ── Render and push framebuffer ─────────────────────────────────────── */
    size_t fb_sz = LCD_W * LCD_H * sizeof(uint16_t);
    uint16_t *fb = heap_caps_malloc(fb_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!fb) fb = malloc(fb_sz);  /* fall back to internal RAM */
    assert(fb != NULL);

    gpio_set_level(PIN_BL, 1);  /* backlight on */
    xTaskCreate(led_task, "led", 2048, NULL, 5, NULL);
    rain_loop(fb, panel);  /* never returns; fb stays alive */
}
