#include "wifi_board.h"
#include "es8388_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "i2c_device.h"
#include "iot/thing_manager.h"
#include "led/single_led.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <wifi_station.h>

#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "driver/gpio.h"
#define TAG "atk_dnesp32s3"


// 定义引脚
#define PIN_NUM_MISO (gpio_num_t)17 // MISO
#define PIN_NUM_MOSI 8 // MOSI
#define PIN_NUM_CLK 18 // CLK
#define PIN_NUM_NSS (gpio_num_t)19 // CLK

// 定义 SD 卡的最大频率
#define SD_SPI_MAX_FREQ 40000000 // 40 MHz


LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

class XL9555 : public I2cDevice {
public:
    XL9555(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x06, 0x03);
        WriteReg(0x07, 0xF0);
    }

    void SetOutputState(uint8_t bit, uint8_t level) {
        uint16_t data;
        int index = bit;

        if (bit < 8) {
            data = ReadReg(0x02);
        } else {
            data = ReadReg(0x03);
            index -= 8;
        }

        data = (data & ~(1 << index)) | (level << index);

        if (bit < 8) {
            WriteReg(0x02, data);
        } else {
            WriteReg(0x03, data);
        }
    }
};


class atk_dnesp32s3 : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    Button boot_button_;
    LcdDisplay* display_;
   // XL9555* xl9555_;

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 10,
        
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    // Initialize spi peripheral
    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = LCD_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = LCD_SCLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }
    void InitiallizeTFCard()
    {
        esp_err_t ret;

        // 打印初始化信息
        ESP_LOGI("TFCARD", "Initializing SD card over SPI...\n");

        gpio_config_t io_conf = {
            .pin_bit_mask =  (1ULL << PIN_NUM_NSS),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
           .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(PIN_NUM_NSS, 0);

        // 配置 SD 卡挂载参数
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        // host.max_freq_khz = SD_SPI_MAX_FREQ / 1000; // 设置最大频率为 40 MHz

        // 挂载 SD 卡
        esp_vfs_fat_sdmmc_mount_config_t mount_config = {
            .format_if_mount_failed = true,    // 如果挂载失败，不格式化 SD 卡
            .max_files = 5,                    // 最大文件句柄数
            .allocation_unit_size = 16 * 1024, // 分配单元大小
        };

        // 配置 SPI 总线参数
        spi_bus_config_t bus_cfg = {
            .mosi_io_num = PIN_NUM_MOSI,
            .miso_io_num = PIN_NUM_MISO,
            .sclk_io_num = PIN_NUM_CLK,
            .quadwp_io_num = -1, // 不使用 Quad SPI 模式
            .quadhd_io_num = -1,
            .max_transfer_sz = 4000, // 最大传输大小
        };

        // 初始化 SPI 总线
        ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK)
        {
            printf("Failed to initialize SPI bus: %s\n", esp_err_to_name(ret));
            return;
        }
        else
        {
            printf("SPI bus initialized successfully.\n");
        }
     //   vTaskDelay(400);
        // 配置 SPI 总线
        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        // slot_config.gpio_cs = PIN_NUM_CS;
        slot_config.host_id = (spi_host_device_t)host.slot; // 使用默认的 SPI 主机 ID

        sdmmc_card_t *card;
        printf("Mounting SD card...\n");
        ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
        if (ret != ESP_OK)
        {
            if (ret == ESP_FAIL)
            {
                printf("Failed to mount filesystem. If you want the card to be formatted, set format_if_mount_failed = true.\n");
            }
            else
            {
                printf("Failed to initialize the card (%s). Make sure SD card lines have pull-up resistors in place.\n", esp_err_to_name(ret));
            }
            spi_bus_free(SPI2_HOST); // 释放 SPI 总线
            return;
        }
        printf("SD card mounted successfully.\n");

        // 打印 SD 卡信息
        sdmmc_card_print_info(stdout, card);

        // 测试读写操作
        FILE *f = fopen("/sdcard/test.txt", "w");
        if (f == NULL)
        {
            printf("Failed to open file for writing\n");
            return;
        }
        fprintf(f, "Hello, ESP-IDF SD Card!\n");
        fclose(f);
        printf("File written successfully.\n");

        // 读取测试文件
        f = fopen("/sdcard/test.txt", "r");
        if (f == NULL)
        {
            printf("Failed to open file for reading\n");
            return;
        }
        char line[64];
        fgets(line, sizeof(line), f);
        fclose(f);
        printf("Read from file: %s\n", line);
    }
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

    void InitializeSt7789Display() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        ESP_LOGD(TAG, "Install panel IO");
        // 液晶屏控制IO初始化
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = LCD_CS_PIN;
        io_config.dc_gpio_num = LCD_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 20 * 1000 * 1000;
        io_config.trans_queue_depth = 7;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io);

        // 初始化液晶屏驱动芯片ST7789
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;
        panel_config.data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel);
        
        esp_lcd_panel_reset(panel);
       // xl9555_->SetOutputState(8, 1);
       // xl9555_->SetOutputState(2, 0);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY); 
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_20_4,
                                        .icon_font = &font_awesome_20_4,
                                        #if CONFIG_USE_WECHAT_MESSAGE_STYLE
                                            .emoji_font = font_emoji_32_init(),
                                        #else
                                            .emoji_font = DISPLAY_HEIGHT >= 240 ? font_emoji_64_init() : font_emoji_32_init(),
                                        #endif
                                    });
    }

    // 物联网初始化，添加对 AI 可见设备 
    void InitializeIot() {
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
      //  thing_manager.AddThing(iot::CreateThing("Screen"));
    }

public:
    atk_dnesp32s3() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        InitializeSpi();
        InitializeSt7789Display();
        InitializeButtons();
        InitializeIot();
      //  InitiallizeTFCard();
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8388AudioCodec audio_codec(
            i2c_bus_, 
            I2C_NUM_0, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            GPIO_NUM_NC, 
            AUDIO_CODEC_ES8388_ADDR
        );
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }
};

DECLARE_BOARD(atk_dnesp32s3);
