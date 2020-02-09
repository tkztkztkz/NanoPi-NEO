
#include <vector>
#include <thread>
#include "common/ctrl_spi.h"
#include "common/ctrl_gpio_poll.h"

#ifdef DISPLAY_NANOHATOLED
#include "common/display_ssd1306_i2c.h"	// Conflict to fbdev.h. Use exclusive.
#endif

#if defined(DISPLAY_FBDEV_FB0) || defined(DISPLAY_FBDEV_FB1)
#include "common/display_fbdev.h"
#endif

#if defined(DISPLAY_13IPS240240) || defined(DISPLAY_IPS320240)|| defined(DISPLAY_IPS320240V) || defined(DISPLAY_13IPS240240_RPI)
#include "common/display_st7789_spi.h"
#endif

//#include "common/display_st7735_spi.h"

#if defined(DISPLAY_RASPI_DAP_BASE)
#include "common/display_ili9341_spi.h"
#endif
//#include "common/display_ili9328_spi.h"
//#include "common/display_ili9225_spi.h"
//#include "common/display_ili9486_spi.h"

std::vector<DisplayIF*>	GetUsrDisplays()
{
	std::vector<DisplayIF*>	iDisplays;

#ifdef DISPLAY_NANOHATOLED
	iDisplays.push_back( new Display_SSD1306_i2c(180,0) );
#endif
#ifdef DISPLAY_13IPS240240
	iDisplays.push_back( new Display_ST7789_IPS_240x240_spi(0) );
#endif
#ifdef DISPLAY_13IPS240240_RPI
	iDisplays.push_back( new Display_ST7789_IPS_240x240_spi(270, -1,25,27,24) );
#endif
#ifdef DISPLAY_FBDEV_FB0
	iDisplays.push_back( new Display_fbdev("/dev/fb0") );
#endif
#ifdef DISPLAY_FBDEV_FB1
	iDisplays.push_back( new Display_fbdev("/dev/fb1") );
#endif
#ifdef DISPLAY_IPS320240
	iDisplays.push_back( new Display_ST7789_IPS_spi(270) );
#endif
#ifdef DISPLAY_IPS320240V
	iDisplays.push_back( new Display_ST7789_IPS_spi(0) );
#endif

#ifdef DISPLAY_RASPI_DAP_BASE
//	iDisplays.push_back( new Display_ILI9341_spi_TM24(180, 8, 17, 18, -1) );
	iDisplays.push_back( new Display_ILI9341_spi_TM24(180, 8, 25, -1, 16) );
#endif
//	iDisplays.push_back( new Display_SSD1306_i2c(180,2) );	// for SH1306

//	iDisplays.push_back( new Display_ILI9341_spi_TM24(270,67) );
//	iDisplays.push_back( new Display_ILI9341_spi_TM22(270) );
//	iDisplays.push_back( new Display_ILI9341_spi_TM22(0) );
//	iDisplays.push_back( new Display_ILI9328_spi_TM22(270));
//	iDisplays.push_back( new Display_ILI9225_spi(270));//,198) );
//	iDisplays.push_back( new Display_ST7735_spi(90,3) );
//	iDisplays.push_back( new Display_ST7789_spi(270) );
//	iDisplays.push_back( new Display_WaveShare35_spi(0) );
//	iDisplays.push_back( new Display_ILI9486_spi(270) );
//	iDisplays.push_back( new Display_fbdev("/dev/fb1") );
	return	iDisplays;
}

