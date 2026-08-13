#include <string.h>
#include <math.h>         
#include <stdlib.h>       
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "chardisp.h"

extern const int SPI_DISP_SCK;  
extern const int SPI_DISP_CSn;  
extern const int SPI_DISP_TX;   
extern const int TFT_DC;        
extern const int TFT_RST;       
extern const int TFT_BL;        

#define ILI9341_SWRESET 0x01
#define ILI9341_SLPOUT  0x11
#define ILI9341_DISPOFF 0x28
#define ILI9341_DISPON  0x29
#define ILI9341_CASET   0x2A
#define ILI9341_PASET   0x2B
#define ILI9341_RAMWR   0x2C
#define ILI9341_MADCTL  0x36
#define ILI9341_PIXFMT  0x3A

#define RGB565(r,g,b) ((uint16_t)((((uint16_t)((r)&0xF8))<<8)|(((uint16_t)((g)&0xFC))<<3)|((uint16_t)((b)>>3))))

static spi_inst_t *cd_spi = spi1;

enum { TFT_W = 240, TFT_H = 320 };

// 顏色定義
static const uint16_t COLOR_BLACK     = RGB565(0x00,0x00,0x00); 
static const uint16_t COLOR_WHITE     = RGB565(0xFF,0xFF,0xFF);

static const uint16_t COLOR_TIME      = RGB565(0x00,0xFF,0x00); // 綠
static const uint16_t COLOR_MEASURE   = RGB565(0x00,0xCC,0xFF); // 藍綠
static const uint16_t COLOR_THRESHOLD = RGB565(0xFF,0xC0,0x40); // 橘黃
static const uint16_t COLOR_INFO      = RGB565(0xFF,0xFF,0xFF); // 白
static const uint16_t COLOR_FAN       = RGB565(0x00,0xFF,0xFF); // 青色 (風扇)
static const uint16_t COLOR_WATER     = RGB565(0x00,0x00,0xFF); // 藍色 (水)

enum { FONT_W = 5, FONT_H = 8, SCALE = 2, CELL_W = (FONT_W+1)*SCALE, CELL_H = FONT_H*SCALE };
enum { COLS = 16 };

static const int LINE1_Y = 16;                       
static const int LINE2_Y = LINE1_Y + CELL_H + 8;     
static const int LINE3_Y = LINE2_Y + CELL_H + 8;     
static const int LINE4_Y = LINE3_Y + CELL_H + 8; 

static const int TEXT_X0 = (TFT_W - COLS*CELL_W)/2;

