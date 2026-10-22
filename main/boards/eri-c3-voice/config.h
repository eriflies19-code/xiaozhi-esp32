#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

// Audio: 1 bo I2S cua C3, mic va loa dung chung BCLK/WS (duplex)
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000

#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_2   // MAX98357A BCLK + INMP441 SCK
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_1   // MAX98357A LRC  + INMP441 WS
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_3   // MAX98357A DIN
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_8   // INMP441 SD

// Buttons
#define CHAT_BUTTON_GPIO    GPIO_NUM_4   // bam: bat/tat hoi thoai
#define VOLUME_BUTTON_GPIO  GPIO_NUM_10  // bam: tang am luong, giu: giam

// OLED SSD1306 128x64 (I2C)
#define DISPLAY_SDA_PIN     GPIO_NUM_21
#define DISPLAY_SCL_PIN     GPIO_NUM_20
#define DISPLAY_WIDTH       128
#define DISPLAY_HEIGHT      64
#define DISPLAY_MIRROR_X    false
#define DISPLAY_MIRROR_Y    false
#define DISPLAY_I2C_ADDR    0x3C

#endif // _BOARD_CONFIG_H_
