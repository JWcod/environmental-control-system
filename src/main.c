#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/timer.h" 
#include "queue.h"          
#include "chardisp.h"       

// ======================================================================
// 腳位定義 (Pin Definitions)
// ======================================================================

const int SPI_DISP_SCK = 14; 
const int SPI_DISP_TX  = 15;  
const int SPI_DISP_CSn = 25;  
const int TFT_DC       = 28;
const int TFT_RST      = 29;
const int TFT_BL       = 30; 

const int  SEG7_DMA_CHANNEL = 0;

#define I2C_PORT      i2c1
#define PIN_I2C_SDA   10
#define PIN_I2C_SCL   11
#define AHT20_ADDR    0x38

#define LDR_ADC_GPIO      26  
#define LDR_ADC_CHANNEL   0   

const int PIN_RGB_R = 20; 
const int PIN_RGB_G = 21; 
const int PIN_RGB_B = 22; 

#define ADC_MAX_VALUE     4095
#define PWM_WRAP          4095
#define FILTER_SHIFT      3   

// ======================================================================
// 全域變數
// ======================================================================

const char* username = "wang7380";

volatile int g_hour = 0;
volatile int g_min  = 0;
volatile int g_sec  = 0;

// 門檻值
int g_temp_threshold_c   = 28;   
int g_hum_threshold_pct  = 80;   
int g_light_threshold_lv = 800;  // 用於判斷「太暗」並顯示文字/白底的門檻

int g_hum_low_threshold_pct = 40; 

// 即時數值
float g_current_temp = 0.0f;
float g_current_hum  = 0.0f;
int   g_current_light_raw = 0;

// ======================================================================
// AHT20
// ======================================================================
static bool aht20_write(const uint8_t *buf, size_t n) {
    return i2c_write_blocking(I2C_PORT, AHT20_ADDR, buf, n, false) == (int)n;
}
static bool aht20_read(uint8_t *buf, size_t n) {
    return i2c_read_blocking(I2C_PORT, AHT20_ADDR, buf, n, false) == (int)n;
}
static bool aht20_init_sensor(void) {
    uint8_t cmd[3] = {0xBE, 0x08, 0x00};
    if (!aht20_write(cmd, 3)) return false;
    sleep_ms(20);
    return true;
}
static bool aht20_measure(float *t, float *h) {
    uint8_t trig[3] = {0xAC, 0x33, 0x00};
    if (!aht20_write(trig, 3)) return false;
    sleep_ms(80);
    uint8_t d[6] = {0};
    if (!aht20_read(d, 6)) return false;
    if (d[0] & 0x80) return false; 
    uint32_t raw_h = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
    uint32_t raw_t = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
    *h = (raw_h * 100.0f) / 1048576.0f;
    *t = (raw_t * 200.0f) / 1048576.0f - 50.0f;
    return true;
}

// ======================================================================
// LDR & PWM
// ======================================================================
static void ldr_adc_init(void) {
    adc_init();
    adc_gpio_init(LDR_ADC_GPIO);
    adc_select_input(LDR_ADC_CHANNEL);
}
static uint16_t ldr_read_raw(void) {
    adc_select_input(LDR_ADC_CHANNEL);
    return adc_read();
}