// 字型定義
static const uint8_t font5x8[96][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x3E,0x41,0x5D,0x59,0x4E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
  {0x00,0x01,0x02,0x00,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
  {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
  {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
  {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x14,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
  {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
  {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x02,0x01,0x02,0x04,0x02},{0x00,0x00,0x00,0x00,0x00}
};

static inline void tft_cs_low(void)  { gpio_put(SPI_DISP_CSn, 0); }
static inline void tft_cs_high(void) { gpio_put(SPI_DISP_CSn, 1); }
static inline void tft_dc_cmd(void)  { gpio_put(TFT_DC, 0); }
static inline void tft_dc_data(void) { gpio_put(TFT_DC, 1); }

static inline void tft_write_cmd(uint8_t c){
    tft_dc_cmd(); tft_cs_low();
    spi_write_blocking(cd_spi, &c, 1);
    tft_cs_high();
}

static inline void tft_write_data(const uint8_t *d, size_t n){
    tft_dc_data(); tft_cs_low();
    spi_write_blocking(cd_spi, d, n);
    tft_cs_high();
}

static void tft_write_u16_repeat(uint16_t color, size_t count){
    uint8_t hi = (uint8_t)(color >> 8), lo = (uint8_t)(color & 0xFF);
    uint8_t buf[128]; 
    for (int i=0;i<64;i++){ buf[2*i]=hi; buf[2*i+1]=lo; }

    tft_dc_data(); tft_cs_low();
    while (count) {
        size_t chunk = (count > 64) ? 64 : count;
        spi_write_blocking(cd_spi, buf, chunk * 2);
        count -= chunk;
    }
    tft_cs_high();
}

static void tft_set_addr_window(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1){
    uint8_t buf[4];
    tft_write_cmd(ILI9341_CASET);
    buf[0]=x0>>8; buf[1]=x0&0xFF; buf[2]=x1>>8; buf[3]=x1&0xFF;
    tft_write_data(buf,4);
    tft_write_cmd(ILI9341_PASET);
    buf[0]=y0>>8; buf[1]=y0&0xFF; buf[2]=y1>>8; buf[3]=y1&0xFF;
    tft_write_data(buf,4);
    tft_write_cmd(ILI9341_RAMWR);
}

static void tft_fill_rect(int x,int y,int w,int h,uint16_t color){
    if(x<0){w+=x; x=0;} if(y<0){h+=y; y=0;}
    if(x>=TFT_W || y>=TFT_H || w<=0 || h<=0) return;
    if(x+w-1>=TFT_W) w=TFT_W-x;
    if(y+h-1>=TFT_H) h=TFT_H-y;

    tft_set_addr_window((uint16_t)x,(uint16_t)y,(uint16_t)(x+w-1),(uint16_t)(y+h-1));
    tft_write_u16_repeat(color, (size_t)w*h);
}

static void tft_draw_pixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= TFT_W || y < 0 || y >= TFT_H) return;
    tft_set_addr_window(x, y, x, y);
    uint8_t data[2] = { (uint8_t)(color >> 8), (uint8_t)(color & 0xFF) };
    tft_write_data(data, 2);
}

static void tft_draw_line(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (1) {
        tft_draw_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void init_chardisp_pins(void){
    gpio_set_function(SPI_DISP_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SPI_DISP_TX,  GPIO_FUNC_SPI);
    gpio_init(SPI_DISP_CSn); gpio_set_dir(SPI_DISP_CSn, GPIO_OUT); gpio_put(SPI_DISP_CSn, 1);
    gpio_init(TFT_DC);       gpio_set_dir(TFT_DC,       GPIO_OUT); gpio_put(TFT_DC, 1);
    gpio_init(TFT_RST);      gpio_set_dir(TFT_RST,      GPIO_OUT); gpio_put(TFT_RST, 1);

    spi_init(cd_spi, 20 * 1000 * 1000); 
    spi_set_format(cd_spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

static void tft_hw_reset(void){
    gpio_put(TFT_RST, 0); sleep_ms(20);
    gpio_put(TFT_RST, 1); sleep_ms(120);
}

static void tft_basic_init(void){
    tft_write_cmd(ILI9341_SWRESET); sleep_ms(5);
    tft_write_cmd(ILI9341_DISPOFF);
    uint8_t pix = 0x55;
    tft_write_cmd(ILI9341_PIXFMT); tft_write_data(&pix,1);
    uint8_t madctl = 0x48;
    tft_write_cmd(ILI9341_MADCTL); tft_write_data(&madctl,1);
    tft_write_cmd(ILI9341_SLPOUT); sleep_ms(120);
    tft_write_cmd(ILI9341_DISPON); sleep_ms(10);
}

static void draw_char2x(int x,int y,char c,uint16_t fg,uint16_t bg){
    if(c < 0x20 || c > 0x7F) c = ' ';
    const uint8_t *col = font5x8[c-0x20];
    for(int cx=0; cx<FONT_W; cx++){
        uint8_t bits = col[cx];
        for(int dx=0; dx<SCALE; dx++){
            int px = x + (cx*SCALE + dx);
            int run_h = FONT_H * SCALE;
            tft_set_addr_window((uint16_t)px, (uint16_t)y, (uint16_t)px, (uint16_t)(y + run_h - 1));
            tft_dc_data(); tft_cs_low();
            for(int ry=0; ry<FONT_H; ry++){
                uint16_t color = (bits & (1<<ry)) ? fg : bg;
                uint8_t hi = (uint8_t)(color>>8), lo = (uint8_t)(color & 0xFF);
                uint8_t p[2] = {hi, lo};
                spi_write_blocking(cd_spi, p, 2);
                spi_write_blocking(cd_spi, p, 2);
            }
            tft_cs_high();
        }
    }
    tft_fill_rect(x + FONT_W*SCALE, y, SCALE, FONT_H*SCALE, bg);
}

static void draw_text_line_color(int x0, int y0, const char *s, uint16_t fg, uint16_t bg) {
    tft_fill_rect(x0, y0, COLS * CELL_W, CELL_H, bg);
    for (int i = 0; i < COLS; i++) {
        char c = (s && s[i]) ? s[i] : ' ';
        draw_char2x(x0 + i * CELL_W, y0, c, fg, bg);
    }
}

// 白底模式狀態
static bool g_white_mode = false; 

void cd_init(void){
    tft_hw_reset();
    tft_basic_init();
    tft_fill_rect(0, 0, TFT_W, TFT_H, COLOR_BLACK);

    draw_text_line_color(TEXT_X0, LINE1_Y, "", COLOR_TIME,      COLOR_BLACK);
    draw_text_line_color(TEXT_X0, LINE2_Y, "", COLOR_MEASURE,   COLOR_BLACK);
    draw_text_line_color(TEXT_X0, LINE3_Y, "", COLOR_THRESHOLD, COLOR_BLACK);
    draw_text_line_color(TEXT_X0, LINE4_Y, "", COLOR_INFO,      COLOR_BLACK);
}

// 設定白底模式
void cd_set_white_mode(bool enabled) {
    if (g_white_mode == enabled) return;
    g_white_mode = enabled;

    uint16_t bg = g_white_mode ? COLOR_WHITE : COLOR_BLACK;
    tft_fill_rect(0, 0, TFT_W, TFT_H, bg);
}

// 顯示函式
void cd_display1(const char *str){ 
    uint16_t fg = g_white_mode ? COLOR_BLACK : COLOR_TIME;
    uint16_t bg = g_white_mode ? COLOR_WHITE : COLOR_BLACK;
    draw_text_line_color(TEXT_X0, LINE1_Y, str, fg, bg); 
}
void cd_display2(const char *str){ 
    uint16_t fg = g_white_mode ? COLOR_BLACK : COLOR_MEASURE;
    uint16_t bg = g_white_mode ? COLOR_WHITE : COLOR_BLACK;
    draw_text_line_color(TEXT_X0, LINE2_Y, str, fg, bg); 
}
void cd_display3(const char *str){ 
    uint16_t fg = g_white_mode ? COLOR_BLACK : COLOR_THRESHOLD;
    uint16_t bg = g_white_mode ? COLOR_WHITE : COLOR_BLACK;
    draw_text_line_color(TEXT_X0, LINE3_Y, str, fg, bg); 
}
void cd_display4(const char *str){ 
    uint16_t fg = g_white_mode ? COLOR_BLACK : COLOR_INFO;
    uint16_t bg = g_white_mode ? COLOR_WHITE : COLOR_BLACK;
    draw_text_line_color(TEXT_X0, LINE4_Y, str, fg, bg); 
}
void cd_display2_ex(const char *str, uint16_t color) { 
    uint16_t fg = color;
    if (g_white_mode) {
        if (color == 0xF800) fg = 0xF800; // 紅色保留
        else fg = COLOR_BLACK;            
    }
    uint16_t bg = g_white_mode ? COLOR_WHITE : COLOR_BLACK;
    draw_text_line_color(TEXT_X0, LINE2_Y, str, fg, bg); 
}

// 風扇動畫
static int fan_angle = 0;      
static bool fan_was_on = false; 

// ★ 修改：風扇位置移到左邊 (x=60)
void cd_update_fan(bool on) {
    const int cx = 60;  // 左半邊
    const int cy = 240;        
    const int r = 40;          

    uint16_t bg_color = g_white_mode ? COLOR_WHITE : COLOR_BLACK;

    if (!on) {
        if (fan_was_on) {
            tft_fill_rect(cx - r - 2, cy - r - 2, 2 * r + 4, 2 * r + 4, bg_color);
            fan_was_on = false;
        }
        return;
    }

    tft_fill_rect(cx - r - 2, cy - r - 2, 2 * r + 4, 2 * r + 4, bg_color);
    fan_angle = (fan_angle + 30) % 360; 
    fan_was_on = true;
    uint16_t fan_color = g_white_mode ? 0x001F : COLOR_FAN; 

    for (int i = 0; i < 3; i++) {
        int current_deg = fan_angle + i * 120; 
        double rad = current_deg * 3.14159 / 180.0;
        
        int x_tip = cx + (int)(r * cos(rad));
        int y_tip = cy + (int)(r * sin(rad));

        tft_draw_line(cx, cy, x_tip, y_tip, fan_color);
    }
    
    uint16_t info_color = g_white_mode ? COLOR_BLACK : COLOR_INFO;
    tft_fill_rect(cx - 3, cy - 3, 6, 6, info_color);
    tft_draw_pixel(cx-r, cy, info_color);
    tft_draw_pixel(cx+r, cy, info_color);
    tft_draw_pixel(cx, cy-r, info_color);
    tft_draw_pixel(cx, cy+r, info_color);
}

// ★ 新增：水滴動畫
static bool water_was_on = false;

// 水滴位置在右邊 (x=180)
void cd_update_water(bool on) {
    const int cx = 180; // 右半邊
    const int cy = 240;
    const int r = 30;   // 稍微小一點

    uint16_t bg_color = g_white_mode ? COLOR_WHITE : COLOR_BLACK;

    if (!on) {
        if (water_was_on) {
            // 清除區域
            tft_fill_rect(cx - r - 2, cy - r - 12, 2 * r + 4, 2 * r + 14, bg_color);
            water_was_on = false;
        }
        return;
    }
    
    // 繪製水滴：簡單用幾條線勾勒
    // 圓形底部 + 三角形頂部
    
    // 如果想要動畫效果，可以讓顏色閃爍，或者讓水滴上下浮動
    // 這裡做簡單的靜態水滴，每次都重畫也無妨
    
    water_was_on = true;
    uint16_t water_color = g_white_mode ? 0x0010 : COLOR_WATER; // 深藍 或 亮藍

    // 清除舊畫面 (避免顏色殘留)
    tft_fill_rect(cx - r - 2, cy - r - 12, 2 * r + 4, 2 * r + 14, bg_color);

    // 畫底部圓弧 (用多條線模擬)
    int y_center_bottom = cy + 5;
    int r_bottom = 15;
    
    // 畫下半圓
    for (int deg = 0; deg <= 180; deg += 10) {
         double rad = deg * 3.14159 / 180.0;
         int x1 = cx + (int)(r_bottom * cos(rad));
         int y1 = y_center_bottom + (int)(r_bottom * sin(rad));
         int x2 = cx + (int)(r_bottom * cos(rad + 0.17)); // +10度
         int y2 = y_center_bottom + (int)(r_bottom * sin(rad + 0.17));
         tft_draw_line(x1, y1, x2, y2, water_color);
    }
    
    // 畫上半部尖端
    int y_top = cy - 20;
    // 左腰
    tft_draw_line(cx - r_bottom, y_center_bottom, cx, y_top, water_color);
    // 右腰
    tft_draw_line(cx + r_bottom, y_center_bottom, cx, y_top, water_color);
    
    // 填滿中間一點點讓它顯眼 (可選)
    tft_draw_pixel(cx, cy, water_color);
    tft_draw_pixel(cx, cy+2, water_color);
}