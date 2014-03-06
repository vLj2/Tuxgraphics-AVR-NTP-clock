/*********************************************
 * vim:sw=8:ts=8:si:et
 * To use the above modeline in vim you must have "set modeline" in your .vimrc
 * Author: Guido Socher
 * Copyright: GPL V2
 * See http://www.gnu.org/licenses/gpl.html
 *
 * Chip type           : Atmega168 or Atmega328 with ENC28J60
 *********************************************/
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>
#include "ip_arp_udp_tcp.h"
#include "websrv_help_functions.h"
#include "enc28j60.h"
#include "timeout.h"
#include "net.h"
#include "lcd.h"
#include "timeconversions.h"

// Note: This software implements a NTP clock with LCD display and 
// built-in web server.
// The web server is at "myip" and the NTP client tries to access "ntpip".
// 
// Please modify the following lines. mac and ip have to be unique
// in your local area network. You can not have the same numbers in
// two devices:
static uint8_t mymac[6] = {0x54,0x55,0x58,0x10,0x00,0x28};
// how did I get the mac addr? Translate the first 3 numbers into ascii is: TUX
static uint8_t myip[4] = {10,0,0,28};
// Default gateway. The ip address of your DSL router. It can be set to the same as
// msrvip the case where there is no default GW to access the 
// web server (=web server is on the same lan as this host) 
static uint8_t gwip[4] = {10,0,0,2};
//
// change summer/winter time and your timezone here (GMT +1 is Germany, France etc... in winter), unit is hours times 10:
//int8_t hours_offset_to_utc=+10;  // +10 means +1.0 hours = +1 hour
// US/Canada eastern time in summer (-4 hours):
int16_t hours_offset_to_utc=-40;
//
// listen port for tcp/www:
#define MYWWWPORT 80
//
// --------------- normally you don't change anything below this line
// time.apple.com (any of 17.254.0.31, 17.254.0.26, 17.254.0.27, 17.254.0.28):
static uint8_t ntpip[4] = {17,254,0,31};
// timeserver.rwth-aachen.de
//static uint8_t ntpip[4] = {134,130,4,17};
// time.nrc.ca
//static uint8_t ntpip[4] = {132,246,168,148};
//
// list of other servers:
// tick.utoronto.ca 128.100.56.135
// ntp.nasa.gov 198.123.30.132
// timekeeper.isi.edu 128.9.176.30
//
// -------- never change anything below this line ---------
// global string buffer
#define STR_BUFFER_SIZE 15
static char gStrbuf[STR_BUFFER_SIZE+1];
static uint16_t gPlen;

static uint8_t ntpclientportL=0; // lower 8 bytes of local port number
static uint8_t prev_minutes=99; // inititlaize to something that does not exist
static uint8_t haveNTPanswer=0; // 0=never sent an ntp req, 1=have time, 2=reqest sent no answer yet
static char lindicator='/'; // link indicator on the right
static uint8_t send_ntp_req_from_idle_loop=1;
static uint8_t update_at_58_avoid_duplicates=0;
// this is were we keep time (in unix gmtime format):
// Note: this value may jump a few seconds when a new ntp answer comes.
// You need to keep this in mid if you build an alarm clock. Do not match
// on "==" use ">=" and set a state that indicates if you already triggered the alarm.
static uint32_t time=0;
static uint8_t lcd_needs_update=0;
static uint8_t waiting_gw=0;
static uint8_t display_24hclock=1;
static uint8_t display_utcoffset=1;
// eth/ip buffer:
#define BUFFER_SIZE 670
static uint8_t buf[BUFFER_SIZE+1];

// set output to VCC, red LED off
#define LEDOFF PORTB|=(1<<PORTB1)
// set output to GND, red LED on
#define LEDON PORTB&=~(1<<PORTB1)
// to test the state of the LED
#define LEDISOFF PORTB&(1<<PORTB1)
// 
static char password[10]="secret"; // must be a-z and 0-9, will be cut to 8 char
//
uint8_t verify_password(char *str)
{
        // a simple password/cookie:
        if (strncmp(password,str,sizeof(password))==0){
                return(1);
        }
        return(0);
}

