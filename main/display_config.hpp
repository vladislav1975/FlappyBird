#pragma once

// Select the LovyanGFX v1 API before including the library header.
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define SCREEN_WIDTH      128  // Native display width in pixels, before rotation.
#define SCREEN_HEIGHT     160  // Native display height in pixels, before rotation.

// ============================================================================
// CENTRALIZED GPIO PIN ASSIGNMENTS
// ============================================================================
#define PIN_NUM_SCLK   18  // SPI clock signal.
#define PIN_NUM_MOSI   23  // SPI MOSI data output from the ESP32 to the display.
#define PIN_NUM_MISO   -1  // SPI MISO input; set to -1 because display readback is not connected.
#define PIN_NUM_DC     2   // Display data/command selection signal.
#define PIN_NUM_CS     5   // Display chip-select signal.
#define PIN_NUM_RST    4   // Display hardware reset signal.
#define PIN_NUM_BCKL   25  // Display backlight control signal.

// ============================================================================
// DISPLAY DEVICE CONFIGURATION CLASS
// ============================================================================
/**
 * @brief LovyanGFX device configuration for the project's ST7735 TFT display.
 *
 * This class binds the SPI bus configuration to the ST7735 panel configuration.
 * The pin assignments and native panel dimensions are defined above so the
 * hardware setup can be reviewed and adjusted in one place.
 */
class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7735S _panel_instance; // LovyanGFX panel driver for the ST7735 display controller.
  lgfx::Bus_SPI       _bus_instance;   // SPI bus configuration instance used by the display panel.

public:
  /**
   * @brief Configure the SPI bus and attach the ST7735 panel to this device.
   *
   * The constructor is called when the global display object is created.
   * LovyanGFX receives the bus settings first, then the panel settings.
   */
  LGFX(void)
  {
    // Configure the SPI bus used to communicate with the display.
    auto cfg_bus = _bus_instance.config();
    cfg_bus.spi_host   = SPI3_HOST;
    cfg_bus.spi_mode   = 0;
    cfg_bus.freq_write = 20000000;         // Write clock frequency: 20 MHz.
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

    // Configure the ST7735 panel geometry and control pins.
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
    cfg_panel.invert        = false;        // Color inversion is disabled for the display's current color configuration.
    cfg_panel.rgb_order     = true;
    cfg_panel.dlen_16bit    = false;
    cfg_panel.bus_shared    = false;

    _panel_instance.config(cfg_panel);
    setPanel(&_panel_instance);
  }
};
