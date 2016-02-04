-- setup SPI and connect display
function init_spi_display()
   -- Hardware SPI CLK  = GPIO14
   -- Hardware SPI MOSI = GPIO13
   -- Hardware SPI MISO = GPIO12 (not used)
   -- CS, D/C, and RES can be assigned freely to available GPIOs
   local cs  = 8 -- GPIO15, pull-down 10k to GND
   local dc  = 4 -- GPIO2
   local res = 0 -- GPIO16

   spi.setup(1, spi.MASTER, spi.CPOL_LOW, spi.CPHA_LOW, 8, 8)
   disp = ucg.ili9341_18x240x320_hw_spi(cs, dc, res)
end



init_spi_display()

disp:begin(ucg.FONT_MODE_TRANSPARENT)
disp:clearScreen()

disp:setFont(ucg.font_ncenR12_tr);
disp:setColor(255, 255, 255);
disp:setColor(1, 255, 0,0);


disp:setPrintPos(0, 25)
disp:print("Hello World!")