// add a decimal point if needed: 120 becomes 12 and 105 becomes 10.5
// 10 becomes 1 and 11 becomes 1.1
// string must be at least 2 char long.
void adddotifneeded(char *s)
{
        int8_t l;
        l=strlen(s);
        if (l<2){
                return;
        }
        // look at last digit:
        if(s[l-1]=='0'){
                s[l-1]='\0';
        }else{
                // add dot
                s[l]=s[l-1];
                s[l-1]='.';
                s[l+1]='\0';
        }
}

// delete any decimal point. If no decimal point is available then add a zero at the 
// end.
void deldot(char *s)
{
        char c;
        char *dst;
        uint8_t founddot=0;
        dst=s;
        while ((c = *s)) {
                if (c == '.'||c == ','){ // dot or comma
                        s++;
                        founddot=1;
                        continue;
                }
                *dst = c;
                dst++;
                s++;
        }
        if (founddot==0){
                *dst = '0';
                dst++;
        }
        *dst = '\0';
}


uint16_t http200ok(void)
{
        return(fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\n\r\n")));
}

uint16_t http200okjs(void)
{
        return(fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: application/x-javascript\r\n\r\n")));
}

// t1.js
uint16_t print_t1js(void)
{
        uint16_t plen;
        plen=http200okjs();
        // show this computers TZ offset
        plen=fill_tcp_data_p(buf,plen,PSTR("\
function tzi(){\n\
var t = new Date();\n\
var tzo=t.getTimezoneOffset()/60;\n\
var st;\n\
if (tzo>0) st=\"GMT-\"+tzo; else st=\"GMT+\"+tzo;\n\
document.write(\" [Info: your PC is: \"+st+\"]\");\n\
}\n\
"));
        return(plen);
}

uint16_t print_webpage_ok(void)
{
        return(fill_tcp_data_p(buf,http200ok(),PSTR("<a href=/>OK</a>. Please power-cycle after IP-addr. change")));
}

// prepare the webpage by writing the data to the tcp send buffer
// Note that this is about as much as you can put on a html page.
// If you plan to build additional functions into this clock (e.g alarm clock) then
// add a new page under /alarm and do not expand this /config page.
uint16_t print_webpage_config(void)
{
        uint16_t plen;
        uint8_t i;
        plen=http200ok();
        plen=fill_tcp_data_p(buf,plen,PSTR("<script src=t1.js></script>"));
        plen=fill_tcp_data_p(buf,plen,PSTR("<h2>config</h2><pre>MAC addr: "));
        mk_net_str(gStrbuf,mymac,6,':',16);
        plen=fill_tcp_data(buf,plen,gStrbuf);
        plen=fill_tcp_data_p(buf,plen,PSTR("\n<form action=/cu method=get>Clock IP  :<input type=text name=ip value="));
        mk_net_str(gStrbuf,myip,4,'.',10);
        plen=fill_tcp_data(buf,plen,gStrbuf);
        plen=fill_tcp_data_p(buf,plen,PSTR(">\nDefault GW:<input type=text name=gw value="));
        mk_net_str(gStrbuf,gwip,4,'.',10);
        plen=fill_tcp_data(buf,plen,gStrbuf);
        plen=fill_tcp_data_p(buf,plen,PSTR(">\nNTP srv IP:<input type=text name=ns value="));
        mk_net_str(gStrbuf,ntpip,4,'.',10);
        plen=fill_tcp_data(buf,plen,gStrbuf);
        plen=fill_tcp_data_p(buf,plen,PSTR(">\nGMT offset:<input type=text name=tz value="));
        i=0;
        if (hours_offset_to_utc>=0){
                gStrbuf[i]='+';
                i++;
        }
        itoa(hours_offset_to_utc,&(gStrbuf[i]),10);
        adddotifneeded(&(gStrbuf[i]));
        plen=fill_tcp_data(buf,plen,gStrbuf);
        plen=fill_tcp_data_p(buf,plen,PSTR("><script>tzi()</script>\n"));
        plen=fill_tcp_data_p(buf,plen,PSTR("show: <input type=checkbox name=hh"));
        if (display_24hclock){
                plen=fill_tcp_data_p(buf,plen,PSTR(" checked"));
        }
        plen=fill_tcp_data_p(buf,plen,PSTR(">24h <input type=checkbox name=uo"));
        if (display_utcoffset){
                plen=fill_tcp_data_p(buf,plen,PSTR(" checked"));
        }
        plen=fill_tcp_data_p(buf,plen,PSTR(">GMT offset\n\n"));
        plen=fill_tcp_data_p(buf,plen,PSTR("New pw:<input type=text name=np>\n\npw:    <input type=password name=pw><input type=submit value=apply></form>\n"));
        return(plen);
}

// prepare the main webpage by writing the data to the tcp send buffer
uint16_t print_webpage(void)
{
        uint16_t plen;
        char day[16];
        char clock[12];
        plen=http200ok();
        plen=fill_tcp_data_p(buf,plen,PSTR("<h2>NTP clock</h2><pre>\n"));
        if (haveNTPanswer){ // 1 or 2
                gmtime(time+(int32_t)+360*(int32_t)hours_offset_to_utc,display_24hclock,day,clock);
                plen=fill_tcp_data(buf,plen,day);
                plen=fill_tcp_data(buf,plen,"\n");
                plen=fill_tcp_data(buf,plen,clock);
                plen=fill_tcp_data_p(buf,plen,PSTR(" (GMT"));
                if (hours_offset_to_utc>=0){
                        plen=fill_tcp_data(buf,plen,"+");
                }
                itoa(hours_offset_to_utc,gStrbuf,10);
                adddotifneeded(gStrbuf);
                plen=fill_tcp_data(buf,plen,gStrbuf);
                plen=fill_tcp_data_p(buf,plen,PSTR(")\n"));
                if (haveNTPanswer>1){
                        plen=fill_tcp_data_p(buf,plen,PSTR("Last ntp sync is older than 1 hour\n"));
                }
                plen=fill_tcp_data_p(buf,plen,PSTR("</pre><br>\n"));
        }else{
                plen=fill_tcp_data_p(buf,plen,PSTR("Waiting for NTP answer...<br>\n"));
        }
        plen=fill_tcp_data_p(buf,plen,PSTR("<a href=\"./config\">[config]</a> <a href=\"./\">[refresh]</a>\n"));
        return(plen);
}

// analyse the url given
// return values: are used to show different pages (see main program)
//
int8_t analyse_get_url(char *str)
{
        uint8_t updateerr=0;
        int16_t i;
        // the first slash:
        if (str[0] == '/' && str[1] == ' '){
                // end of url, display just the web page
                return(0);
        }
        if (strncmp("/t1.js",str,6)==0){
                gPlen=print_t1js();
                return(10);
        }
        if (strncmp("/config",str,7)==0){
                gPlen=print_webpage_config();
                return(10);
        }
        if (strncmp("/cu?",str,4)==0){
                if (find_key_val(str,gStrbuf,STR_BUFFER_SIZE,"pw")){
                        urldecode(gStrbuf);
                        if (verify_password(gStrbuf)){
                                if (find_key_val(str,gStrbuf,STR_BUFFER_SIZE,"ip")){
                                        urldecode(gStrbuf);
                                        if (parse_ip(myip,gStrbuf)!=0){
                                                updateerr=1;
                                        }
                                }
                                if (find_key_val(str,gStrbuf,STR_BUFFER_SIZE,"ns")){
                                        urldecode(gStrbuf);
                                        if (parse_ip(ntpip,gStrbuf)!=0){
                                                updateerr=1;
                                        }
                                }
                                if (find_key_val(str,gStrbuf,STR_BUFFER_SIZE,"gw")){
                                        urldecode(gStrbuf);
                                        if (parse_ip(gwip,gStrbuf)!=0){
                                                updateerr=1;
                                        }
                                }
                                display_24hclock=0;
                                if (find_key_val(str,gStrbuf,STR_BUFFER_SIZE,"hh")){
                                        display_24hclock=1;
                                }
                                display_utcoffset=0;
                                if (find_key_val(str,gStrbuf,STR_BUFFER_SIZE,"uo")){
                                        display_utcoffset=1;
                                }
                                if (find_key_val(str,gStrbuf,STR_BUFFER_SIZE,"tz")){
                                        urldecode(gStrbuf);
                                        deldot(gStrbuf);
                                        i=atoi(gStrbuf);
                                        if (i> -140 && i < 140){
                                                hours_offset_to_utc=i;
                                        }else{
                                                updateerr=1;
                                        }
                                }
                                if (find_key_val(str,gStrbuf,STR_BUFFER_SIZE,"np")){
                                        urldecode(gStrbuf);
                                        strncpy(password,gStrbuf,8);
                                        password[8]='\0';
                                }
                                // parse_ip does store also partially
                                // wrong values if it returns an error
                                // but it is still better to inform the user
                                if (updateerr){
                                        gPlen=fill_tcp_data_p(buf,http200ok(),PSTR("<a href=/config>Error</a>"));
                                        return(10);
                                }
                                // store in eeprom:
                                eeprom_write_byte((uint8_t *)0x0,19); // magic number
                                eeprom_write_block((uint8_t *)myip,(void *)1,sizeof(myip));
                                eeprom_write_block((uint8_t *)gwip,(void *)6,sizeof(gwip));
                                eeprom_write_block((uint8_t *)ntpip,(void *)11,sizeof(ntpip));
                                eeprom_write_byte((uint8_t *)16,hours_offset_to_utc);
                                eeprom_write_block((char *)password,(void *)19,sizeof(password));
                                eeprom_write_byte((uint8_t *)30,display_utcoffset);
                                eeprom_write_byte((uint8_t *)31,display_24hclock);
                                prev_minutes=99; // refresh the full display
                                return(1);
                        }
                }
        }
        return(-1); // Unauthorized
}

void print_time_to_lcd(void)
{
        char day[16];
        char clock[12];
        uint8_t minutes;

        // returns day and time-string in seperate variables:
        minutes=gmtime(time+(int32_t)+360*(int32_t)hours_offset_to_utc,display_24hclock,day,clock);
        if (prev_minutes!=minutes){
                // update complete display including day
                lcd_clrscr();
                lcd_gotoxy(0,1);
                lcd_puts(day);
                // update complete display
        }
        // write first line
        lcd_gotoxy(0,0);
        lcd_puts(clock);
        lcd_gotoxy(11,0);
        if (display_utcoffset){
                if (hours_offset_to_utc>=0){
                        lcd_puts_P("+");
                }
                itoa(hours_offset_to_utc,gStrbuf,10);
                adddotifneeded(gStrbuf);
                lcd_puts(gStrbuf);
        }else{
                lcd_puts_P("   ");
        }
        // write to first line
        lcd_gotoxy(15,0);
        lcd_putc(lindicator);
        prev_minutes=minutes;
        // before every hour
        if (minutes==57 && update_at_58_avoid_duplicates==0){  
                client_gw_arp_refresh();  // causes a refresh arp for the GW mac
                update_at_58_avoid_duplicates=1;
        }
        if (minutes==58 && update_at_58_avoid_duplicates==1){  
                // mark that we will wait for new
                // ntp update
                haveNTPanswer=2;
                send_ntp_req_from_idle_loop=1;
                update_at_58_avoid_duplicates=2;
        }
        // we need this update_at_58_avoid_duplicates variable
        // otherwise we will send every second an arp or ntp request
        // in that minute
        if (minutes<56){
                update_at_58_avoid_duplicates=0;
        }
}

// interrupt, step seconds counter
ISR(TIMER1_COMPA_vect){
        time++;
        lcd_needs_update++;
        if (LEDISOFF){
                LEDON;
        }else{
                LEDOFF;
        }
}

// Generate a 1s clock signal as interrupt form the 12.5MHz system clock
// Since we have that 1024 prescaler we do not really generate a second
// by 1.00000256000655361677s in other words we should add one second
// to our counter every 108 hours. That is however not needed as we
// update via ntp every hour. Note also that a non calibrated crystal
// osciallator may have an error in that order already. 
void clock_init(void)
{
        /* write high byte first for 16 bit register access: */
        TCNT1H=0;  /* set counter to zero*/
        TCNT1L=0;
        // Mode 4 table 14-4 page 132. CTC mode and top in OCR1A
        // WGM13=0, WGM12=1, WGM11=0, WGM10=0
        TCCR1A=(0<<COM1B1)|(0<<COM1B0)|(0<<WGM11);
        TCCR1B=(1<<CS12)|(1<<CS10)|(1<<WGM12)|(0<<WGM13); // crystal clock/1024

        // divide crystal clock by 16+1:
        //ICR1H=0;
        //ICR1L=16; // top for counter
        // At what value to cause interrupt. You can use this for calibration
        // of the clock. Theoretical value for 25MHz: 12207=0x2f and 0xaf
        // The crystal I use seems to tick a little slower I use 12206=0x2f and 0xae
        OCR1AH=0x2f;
        OCR1AL=0xae;
        // interrupt mask bit:
        TIMSK1 = (1 << OCIE1A);
}

int main(void){
        int8_t cmd;
        uint16_t pktlen;
        // set the clock speed to "no pre-scaler" (8MHz with internal osc or 
        // full external speed)
        // set the clock prescaler. First write CLKPCE to enable setting of clock the
        // next four instructions.
        CLKPR=(1<<CLKPCE); // change enable
        CLKPR=0; // "no pre-scaler"
        _delay_loop_1(0); // 60us

        // enable PB0, as input, config reset
        DDRB&= ~(1<<DDB0);
        PORTB|= (1<<PINB0); // internal pullup resistor on, jumper goes to ground
        /*initialize enc28j60*/
        enc28j60Init(mymac);
        enc28j60clkout(2); // change clkout from 6.25MHz to 12.5MHz
        _delay_loop_1(0); // 60us
        
        // lcd display:
        lcd_init(LCD_DISP_ON);
        lcd_clrscr();

        clock_init();

        /* Magjack leds configuration, see enc28j60 datasheet, page 11 */
        // LEDB=yellow LEDA=green
        //
        // 0x476 is PHLCON LEDA=links status, LEDB=receive/transmit
        // enc28j60PhyWrite(PHLCON,0b0000 0100 0111 01 10);
        enc28j60PhyWrite(PHLCON,0x476);
        
        DDRB|= (1<<DDB1); // LED, enable PB1, LED as output
        LEDOFF;

        // read eeprom values unless there is a jumper between PB0 and GND:
        if (!bit_is_clear(PINB,PINB0)){
                if (eeprom_read_byte((uint8_t *)0x0) == 19){
                        // ok magic number matches accept values
                        eeprom_read_block((uint8_t *)myip,(void *)1,sizeof(myip));
                        eeprom_read_block((uint8_t *)gwip,(void *)6,sizeof(gwip));
                        eeprom_read_block((uint8_t *)ntpip,(void *)11,sizeof(ntpip));
                        hours_offset_to_utc=(int8_t)eeprom_read_byte((uint8_t *)16);
                        eeprom_read_block((char *)password,(void *)19,sizeof(password));
                        password[8]='\0'; // make sure it is terminated, should not be necessary
                        display_utcoffset=(int8_t)eeprom_read_byte((uint8_t *)30);
                        display_24hclock=(int8_t)eeprom_read_byte((uint8_t *)31);
                }
        }
        
        //init the web server ethernet/ip layer:
        init_ip_arp_udp_tcp(mymac,myip,MYWWWPORT);
        // init the client code:
        client_set_gwip(gwip);  // e.g internal IP of dsl router
        waiting_gw=1;
        lcd_clrscr();
        lcd_puts_P("waiting for");
        lcd_gotoxy(0,1);
        lcd_puts_P("GW IP");
        //
        sei(); // interrupt on
        //
        while(1){
                pktlen=enc28j60PacketReceive(BUFFER_SIZE, buf);
                // handle ping and wait for a tcp packet
                gPlen=packetloop_icmp_tcp(buf,pktlen);
                if(gPlen==0){
                        goto UDP;
                }
                        
                if (strncmp("GET ",(char *)&(buf[gPlen]),4)!=0){
                        // head, post and other methods:
                        //
                        // for possible status codes see:
                        // http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html
                        gPlen=http200ok();
                        gPlen=fill_tcp_data_p(buf,gPlen,PSTR("<h1>200 OK</h1>"));
                        goto SENDTCP;
                }
                cmd=analyse_get_url((char *)&(buf[gPlen+4]));
                // for possible status codes see:
                // http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html
                if (cmd==-1){
                        gPlen=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 401 Unauthorized\r\nContent-Type: text/html\r\n\r\n<h1>401 Unauthorized</h1>"));
                        goto SENDTCP;
                }
                if (cmd==1){
                        gPlen=print_webpage_ok();
                        goto SENDTCP;
                }
                if (cmd==10){
                        goto SENDTCP;
                }
                gPlen=print_webpage();
                //
SENDTCP:
                www_server_reply(buf,gPlen); // send data
                continue;
                // tcp port www end --- udp start
UDP:
                // UDP/NTP protocol handling and idel tasks
                // check if ip packets are for us:
                if(eth_type_is_ip_and_my_ip(buf,pktlen)==0){
                        // we are idle here (no incomming packet to process).
                        // In other words these are our background tasks:
                        if (haveNTPanswer && send_ntp_req_from_idle_loop && client_waiting_gw()==0){
                                ntpclientportL++; // new src port
                                send_ntp_req_from_idle_loop=0;
                                client_ntp_request(buf,ntpip,ntpclientportL);
                        }
                        if (haveNTPanswer==0 && lcd_needs_update>1 ){
                                lcd_clrscr();
                                lcd_puts_P("waiting for");
                                lcd_gotoxy(0,1);
                                lcd_puts_P("NTP srv");
                                if (enc28j60linkup()){
                                        ntpclientportL++; // new src port
                                        client_ntp_request(buf,ntpip,ntpclientportL);
                                }
                                lcd_needs_update=0;
                                continue;
                        }
                        if (waiting_gw==1 && lcd_needs_update>1 && enc28j60linkup()){
                                client_gw_arp_refresh();
                                lcd_needs_update=0;
                                continue;
                        }
                        // -- update the LCD
                        if (haveNTPanswer && lcd_needs_update){
                                lindicator=' ';
                                if (haveNTPanswer!=1){
                                        lindicator='|';
                                }
                                if (!enc28j60linkup()){
                                        lindicator='/';
                                }
                                lcd_needs_update=0;
                                print_time_to_lcd();
                        }
                        continue;
                }
                // ntp src port must be 123 (0x7b):
                if (buf[IP_PROTO_P]==IP_PROTO_UDP_V&&buf[UDP_SRC_PORT_H_P]==0&&buf[UDP_SRC_PORT_L_P]==0x7b){
                        if (client_ntp_process_answer(buf,&time,ntpclientportL)){
                                LEDOFF;
                                // convert to unix time:
                                if ((time & 0x80000000UL) ==0){
                                        // 7-Feb-2036 @ 06:28:16 UTC it will wrap:
                                        time+=2085978496;
                                }else{
                                        // from now until 2036:
                                        time-=GETTIMEOFDAY_TO_NTP_OFFSET;
                                }
                                haveNTPanswer=1;
                        }
                }
        }
        return (0);
}
