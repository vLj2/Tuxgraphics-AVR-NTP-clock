# makefile, written by guido socher
MCU=atmega168
DUDECPUTYPE=m168
#MCU=atmega88
#DUDECPUTYPE=m88
#MCU=atmega328p
#DUDECPUTYPE=m328
CC=avr-gcc
OBJCOPY=avr-objcopy
# optimize for size:
CFLAGS=-g -mmcu=$(MCU) -Wall -Wstrict-prototypes -Os -mcall-prologues
#
LOADCMD=avrdude
LOADARG=-p $(DUDECPUTYPE) -c stk500v2 -e -U flash:w:
#
#-------------------
.PHONY: test0 test1 test2 all testLCD
#
all: eth_ntp_clock.hex test0.hex test1.hex test2.hex test_readSiliconRev.hex testLCD.hex 
#
test0: test0.hex
#
testLCD: testLCD.hex
#
test1: test1.hex 
#
test2: test2.hex
#
#-------------------
help: 
	@echo "Usage: make all|testLCD.hex|test0.hex|test1.hex|test2.hex|load|load_testLCD|load_test0|load_test1|load_test2|rdfuses"
	@echo "or"
	@echo "Usage: make clean"
	@echo "to set the clock source correctly run"
	@echo "make fuse"
#-------------------
eth_ntp_clock.hex : eth_ntp_clock.out 
	$(OBJCOPY) -R .eeprom -O ihex eth_ntp_clock.out eth_ntp_clock.hex 
	avr-size eth_ntp_clock.out
	@echo " "
	@echo "Expl.: data=initialized data, bss=uninitialized data, text=code"
	@echo " "

eth_ntp_clock.out : main.o ip_arp_udp_tcp.o enc28j60.o timeconversions.o lcd.o
	$(CC) $(CFLAGS) -o eth_ntp_clock.out -Wl,-Map,eth_ntp_clock.map main.o ip_arp_udp_tcp.o enc28j60.o  timeconversions.o lcd.o
enc28j60.o : enc28j60.c avr_compat.h timeout.h enc28j60.h
	$(CC) $(CFLAGS) -Os -c enc28j60.c
ip_arp_udp_tcp.o : ip_arp_udp_tcp.c net.h avr_compat.h enc28j60.h
	$(CC) $(CFLAGS) -Os -c ip_arp_udp_tcp.c
main.o : main.c ip_arp_udp_tcp.h avr_compat.h enc28j60.h timeout.h net.h
	$(CC) $(CFLAGS) -Os -c main.c
timeconversions.o : timeconversions.c timeconversions.h 
	$(CC) $(CFLAGS) -Os -c timeconversions.c
lcd.o : lcd.c lcd.h lcd_hw.h
	$(CC) $(CFLAGS) -Os -c lcd.c
#------------------
testLCD.hex : testLCD.out 
	$(OBJCOPY) -R .eeprom -O ihex testLCD.out testLCD.hex 
	avr-size testLCD.out
	@echo " "
	@echo "Expl.: data=initialized data, bss=uninitialized data, text=code"
	@echo " "
testLCD.out : testLCD.o lcd.o
	$(CC) $(CFLAGS) -o testLCD.out -Wl,-Map,testLCD.map testLCD.o lcd.o
testLCD.o : testLCD.c lcd.h
	$(CC) $(CFLAGS) -Os -c testLCD.c
#------------------
test0.hex : test0.out 
	$(OBJCOPY) -R .eeprom -O ihex test0.out test0.hex 
	avr-size test0.out
	@echo " "
	@echo "Expl.: data=initialized data, bss=uninitialized data, text=code"
	@echo " "
test0.out : test0.o 
	$(CC) $(CFLAGS) -o test0.out -Wl,-Map,test0.map test0.o 
test0.o : test0.c 
	$(CC) $(CFLAGS) -Os -c test0.c
#------------------
test2.hex : test2.out 
	$(OBJCOPY) -R .eeprom -O ihex test2.out test2.hex 
	avr-size test2.out
	@echo " "
	@echo "Expl.: data=initialized data, bss=uninitialized data, text=code"
	@echo " "
test2.out : test2.o enc28j60.o ip_arp_udp_tcp.o
	$(CC) $(CFLAGS) -o test2.out -Wl,-Map,test2.map test2.o enc28j60.o ip_arp_udp_tcp.o
test2.o : test2.c ip_arp_udp_tcp.h avr_compat.h enc28j60.h timeout.h net.h
	$(CC) $(CFLAGS) -Os -c test2.c
#------------------
test1.hex : test1.out 
	$(OBJCOPY) -R .eeprom -O ihex test1.out test1.hex 
	avr-size test1.out
	@echo " "
	@echo "Expl.: data=initialized data, bss=uninitialized data, text=code"
	@echo " "
test1.out : test1.o enc28j60.o ip_arp_udp_tcp.o
	$(CC) $(CFLAGS) -o test1.out -Wl,-Map,test1.map test1.o enc28j60.o ip_arp_udp_tcp.o
test1.o : test1.c ip_arp_udp_tcp.h avr_compat.h enc28j60.h timeout.h net.h
	$(CC) $(CFLAGS) -Os -c test1.c
#------------------
test_readSiliconRev.hex : test_readSiliconRev.out 
	$(OBJCOPY) -R .eeprom -O ihex test_readSiliconRev.out test_readSiliconRev.hex 
	avr-size test_readSiliconRev.out
	@echo " "
	@echo "Expl.: data=initialized data, bss=uninitialized data, text=code"
	@echo " "
test_readSiliconRev.out : test_readSiliconRev.o enc28j60.o ip_arp_udp_tcp.o
	$(CC) $(CFLAGS) -o test_readSiliconRev.out -Wl,-Map,test_readSiliconRev.map test_readSiliconRev.o enc28j60.o ip_arp_udp_tcp.o
test_readSiliconRev.o : test_readSiliconRev.c ip_arp_udp_tcp.h avr_compat.h enc28j60.h timeout.h net.h
	$(CC) $(CFLAGS) -Os -c test_readSiliconRev.c
#------------------
#------------------
load_testLCD: testLCD.hex
	$(LOADCMD) $(LOADARG)testLCD.hex
load_test2: test2.hex
	$(LOADCMD) $(LOADARG)test2.hex
load_readSiliconRev: test_readSiliconRev.hex
	$(LOADCMD) $(LOADARG)test_readSiliconRev.hex
load_test1: test1.hex
	$(LOADCMD) $(LOADARG)test1.hex
load_test0: test0.hex
	$(LOADCMD) $(LOADARG)test0.hex
#------------------
load: eth_ntp_clock.hex
	$(LOADCMD) $(LOADARG)eth_ntp_clock.hex
#
pre: eth_ntp_clock.hex
	cp eth_ntp_clock.hex eth_ntp_clock-precompiled.hex
#
#-------------------
# Check this with make rdfuses
rdfuses:
	avrdude -p $(DUDECPUTYPE) -c stk500v2 -v -q
#
fuse:
	@echo "Setting AVR clock source to clock signal from enc28j60..."
	@sleep 1
	avrdude -p  $(DUDECPUTYPE) -c stk500v2 -u -v -U lfuse:w:0x60:m
#-------------------
clean:
	rm -f *.o *.map *.out test*.hex eth_ntp_clock.hex
#-------------------
