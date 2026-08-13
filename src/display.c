#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"

/* 7-segment display message buffer, msg[8]

 Note how we've added in the positions as well to make it 
 more SPI-and-DMA-friendly.  So when we configure DMA, we 
 can just copy this buffer to SPI directly and not make 
 any new modifications.

 DMA messes up when we don't align the buffer to 16 bytes,
 so we use the `__attribute__((aligned(16)))` to ensure
 that the buffer is aligned correctly when DMA accesses it.

 We also removed the `static` keyword since the autotest 
 needs to be able to get the address of this array.  
 (Think about why we might need that...)
 `static` variables are not accessible outside the file 
 they are defined in.

 When we update the characters, we will only update [7:0] bits.
*/
uint16_t __attribute__((aligned(16))) msg[8] = {
    (0 << 8) | 0x3F, // seven-segment value of 0
    (1 << 8) | 0x06, // seven-segment value of 1
    (2 << 8) | 0x5B, // seven-segment value of 2
    (3 << 8) | 0x4F, // seven-segment value of 3
    (4 << 8) | 0x66, // seven-segment value of 4
    (5 << 8) | 0x6D, // seven-segment value of 5
    (6 << 8) | 0x7D, // seven-segment value of 6
    (7 << 8) | 0x07, // seven-segment value of 7
};

extern char font[]; // Font mapping for 7-segment display
extern const int SPI_7SEG_SCK; extern const int SPI_7SEG_CSn; extern const int SPI_7SEG_TX;
static int index = 0; // Current index in the message buffer
extern const int SEG7_DMA_CHANNEL;

void display_init_bitbang() {
    // fill in  
    // 將三個腳位改為 GPIO 模式（SIO），輸出方向
    gpio_init(SPI_7SEG_SCK);
    gpio_set_function(SPI_7SEG_SCK, GPIO_FUNC_SIO);
    gpio_set_dir(SPI_7SEG_SCK, GPIO_OUT);

    gpio_init(SPI_7SEG_TX);
    gpio_set_function(SPI_7SEG_TX, GPIO_FUNC_SIO);
    gpio_set_dir(SPI_7SEG_TX, GPIO_OUT);

    gpio_init(SPI_7SEG_CSn);
    gpio_set_function(SPI_7SEG_CSn, GPIO_FUNC_SIO);
    gpio_set_dir(SPI_7SEG_CSn, GPIO_OUT);

    // 上電預設：CSn=高（不選晶片），SCK=低、TX=低（避免亂送資料）
    gpio_put(SPI_7SEG_CSn, 1);
    gpio_put(SPI_7SEG_SCK, 0);
    gpio_put(SPI_7SEG_TX, 0);

    // 小延遲，讓外設穩定（可有可無）
    sleep_us(10);  
}

static inline void bb_send16(uint16_t word) {
    for (int b = 15; b >= 0; --b) {
        // 先擺好資料
        gpio_put(SPI_7SEG_TX, (word >> b) & 1u);
        // setup time
        sleep_us(1);

        // 時脈上緣：外設取樣
        gpio_put(SPI_7SEG_SCK, 1);
        sleep_us(5);

        // 時脈下緣完成一個週期
        gpio_put(SPI_7SEG_SCK, 0);
        sleep_us(5);
    }
}

// ---- Step 2: Bit-banging SPI 傳送 msg[0..7] ----
// 規格：每個元素共 11 bits（[10:8]=位址、[7:0]=段碼），
// 但仍以 16 bits 形式送出（[15:11] = 0 的左側 5 個 padding）。
extern uint16_t msg[8];  // 若已在本檔案有定義就移除此行

void display_bitbang_spi() {
    // fill in 
    for (int i = 0; i < 8; ++i) {
        // 取出 11-bit 有效資料，確保高 5 bits 為 0
        uint16_t frame = (uint16_t)(msg[i] & 0x07FFu);  // [15:11]=0, [10:8]=addr, [7:0]=segments

        // 片選拉低，選取裝置
        gpio_put(SPI_7SEG_CSn, 0);
        sleep_us(10);  // device settle

        // 送 16 個 clock（MSB first）
        bb_send16(frame);

        // 片選拉高，結束一次傳送
        gpio_put(SPI_7SEG_CSn, 1);
        sleep_us(10);  // device settle
    }   
}

