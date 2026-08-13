#ifndef CHARDISP_H
#define CHARDISP_H

#include <stdint.h> 
#include <stdbool.h> 

void init_chardisp_pins(void);
void cd_init(void);
void cd_display1(const char *string);
void cd_display2(const char *string);
void cd_display3(const char *string);
void cd_display4(const char *string);
void cd_display2_ex(const char *str, uint16_t color);

// 控制風扇 (左側)
void cd_update_fan(bool on);

// ★ 新增：控制水滴顯示 (右側)
void cd_update_water(bool on);

// 設定是否啟用「白底模式」
void cd_set_white_mode(bool enabled);

#endif