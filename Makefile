# makefile, written by guido socher
MCU=atmega168
CC=avr-gcc
OBJCOPY=avr-objcopy
# optimize for size:
CFLAGS=-g -mmcu=$(MCU) -Wall -Wstrict-prototypes -Os -mcall-prologues
#-------------------
.PHONY: test0 test1 test2 all testLCD
#
all: test0.hex test1.hex test2.hex test_readSiliconRev.hex testLCD.hex eth_ntp_clock.hex 
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

eth_ntp_clock.out : main.o ip_arp_udp_tcp.o enc28j60.o timeout.o timeconversions.o lcd.o
	$(CC) $(CFLAGS) -o eth_ntp_clock.out -Wl,-Map,eth_ntp_clock.map main.o ip_arp_udp_tcp.o enc28j60.o timeout.o timeconversions.o lcd.o
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
timeout.o : timeout.c timeout.h 
	$(CC) $(CFLAGS) -Os -c timeout.c
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
test2.out : test2.o enc28j60.o timeout.o ip_arp_udp_tcp.o
	$(CC) $(CFLAGS) -o test2.out -Wl,-Map,test2.map test2.o enc28j60.o timeout.o ip_arp_udp_tcp.o
test2.o : test2.c ip_arp_udp_tcp.h avr_compat.h enc28j60.h timeout.h net.h
	$(CC) $(CFLAGS) -Os -c test2.c
#------------------
test1.hex : test1.out 
	$(OBJCOPY) -R .eeprom -O ihex test1.out test1.hex 
	avr-size test1.out
	@echo " "
	@echo "Expl.: data=initialized data, bss=uninitialized data, text=code"
	@echo " "
test1.out : test1.o enc28j60.o timeout.o ip_arp_udp_tcp.o
	$(CC) $(CFLAGS) -o test1.out -Wl,-Map,test1.map test1.o enc28j60.o timeout.o ip_arp_udp_tcp.o
test1.o : test1.c ip_arp_udp_tcp.h avr_compat.h enc28j60.h timeout.h net.h
	$(CC) $(CFLAGS) -Os -c test1.c
#------------------
test_readSiliconRev.hex : test_readSiliconRev.out 
	$(OBJCOPY) -R .eeprom -O ihex test_readSiliconRev.out test_readSiliconRev.hex 
	avr-size test_readSiliconRev.out
	@echo " "
	@echo "Expl.: data=initialized data, bss=uninitialized data, text=code"
	@echo " "
test_readSiliconRev.out : test_readSiliconRev.o enc28j60.o timeout.o ip_arp_udp_tcp.o
	$(CC) $(CFLAGS) -o test_readSiliconRev.out -Wl,-Map,test_readSiliconRev.map test_readSiliconRev.o enc28j60.o timeout.o ip_arp_udp_tcp.o
test_readSiliconRev.o : test_readSiliconRev.c ip_arp_udp_tcp.h avr_compat.h enc28j60.h timeout.h net.h
	$(CC) $(CFLAGS) -Os -c test_readSiliconRev.c
#------------------
#------------------
load_testLCD: testLCD.hex
	./prg_load_uc_168 testLCD.hex
load_test2: test2.hex
	./prg_load_uc_168 test2.hex
load_readSiliconRev: test_readSiliconRev.hex
	./prg_load_uc_168 test_readSiliconRev.hex
load_test1: test1.hex
	./prg_load_uc_168 test1.hex
load_test0: test0.hex
	./prg_load_uc_168 test0.hex
#------------------
load: eth_ntp_clock.hex
	./prg_load_uc_168 eth_ntp_clock.hex
#
#-------------------
# Check this with make rdfuses
rdfuses:
	./prg_fusebit_uc_168 -r
#
fuse:
	@echo "Setting AVR clock source to clock signal from enc28j60..."
	@sleep 1
	./prg_fusebit_uc_168 -w 0x60
#-------------------
clean:
	rm -f *.o *.map *.out test*.hex eth_ntp_clock.hex
#-------------------