void display_init_spi() {
    // fill in  
    // Configure SCK, TX, CSn for SPI peripheral (SPI1 based on pin mapping: SCK=14, TX=15, CSn=13)
    // Set pin functions to SPI
    gpio_set_function(SPI_7SEG_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SPI_7SEG_TX,  GPIO_FUNC_SPI);
    gpio_set_function(SPI_7SEG_CSn, GPIO_FUNC_SPI);

    // Initialize SPI1 at 125 kHz, 16-bit frames, CPOL=0, CPHA=0, MSB first
    // Choose the instance based on the pins (SCK=14/MOSI=15 -> SPI1)
    spi_init(spi1, 125000);
    spi_set_format(spi1, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    // SPI is enabled after spi_init; no DMA/DREQ needed here (that's for Step 4)  
}

void display_print() {
    // fill in   
    // Send all 8 elements of msg[] over SPI as 16-bit words.
    // Each element contains 3-bit position (bits 10..8) + 8-bit segments (bits 7..0).
    for (int i = 0; i < 8; ++i) {
        uint16_t frame = (uint16_t)(msg[i] & 0x07FFu); // ensure upper 5 bits are 0
        spi_write16_blocking(spi1, &frame, 1);
    }
}

void display_init_dma() {
    // fill in   
    // 1) 取得並設定 DMA channel
    int chan = SEG7_DMA_CHANNEL;
    dma_channel_claim(chan);  // 若該 channel 已被用，這裡會 assert；請用沒被佔用的號碼

    dma_channel_config c = dma_channel_get_default_config(chan);

    // 2) 每次搬 16-bit；read 會遞增、write 不遞增；用 SPI1 TX 作為 DREQ
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);     // 16 bits
    channel_config_set_read_increment(&c, true);                // 來源位址遞增
    channel_config_set_write_increment(&c, false);              // 目的位址固定 (SPI DR)
    channel_config_set_dreq(&c, DREQ_SPI1_TX);                  // SPI1 TX 要資料就搬

    // 3) 設定 read-address ring wrap：在 16 bytes 邊界回繞 → ring_size_bits = 4
    //    false = 套用在 read addr（不是 write addr）
    channel_config_set_ring(&c, /*write=*/false, /*ring_size_bits=*/4);

    // 4) 綁定目的/來源/傳輸次數並啟動
    //    目的：SPI1 的 data 寄存器
    volatile void *spi_dr = &spi_get_hw(spi1)->dr;

    //    這裡把 trans_count 設很大讓它持續循環；你也可以設 8 並用進階 chaining 反覆啟動
    dma_channel_configure(
        chan, &c,
        spi_dr,     // write addr (destination)
        msg,        // read addr (source)
        0xFFFFFFFF, // transfer count：超大，依 DREQ 連續搬，read addr 會在 16B 範圍回繞
        true        // start now
    ); 
}

/***************************************************************** */

// We provide you with this function for directly displaying characters.
// This accounts for the decimal point in the 7-segment display, as well as
// the SPI/DMA-friendly format of the message buffer.
void display_char_print(const char message[]) {
    int dp_found = 0; 
    int out_idx = 0;
    for (int i = 0; i < 8 && message[i] != '\0'; i++) {
        if (message[i] == '.') {
            if (dp_found) {
                continue; // Ignore additional decimal points
            }
            if (out_idx > 0) {
                msg[out_idx - 1] |= (1 << 7); // Set decimal point bit
            }
            dp_found = 1;
        } else {
            uint16_t seg = font[(unsigned char)message[i]];
            // Insert position bits at bits 8-10
            seg |= (out_idx & 0x7) << 8;
            msg[out_idx] = seg;
            out_idx++;
        }
    }
    // Clear remaining digits if message shorter than 8
    for (; out_idx < 8; out_idx++) {
        msg[out_idx] = (out_idx << 8); // Only position bits, blank char
    }
}
