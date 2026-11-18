#pragma once

// Говорим библиотеке LovyanGFX, что мы используем современную версию интерфейса (V1)
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define SCREEN_WIDTH      128  // Ширина экрана в пикселях
#define SCREEN_HEIGHT     160  // Высота экрана в пикселях

// ============================================================================
// ЦЕНТРАЛИЗОВАННЫЕ НАСТРОЙКИ ПИНОВ
// ============================================================================
#define PIN_NUM_SCLK   18  // Тактовый сигнал SPI (Clock)
#define PIN_NUM_MOSI   23  // Передача данных от ESP32 к экрану (MOSI)
#define PIN_NUM_MISO   -1  // Прием данных (не используется)
#define PIN_NUM_DC     2   // Выбор режима Данные/Команда (Data/Command)
#define PIN_NUM_CS     5   // Выбор чипа (Chip Select)
#define PIN_NUM_RST    4   // Аппаратный сброс дисплея (Reset)
#define PIN_NUM_BCKL   25  // Подсветка экрана (Backlight)

// ============================================================================
// СОЗДАНИЕ КЛАССА НАСТРОЙКИ ДИСПЛЕЯ
// ============================================================================
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7735S _panel_instance; // Драйвер экрана ST7735
  lgfx::Bus_SPI       _bus_instance;   // Шина SPI

public:
  LGFX(void)
  {
    // Настройка шины SPI
    auto cfg_bus = _bus_instance.config();
    cfg_bus.spi_host   = SPI3_HOST;
    cfg_bus.spi_mode   = 0;
    cfg_bus.freq_write = 20000000;         // 20 МГц для быстрой отрисовки
    cfg_bus.freq_read  = 16000000;
    cfg_bus.spi_3wire  = true;
    cfg_bus.use_lock   = true;
    cfg_bus.dma_channel = SPI_DMA_CH_AUTO;
    cfg_bus.pin_sclk   = PIN_NUM_SCLK;
    cfg_bus.pin_mosi   = PIN_NUM_MOSI;
    cfg_bus.pin_miso   = PIN_NUM_MISO;
    cfg_bus.pin_dc     = PIN_NUM_DC;
    _bus_instance.config(cfg_bus);
    _panel_instance.setBus(&_bus_instance);

    // Настройка панели дисплея
    auto cfg_panel = _panel_instance.config();
    cfg_panel.pin_cs     = PIN_NUM_CS;
    cfg_panel.pin_rst    = PIN_NUM_RST;
    cfg_panel.pin_busy   = -1;
    cfg_panel.memory_width  = SCREEN_WIDTH;
    cfg_panel.memory_height = SCREEN_HEIGHT;
    cfg_panel.panel_width   = SCREEN_WIDTH;
    cfg_panel.panel_height  = SCREEN_HEIGHT;
    cfg_panel.offset_x      = 0;
    cfg_panel.offset_y      = 0;
    cfg_panel.readable      = true;
    cfg_panel.invert        = false;        // Инверсия цветов для правильной палитры вашего экрана
    cfg_panel.rgb_order     = true;
    cfg_panel.dlen_16bit    = false;
    cfg_panel.bus_shared    = false;

    _panel_instance.config(cfg_panel);
    setPanel(&_panel_instance);
  }
};
