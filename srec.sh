srec_cat bootloader.hex -intel -exclude 0x0000 0x3800 -o bootloader.hex -intel
srec_cat app.hex -intel bootloader.hex -intel -offset -minimum-addr security.hex -intel -o firmw.hex -intel