// 螢幕背光
static void backlight_pwm_init(void) {
    gpio_set_function(TFT_BL, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(TFT_BL);
    pwm_set_wrap(slice_num, PWM_WRAP);
    pwm_set_gpio_level(TFT_BL, PWM_WRAP / 2); 
    pwm_set_enabled(slice_num, true);
}
static void update_backlight_brightness(uint16_t raw_light) {
    // 螢幕背光邏輯：環境越暗(數值大) -> 螢幕越暗 (避免刺眼)
    uint32_t duty;
    if (raw_light >= ADC_MAX_VALUE) duty = 0;
    else duty = (ADC_MAX_VALUE - raw_light); 
    duty = duty * PWM_WRAP / ADC_MAX_VALUE;
    if (duty > PWM_WRAP) duty = PWM_WRAP;
    if (duty < 100) duty = 100; 
    pwm_set_gpio_level(TFT_BL, (uint16_t)duty);
}

// RGB LED PWM 初始化
static void rgb_led_pwm_init(void) {
    gpio_set_function(PIN_RGB_R, GPIO_FUNC_PWM);
    gpio_set_function(PIN_RGB_G, GPIO_FUNC_PWM);
    gpio_set_function(PIN_RGB_B, GPIO_FUNC_PWM);

    uint slice_num_2 = pwm_gpio_to_slice_num(PIN_RGB_R);
    uint slice_num_3 = pwm_gpio_to_slice_num(PIN_RGB_B);

    pwm_set_wrap(slice_num_2, PWM_WRAP);
    pwm_set_wrap(slice_num_3, PWM_WRAP);

    // 預設全滅 (共陽極: 4095)
    pwm_set_gpio_level(PIN_RGB_R, PWM_WRAP);
    pwm_set_gpio_level(PIN_RGB_G, PWM_WRAP);
    pwm_set_gpio_level(PIN_RGB_B, PWM_WRAP);

    pwm_set_enabled(slice_num_2, true);
    pwm_set_enabled(slice_num_3, true);
}

// ★ 新增：根據環境亮度自動調整 LED 亮度
// 邏輯：環境越暗 (raw_light 越大) -> LED 越亮 (PWM 值越小)
static void update_rgb_led_auto_brightness(uint16_t raw_light) {
    // 這裡可以設定一個靈敏度倍率，如果您覺得太暗時燈不夠亮，可以把 1 改成 2 或 3
    // 但要注意不要讓數值超過 4095
    const int sensitivity_multiplier = 1; 
    
    uint32_t effective_light = (uint32_t)raw_light * sensitivity_multiplier;
    if (effective_light > ADC_MAX_VALUE) effective_light = ADC_MAX_VALUE;

    // 計算 LED 亮度 (共陽極邏輯)
    // raw_light = 0 (亮) -> level = 4095 (滅)
    // raw_light = 4095 (暗) -> level = 0 (全亮)
    uint16_t level = (uint16_t)(ADC_MAX_VALUE - effective_light);

    // 更新三個燈 (白光)
    pwm_set_gpio_level(PIN_RGB_R, level);
    pwm_set_gpio_level(PIN_RGB_G, level);
    pwm_set_gpio_level(PIN_RGB_B, level);
}

// ======================================================================
// Keypad & System
// ======================================================================
void keypad_init_pins(void);
void keypad_init_timer(void);
uint16_t key_pop(void);

static uint16_t wait_for_key_event(void) {
    while (true) {
        uint16_t evt = key_pop();
        if (evt == 0) {
            sleep_ms(10);
            continue;
        }
        if ((evt & 0x100) != 0) return evt;
    }
}

bool repeating_timer_callback(struct repeating_timer *t) {
    g_sec++;
    if (g_sec >= 60) {
        g_sec = 0; g_min++;
        if (g_min >= 60) {
            g_min = 0; g_hour++;
            if (g_hour >= 24) g_hour = 0;
        }
    }
    return true; 
}

static void set_time_via_keypad(void) {
    char buf[4]; int len = 0;
    cd_display3(""); cd_display4(""); 
    while (true) {
        char line1[17], line2[17];
        snprintf(line1, sizeof(line1), "Set Time HHMM");
        char d[4];
        for(int i=0; i<4; i++) d[i] = (i < len) ? buf[i] : ((i==len)?'_':' ');
        snprintf(line2, sizeof(line2), "Time:%c%c%c%c", d[0], d[1], d[2], d[3]);
        cd_display1(line1); cd_display2(line2);

        uint16_t evt = wait_for_key_event();
        char k = (char)(evt & 0xFF);
        if (k >= '0' && k <= '9') { if (len < 4) buf[len++] = k; }
        else if (k == '*') { if (len > 0) len--; }
        else if (k == '#') { 
            if (len == 4) {
                int hh = (buf[0]-'0')*10 + (buf[1]-'0');
                int mm = (buf[2]-'0')*10 + (buf[3]-'0');
                if (hh < 24 && mm < 60) { g_hour=hh; g_min=mm; g_sec=0; return; } 
                else { cd_display2_ex("Invalid Time!", 0xF800); sleep_ms(1000); len = 0; }
            }
        }
    }
}

static int input_threshold_via_keypad(const char *title, const char *unit, int default_val, int min_val, int max_val) {
    char buf[4]; int len = 0;
    cd_display3(""); cd_display4("D:Cancel #:OK"); 
    while (true) {
        char line1[17], line2[17];
        snprintf(line1, sizeof(line1), "%-16s", title);
        char d[4] = {' ',' ',' ',' '}; 
        if (len == 0) snprintf(line2, sizeof(line2), "Cur:%d %s", default_val, unit);
        else {
            for(int i=0; i<4; i++) d[i] = (i < len) ? buf[i] : ((i==len)?'_':' ');
            snprintf(line2, sizeof(line2), "New:%c%c%c%c %s", d[0], d[1], d[2], d[3], unit);
        }
        cd_display1(line1); cd_display2(line2); 
        uint16_t evt = wait_for_key_event();
        char k = (char)(evt & 0xFF);
        if (k == 'D') return default_val;
        else if (k >= '0' && k <= '9') { if (len < 4) buf[len++] = k; } 
        else if (k == '*') { if (len > 0) len--; }
        else if (k == '#') {
            int value = default_val;
            if (len > 0) {
                value = 0;
                for (int i=0; i<len; i++) value = value*10 + (buf[i]-'0');
            }
            if (value < min_val || value > max_val) {
                cd_display2_ex("Out of Range!", 0xF800); sleep_ms(1000); len = 0; continue;
            }
            return value;
        }
    }
}

static void menu_thresholds(void) {
    cd_display3(""); cd_display4(""); 
    while (true) {
        cd_display1("1:Temp 2:Water%");
        cd_display2("3:Light *:Exit"); 
        uint16_t evt = wait_for_key_event();
        char k = (char)(evt & 0xFF);
        if (k == '*') return;
        if (k == '1') {
             g_temp_threshold_c = input_threshold_via_keypad("Temp Limit", "C", g_temp_threshold_c, 0, 60);
        }
        else if (k == '2') {
             g_hum_low_threshold_pct = input_threshold_via_keypad("Water Trig", "%", g_hum_low_threshold_pct, 0, 100);
        }
        else if (k == '3') {
             g_light_threshold_lv = input_threshold_via_keypad("Light Trig", "", g_light_threshold_lv, 0, 4095);
        }
        cd_display1("1:Temp 2:Water%");
        cd_display2("3:Light *:Exit"); 
    }
}

// ======================================================================
// Main
// ======================================================================
int main(void) {
    stdio_init_all();
    init_chardisp_pins();
    cd_init();
    keypad_init_pins();
    keypad_init_timer();

    i2c_init(I2C_PORT, 100000); 
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_pull_up(PIN_I2C_SCL);
    
    bool aht20_ready = false;
    if (aht20_init_sensor()) aht20_ready = true;

    ldr_adc_init();
    backlight_pwm_init();

    rgb_led_pwm_init();

    set_time_via_keypad();

    struct repeating_timer timer;
    add_repeating_timer_ms(-1000, repeating_timer_callback, NULL, &timer);

    uint32_t filtered_ldr = 0;
    for(int i=0; i<8; i++) filtered_ldr += ldr_read_raw();
    filtered_ldr = (filtered_ldr / 8) << FILTER_SHIFT;

    int loop_counter = 0;
    
    char line1[17], line2[17], line3[17], line4[17];

    while (true) {
        uint16_t raw_ldr = ldr_read_raw();
        filtered_ldr = filtered_ldr - (filtered_ldr >> FILTER_SHIFT) + raw_ldr;
        g_current_light_raw = (uint16_t)(filtered_ldr >> FILTER_SHIFT);
        update_backlight_brightness(g_current_light_raw);

        // 改成 10 循環 (10 * 50ms = 0.5秒) 讀取一次感測器，讓 LED 反應快一點
        if (loop_counter % 10 == 0 && aht20_ready) {
             aht20_measure(&g_current_temp, &g_current_hum);
        }
        
        // --- 狀態判斷 ---
        bool is_hot  = (g_current_temp >= g_temp_threshold_c);        
        bool is_dry  = (g_current_hum < g_hum_low_threshold_pct);     
        // 這裡保留 is_dark 用於 UI 顯示，但 LED 會隨亮度連續變化
        bool is_dark = (g_current_light_raw > g_light_threshold_lv);  

        cd_set_white_mode(is_dark);

        // ★ 修改：LED 自動隨亮度變化 (Continuous Dimming)
        update_rgb_led_auto_brightness(g_current_light_raw);

        // --- 顯示 ---
        int display_t = (int)g_current_temp;
        int display_h = (int)g_current_hum;
        int display_l = g_current_light_raw;
        int h = g_hour, m = g_min, s = g_sec;

        snprintf(line1, sizeof(line1), "    %02d:%02d:%02d    ", h, m, s);
        
        char fan_char = is_hot  ? '*' : ' ';
        // 如果真的很暗 (超過設定值)，顯示 + 號，雖然現在 LED 是連續變化
        char led_char = is_dark ? '+' : ' '; 
        char dry_char = is_dry  ? '!' : ' ';

        snprintf(line2, sizeof(line2), "T%2d H%2d L%4d%c%c%c", display_t, display_h, display_l, fan_char, led_char, dry_char);
        snprintf(line3, sizeof(line3), "Set: %d %d %d", g_temp_threshold_c, g_hum_low_threshold_pct, g_light_threshold_lv);

        const char* f_str = is_hot ? "F:ON" : "    ";
        const char* l_str = is_dark? "L:ON" : "    "; // 顯示文字仍依賴門檻
        const char* w_str = is_dry ? "W:ON" : "    ";
        snprintf(line4, sizeof(line4), "%s %s %s", f_str, l_str, w_str);

        cd_display1(line1);

        uint16_t default_color = 0x07FF; 
        uint16_t warning_color = 0xF800; 
        
        if (is_hot || display_h >= g_hum_threshold_pct) cd_display2_ex(line2, warning_color); 
        else cd_display2_ex(line2, default_color); 

        cd_display3(line3);
        
        if (!is_hot && !is_dark && !is_dry) {
            cd_display4("Press A for Menu");
        } else {
            cd_display4(line4);
        }

        cd_update_fan(is_hot);
        cd_update_water(is_dry); 

        if (kev.head != kev.tail) { 
            uint16_t evt = key_pop();
            if (evt != 0) {
                bool pressed = (evt & 0x100) != 0;
                char k = (char)(evt & 0xFF);
                if (pressed && k == 'A') {
                    menu_thresholds();
                    cd_update_fan(false);
                    cd_update_water(false);
                }
            }
        }

        loop_counter++;
        // 稍微加快更新頻率以獲得更滑順的調光效果
        sleep_ms(50); 
    }
    return 0;
}
