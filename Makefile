# makefile, written by guido socher
MCU=atmega168
DUDECPUTYPE=m168
#MCU=atmega328p
#DUDECPUTYPE=m328p
#
# === Edit this and enter the correct device/com-port:
# linux (plug in the avrusb500 and type dmesg to see which device it is):
LOADCMD=avrdude -P /dev/ttyUSB0

# mac (plug in the programer and use ls /dev/tty.usbserial* to get the name):
#LOADCMD=avrdude -P /dev/tty.usbserial-A9006MOb

# windows (check which com-port you get when you plugin the avrusb500):
#LOADCMD=avrdude -P COM4

# All operating systems: if you have set the default_serial paramter
# in your avrdude.conf file correctly then you can just use this
# and you don't need the above -P option:
#LOADCMD=avrdude
# === end edit this
#
LOADARG=-p $(DUDECPUTYPE) -c stk500v2 -e -U flash:w:
#
CC=avr-gcc
OBJCOPY=avr-objcopy
# optimize for size:
CFLAGS=-g -mmcu=$(MCU) -Wall -W -Os -mcall-prologues
#-------------------
.PHONY: all main
#
all: main.hex test_lcd.hex test_basic_ntpclient.hex
	@echo "done"
#
test_lcd: test_lcd.hex
	@echo "done"
#
test_basic_ntpclient: test_basic_ntpclient.hex
	@echo "done"
#
main: main.hex
	@echo "done"
#
#-------------------
help: 
	@echo "Usage: make all|main.hex|test_basic_ntpclient.hex"
	@echo "or"
	@echo "make fuse|rdfuses"
	@echo "or"
	@echo "make load"
	@echo "or"
	@echo "Usage: make clean"
	@echo " "
	@echo "You have to set the low fuse byte to 0x60 on all new tuxgraphics boards".
	@echo "This can be done with the command (linux/mac if you use avrusb500): make fuse"
#-------------------
main.hex: main.elf 
	$(OBJCOPY) -R .eeprom -O ihex main.elf main.hex 
	avr-size main.elf
	@echo " "
	@echo "Expl.: data=initialized data, bss=uninitialized data, text=code"
	@echo " "

main.elf: main.o ip_arp_udp_tcp.o enc28j60.o websrv_help_functions.o lcd.o timeconversions.o
	$(CC) $(CFLAGS) -o main.elf -Wl,-Map,main.map main.o ip_arp_udp_tcp.o enc28j60.o websrv_help_functions.o lcd.o timeconversions.o
websrv_help_functions.o: websrv_help_functions.c websrv_help_functions.h ip_config.h 
	$(CC) $(CFLAGS) -Os -c websrv_help_functions.c
enc28j60.o: enc28j60.c timeout.h enc28j60.h
	$(CC) $(CFLAGS) -Os -c enc28j60.c
ip_arp_udp_tcp.o: ip_arp_udp_tcp.c net.h enc28j60.h ip_config.h
	$(CC) $(CFLAGS) -Os -c ip_arp_udp_tcp.c
main.o: main.c ip_arp_udp_tcp.h enc28j60.h timeout.h net.h websrv_help_functions.h ip_config.h lcd.h timeconversions.h lcd_hw.h
	$(CC) $(CFLAGS) -Os -c main.c
#
timeconversions.o : timeconversions.c timeconversions.h
	$(CC) $(CFLAGS) -Os -c timeconversions.c
lcd.o : lcd.c lcd.h lcd_hw.h
	$(CC) $(CFLAGS) -Os -c lcd.c
#------------------
test_lcd.hex : test_lcd.elf 
	$(OBJCOPY) -R .eeprom -O ihex test_lcd.elf test_lcd.hex 
	avr-size test_lcd.elf
	@echo " "
	@echo "Expl.: data=initialized data, bss=uninitialized data, text=code"
	@echo " "
test_lcd.elf : test_lcd.o lcd.o
	$(CC) $(CFLAGS) -o test_lcd.elf -Wl,-Map,test_lcd.map test_lcd.o lcd.o
test_lcd.o : test_lcd.c lcd.h
	$(CC) $(CFLAGS) -Os -c test_lcd.c
#------------------
test_basic_ntpclient.hex: test_basic_ntpclient.elf 
	$(OBJCOPY) -R .eeprom -O ihex test_basic_ntpclient.elf test_basic_ntpclient.hex 
	avr-size test_basic_ntpclient.elf
	@echo " "
	@echo "Expl.: data=initialized data, bss=uninitialized data, text=code"
	@echo " "
test_basic_ntpclient.elf: test_basic_ntpclient.o ip_arp_udp_tcp.o enc28j60.o websrv_help_functions.o
	$(CC) $(CFLAGS) -o test_basic_ntpclient.elf -Wl,-Map,test_basic_ntpclient.map test_basic_ntpclient.o ip_arp_udp_tcp.o enc28j60.o websrv_help_functions.o
#
test_basic_ntpclient.o: test_basic_ntpclient.c ip_arp_udp_tcp.h enc28j60.h timeout.h net.h websrv_help_functions.h ip_config.h
	$(CC) $(CFLAGS) -Os -c test_basic_ntpclient.c
#------------------
load: main.hex
	$(LOADCMD) $(LOADARG)main.hex
#
load_test_basic_ntpclient: test_basic_ntpclient.hex
	$(LOADCMD) $(LOADARG)test_basic_ntpclient.hex
#
load_test_lcd: test_lcd.hex
	$(LOADCMD) $(LOADARG)test_lcd.hex
#-------------------
# Check this with make rdfuses
rdfuses:
	$(LOADCMD) -p $(DUDECPUTYPE) -c stk500v2 -v -q
#
fuse:
	@echo "warning: run this command only if you have an external clock on pin xtal1"
	@echo "The is the last chance to stop. Press crtl-C to abort"
	@sleep 2
	$(LOADCMD) -p  $(DUDECPUTYPE) -c stk500v2 -u -v -U lfuse:w:0x60:m
#-------------------
clean:
	rm -f *.o *.map *.elf  main.hex test_basic_ntpclient.hex test_lcd.hex
#-------------------
