/*********************************************
 * vim:sw=8:ts=8:si:et
 * To use the above modeline in vim you must have "set modeline" in your .vimrc
 * Author: Guido Socher
 * Copyright: GPL V2
 * See http://www.gnu.org/licenses/gpl.html
 *
 * NTP clock (standalone ntp client) with web interface for configuration of the clock
 * Chip type           : Atmega168 with ENC28J60
 * Note: there is a version number in the text. Search for tuxgraphics
 *********************************************/
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>
#include "ip_arp_udp_tcp.h"
#include "enc28j60.h"
#include "timeout.h"
#include "avr_compat.h"
#include "net.h"
#include "timeconversions.h"
#include "lcd.h"

/* set output to Vcc, LED off */
#define LEDOFF PORTB|=(1<<PB1)
/* set output to GND, red LED on */
#define LEDON PORTB&=~(1<<PB1);
// --------------- modify start
// please modify the following two lines. mac and ip have to be unique
// in your local area network. You can not have the same numbers in
// two devices:
static uint8_t mymac[6] = {0x54,0x55,0x58,0x10,0x00,0x28};
// how did I get the mac addr? Translate the first 3 numbers into ascii is: TUX
static uint8_t myip[4] = {10,0,0,28};
// if you have a NTP server in the local lan (=no GW) then set gwip to the same value
// as ntpip:
static uint8_t gwip[4] = {10,0,0,2};
// change summer/winter time and your timezone here (utc +1 is Germany, France etc... in winter):
int8_t hours_offset_to_utc=1;  
//
// --------------- modify stop
// time.apple.com:
static uint8_t ntpip[4] = {17,254,0,31};
// timeserver.rwth-aachen.de
//static uint8_t ntpip[4] = {134,130,4,17};
// time.nrc.ca
//static uint8_t ntpip[4] = {132,246,168,148};
//
static uint8_t ledstate=0;
static uint8_t ntpclientportL=0;
// listen port for tcp/www (max range 1-254)
#define MYWWWPORT 80 

#define BUFFER_SIZE 560
static uint8_t buf[BUFFER_SIZE+1];
// this is were we keep time (in unix gmtime format):
static uint32_t time=0;
// we do not update LCD from interrupt, we do it when we have time (no ip packet)
static uint8_t lcd_needs_update=1;  
// global string buffer
#define STR_BUFFER_SIZE 18
static char strbuf[STR_BUFFER_SIZE+1];
//
static char password[8]="secret"; // must be a-z and 0-9

//
uint8_t verify_password(char *str)
{
        // a simple password/cookie:
        if (strncmp(password,str,6)==0){
                return(1);
        }
        return(0);
}

// search for a string of the form key=value in
// a string that looks like q?xyz=abc&uvw=defgh HTTP/1.1\r\n
//
// The returned value is stored in the global var strbuf
uint8_t find_key_val(char *str,char *key)
{
        uint8_t found=0;
        uint8_t i=0;
        char *kp;
        kp=key;
        while(*str &&  *str!=' ' && found==0){
                if (*str == *kp){
                        kp++;
                        if (*kp == '\0'){
                                str++;
                                kp=key;
                                if (*str == '='){
                                        found=1;
                                }
                        }
                }else{
                        kp=key;
                }
                str++;
        }
        if (found==1){
                // copy the value to a buffer and terminate it with '\0'
                while(*str &&  *str!=' ' && *str!='&' && i<STR_BUFFER_SIZE){
                        strbuf[i]=*str;
                        i++;
                        str++;
                }
                strbuf[i]='\0';
        }
        // return the length of the value
        return(i);
}

// convert a single hex digit character to its integer value
unsigned char h2int(char c)
{
        if (c >= '0' && c <='9'){
                return((unsigned char)c - '0');
        }
        if (c >= 'a' && c <='f'){
                return((unsigned char)c - 'a' + 10);
        }
        if (c >= 'A' && c <='F'){
                return((unsigned char)c - 'A' + 10);
        }
        return(0);
}

// decode a url string e.g "hello%20joe" or "hello+joe" becomes "hello joe" 
void urldecode(char *urlbuf)
{
        char c;
        char *dst;
        dst=urlbuf;
        while ((c = *urlbuf)) {
                if (c == '+') c = ' ';
                if (c == '%') {
                        urlbuf++;
                        c = *urlbuf;
                        urlbuf++;
                        c = (h2int(c) << 4) | h2int(*urlbuf);
                }
                *dst = c;
                dst++;
                urlbuf++;
        }
        *dst = '\0';
}

// take string in dot-ed notation and make a binary IP addr array
void str2ip(uint8_t *resultip,char *str)
{
        uint8_t i=0;
        uint8_t ip[4];
        char *strptr;
        while(*str && i <4){
                // set to first digit (clean leading garbabge:
                while(!isdigit(*str) && *str){
                        str++;
                }
                strptr=str;
                while(isdigit(*str) && *str){
                        str++;
                }
                if (*str){
                        // terminate string unless we are at the end
                        // generally we will replace the dot with a \0
                        *str='\0'; 
                        str++;
                }
                ip[i]=atoi(strptr);
                i++;
        }
        if (i==4){
                // result ok, copy
                i=0;
                while(i<4){
                        resultip[i]=ip[i];
                        i++;
                }
        }
}

// take binary IP addr array and display in dot-ed notation
void ip2str(char *resultstr,uint8_t *ip)
{
        uint8_t i=0;
        uint8_t j=0;
        while(i<4){
                itoa((int)ip[i],&resultstr[j],10);
                // search end of str:
                while(resultstr[j]){j++;}
                resultstr[j]='.';
                j++;
                i++;
        }
        j--;
        resultstr[j]='\0';
}

// takes a string of the form password/commandNumber and analyse it
// return values: -1 invalid password, or invalid page
//                 1 just display current settings (no password was given)
//                 2 values upated
int8_t analyse_get_url(char *str)
{
        int8_t i;
        if (strncmp("config",str,6)!=0){
                return(-1);
        }
        if (find_key_val(str,"fd")){
                if (find_key_val(str,"pw")){
                        urldecode(strbuf);
                        if (verify_password(strbuf)==0){
                                return(-1);
                        }
                }else{
                        return(-1);
                }
        }else{
                // no pw found just display values
                return(1);
        }
        // pw is OK now store the values.
        if (find_key_val(str,"ip")){
                str2ip(myip,strbuf);
        }
        if (find_key_val(str,"gw")){
                str2ip(gwip,strbuf);
        }
        if (find_key_val(str,"ns")){
                str2ip(ntpip,strbuf);
        }
        if (find_key_val(str,"tz")){
                urldecode(strbuf);
                i=atoi(strbuf);
                if (i> -14 && i < 14){
                        hours_offset_to_utc=i;
                }
        }
        if (find_key_val(str,"np")){
                urldecode(strbuf);
                strncpy(password,strbuf,7);
                password[7]='\0';
        }
        // store in eeprom:
        eeprom_write_byte((uint8_t *)0x0,19); // magic number
        eeprom_write_block((uint8_t *)myip,(void *)1,sizeof(myip));
        eeprom_write_block((uint8_t *)gwip,(void *)6,sizeof(gwip));
        eeprom_write_block((uint8_t *)ntpip,(void *)11,sizeof(ntpip));
        eeprom_write_byte((uint8_t *)16,hours_offset_to_utc);
        eeprom_write_block((char *)password,(void *)18,sizeof(password));

        return(2);
}


// prepare the webpage by writing the data to the tcp send buffer
uint16_t print_webpage(uint8_t *buf)
{
        uint16_t plen;
        plen=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nPragma: no-cache\r\n\r\n"));
        plen=fill_tcp_data_p(buf,plen,PSTR("<pre>AVR NTP clock, config\n\n<form action=/config method=get>Clock own IP: <input type=text name=ip value="));
        ip2str(strbuf,myip);
        plen=fill_tcp_data(buf,plen,strbuf);
        plen=fill_tcp_data_p(buf,plen,PSTR(">\nDefault GW IP:<input type=text name=gw value="));
        ip2str(strbuf,gwip);
        plen=fill_tcp_data(buf,plen,strbuf);
        plen=fill_tcp_data_p(buf,plen,PSTR(">\nNTP server IP:<input type=text name=ns value="));
        ip2str(strbuf,ntpip);
        plen=fill_tcp_data(buf,plen,strbuf);
        plen=fill_tcp_data_p(buf,plen,PSTR(">\nOffset to UTC:<input type=text name=tz value="));
        if (hours_offset_to_utc>=0){
                strbuf[0]='+';
        }
        itoa(hours_offset_to_utc,&(strbuf[1]),10);
        plen=fill_tcp_data(buf,plen,strbuf);
        plen=fill_tcp_data_p(buf,plen,PSTR(">\nNew password: <input type=text name=np>\n\nPassword:     <input type=password name=pw><input type=hidden name=fd value=1>\n<input type=submit value=apply></form>\n</pre>"));
        return(plen);
}

// interrupt, step seconds counter
ISR(TIMER1_COMPA_vect){
        time++;
        lcd_needs_update++;
        if (ledstate){
                ledstate=0;
                LEDOFF;
        }else{
                ledstate=1;
                LEDON;
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
        // The crystal I use seems to tick a little slower I use 12205=0x2f and 0xad
        OCR1AH=0x2f;
        OCR1AL=0xad;
        // interrupt mask bit:
        TIMSK1 = (1 << OCIE1A);
}

int main(void){

        
        uint16_t plen;
        uint16_t dat_p;
        uint8_t haveGWip=0;
        uint8_t haveNTPanswer=0; // 0 no initial ntp answer, 1= have answer, 2,3=sent new reqest
        uint8_t minutes=0;
        uint8_t prev_minutes=99; // initial value must be someting higher than 59
        int8_t cmd;
        char day[22];
        char clock[22];
        char tzstr[4];
        char lindicator='/'; // link indicator on the right
        
        // set the clock speed to "no pre-scaler" (8MHz with internal osc or 
        // full external speed)
        // set the clock prescaler. First write CLKPCE to enable setting of clock the
        // next four instructions.
        CLKPR=(1<<CLKPCE); // change enable
        CLKPR=0; // "no pre-scaler"
        delay_ms(1);

        /* enable PD2/INT0, as input */
        DDRD&= ~(1<<DDD2);

        // enable PB0, as input, tuxgraphics IP autoconfig 
        DDRB&= ~(1<<DDB0);
        PORTB|= (1<<PINB0); // internal pullup resistor on, jumper goes to ground

        /*initialize enc28j60*/
        enc28j60Init(mymac);
        enc28j60clkout(2); // change clkout from 6.25MHz to 12.5MHz
        delay_ms(10);
        
        // LED
        /* enable PB1, LED as output */
        DDRB|= (1<<DDB1);
        LEDOFF;

        // read eeprom values unless there is a jumper between PB0 and GND:
        if (!bit_is_clear(PINB,PINB0)){
                if (eeprom_read_byte((uint8_t *)0x0) == 19){
                        // ok magic number matches accept values
                        eeprom_read_block((uint8_t *)myip,(void *)1,sizeof(myip));
                        eeprom_read_block((uint8_t *)gwip,(void *)6,sizeof(gwip));
                        eeprom_read_block((uint8_t *)ntpip,(void *)11,sizeof(ntpip));
                        hours_offset_to_utc=eeprom_read_byte((uint8_t *)16);
                        eeprom_read_block((char *)password,(void *)18,sizeof(password));
                        password[7]='\0'; // make sure it is terminated, should not be necessary
                }
        }
        
        /* Magjack leds configuration, see enc28j60 datasheet, page 11 */
        // LEDB=yellow LEDA=green
        //
        // 0x476 is PHLCON LEDA=links status, LEDB=receive/transmit
        // enc28j60PhyWrite(PHLCON,0b0000 0100 0111 01 10);
        enc28j60PhyWrite(PHLCON,0x476);
        delay_ms(20);
        // lcd display:
        lcd_init(LCD_DISP_ON);
        lcd_clrscr();
        

        //init the ethernet/ip layer:
        init_ip_arp_udp_tcp(mymac,myip,MYWWWPORT);
        
        clock_init();
        lcd_puts_P("OK");

        // just use variable haveGWip as a loop counter here to save memory
        while(haveGWip<30){
                if (enc28j60linkup()){
                        haveGWip=99;
                }
                delay_ms(400);
        }
        haveGWip=0;

        sei(); // interrupt on

        while(1){
                // get the next new packet:
                plen = enc28j60PacketReceive(BUFFER_SIZE, buf);

                /*plen will ne unequal to zero if there is a valid 
                 * packet (without crc error) */
                if(plen==0){
                        // update the lcd display periodically when there is
                        // nothing else to do
                        if (lcd_needs_update){
                                if (enc28j60linkup()){
                                        lindicator=' ';
                                        if (haveNTPanswer>=2){
                                                // have link but no up-to date ntp answer
                                                lindicator='|';
                                        }
                                        if (lcd_needs_update >3){ // 3 seconds
                                                if (haveGWip==0){
                                                        lcd_clrscr();
                                                        lcd_puts_P("waiting for");
                                                        lcd_gotoxy(0,1);
                                                        lcd_puts_P("GW IP");
                                                        lcd_needs_update=0;
                                                        client_arp_whohas(buf,gwip);
                                                        continue;
                                                }
                                                if (haveNTPanswer==0){
                                                        lcd_clrscr();
                                                        lcd_puts_P("waiting for");
                                                        lcd_gotoxy(0,1);
                                                        lcd_puts_P("NTP answer");
                                                        lcd_needs_update=0;
                                                        ntpclientportL++;
                                                        client_ntp_request(buf,ntpip,ntpclientportL);
                                                        continue;
                                                }
                                        }
                                }else{
                                        lindicator='/';
                                        if (haveGWip==0){
                                                lcd_clrscr();
                                                lcd_puts_P("Eth-link down");
                                                lcd_needs_update=0;
                                                continue;
                                        }
                                }
                                // link up or link down
                                if (haveNTPanswer>0){
                                        minutes=gmtime(time+3600*hours_offset_to_utc,day,clock);
                                        if (prev_minutes!=minutes){
                                                // update complete display
                                                lcd_clrscr();
                                                lcd_puts(day);
                                        }
                                        lcd_gotoxy(0,1);
                                        lcd_puts(clock);
                                        if (prev_minutes!=minutes){
                                                // update complete display
                                                lcd_puts_P(" (");
                                                if (hours_offset_to_utc>=0){
                                                        lcd_puts_P("+");
                                                }
                                                itoa(hours_offset_to_utc,tzstr,10);
                                                lcd_puts(tzstr);
                                                lcd_puts_P(")");
                                        }
                                        lcd_gotoxy(14,1);
                                        lcd_putc(lindicator);
                                        // before every hour
                                        if (minutes==58){  
                                                // mark that we will wait for new
                                                // ntp update
                                                haveNTPanswer=2;
                                        }
                                        if (minutes==59 && haveNTPanswer==2){ 
                                                if (lindicator!='/'){
                                                        ntpclientportL++;
                                                        client_ntp_request(buf,ntpip,ntpclientportL);
                                                }
                                                // ask only once in the 59th minute:
                                                haveNTPanswer=3;
                                        }
                                        prev_minutes=minutes;
                                        lcd_needs_update=0;
                                }
                        }
                        continue;
                }
                        
                // arp is broadcast if unknown but a host may also
                // verify the mac address by sending it to 
                // a unicast address.
                if(eth_type_is_arp_and_my_ip(buf,plen)){
                        if (eth_type_is_arp_req(buf)){
                                make_arp_answer_from_request(buf);
                        }
                        if (eth_type_is_arp_reply(buf)){
                                if (client_store_gw_mac(buf,gwip)){
                                        haveGWip=1;
                                }
                        }
                        continue;
                }

                // check if ip packets are for us:
                if(eth_type_is_ip_and_my_ip(buf,plen)==0){
                        continue;
                }
                // led----------
                if(buf[IP_PROTO_P]==IP_PROTO_ICMP_V && buf[ICMP_TYPE_P]==ICMP_TYPE_ECHOREQUEST_V){
                        // a ping packet, let's send pong
                        make_echo_reply_from_request(buf,plen);
                        continue;
                }
                // tcp port www start, compare only the lower byte
                if (buf[IP_PROTO_P]==IP_PROTO_TCP_V&&buf[TCP_DST_PORT_H_P]==0&&buf[TCP_DST_PORT_L_P]==MYWWWPORT){
                        if (buf[TCP_FLAGS_P] & TCP_FLAGS_SYN_V){
                                make_tcp_synack_from_syn(buf);
                                // make_tcp_synack_from_syn does already send the syn,ack
                                continue;
                        }
                        if (buf[TCP_FLAGS_P] & TCP_FLAGS_ACK_V){
                                init_len_info(buf); // init some data structures
                                cmd=0;
                                // we can possibly have no data, just ack:
                                dat_p=get_tcp_data_pointer();
                                if (dat_p==0){
                                        if (buf[TCP_FLAGS_P] & TCP_FLAGS_FIN_V){
                                                // finack, answer with ack
                                                make_tcp_ack_from_any(buf);
                                        }
                                        // just an ack with no data, wait for next packet
                                        continue;
                                }
                                if (strncmp("GET ",(char *)&(buf[dat_p]),4)!=0){
                                        // head, post and other methods:
                                        //
                                        // for possible status codes see:
                                        // http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html
                                        plen=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n<h1>200 OK</h1>"));
                                        goto SENDTCP;
                                }
                                if (strncmp("/ ",(char *)&(buf[dat_p+4]),2)==0){
                                        plen=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n"));
                                        plen=fill_tcp_data_p(buf,plen,PSTR("<h2>AVR NTP clock</h2><pre>\n"));
                                        if (haveNTPanswer){
                                                gmtime(time+3600*hours_offset_to_utc,day,clock);
                                                plen=fill_tcp_data(buf,plen,day);
                                                plen=fill_tcp_data(buf,plen,"\n");
                                                plen=fill_tcp_data(buf,plen,clock);
                                                plen=fill_tcp_data_p(buf,plen,PSTR(" (UTC "));
                                                if (hours_offset_to_utc>=0){
                                                        plen=fill_tcp_data(buf,plen,"+");
                                                }
                                                itoa(hours_offset_to_utc,tzstr,10);
                                                plen=fill_tcp_data(buf,plen,tzstr);
                                                plen=fill_tcp_data_p(buf,plen,PSTR(")\n"));
                                                if (haveNTPanswer>1){
                                                        plen=fill_tcp_data_p(buf,plen,PSTR("\nLast ntp sync is older than 1 hour\n"));
                                                }
                                                plen=fill_tcp_data_p(buf,plen,PSTR("</pre>\n<br><a href=\"./\">refresh</a>\n"));
                                        }else{
                                                plen=fill_tcp_data_p(buf,plen,PSTR("Waiting for NTP answer..."));
                                        }
                                        goto SENDTCP;
                                }
                                cmd=analyse_get_url((char *)&(buf[dat_p+5]));
                                // for possible status codes see:
                                // http://www.w3.org/Protocols/rfc2616/rfc2616-sec10.html
                                if (cmd==-1){
                                        plen=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 401 Unauthorized\r\nContent-Type: text/html\r\n\r\n<h1>401 Unauthorized</h1>"));
                                        goto SENDTCP;
                                }
                                if (cmd==1){
                                        plen=print_webpage(buf);
                                        goto SENDTCP;
                                }
                                if (cmd==2){
                                        plen=fill_tcp_data_p(buf,0,PSTR("HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\n"));
                                        plen=fill_tcp_data_p(buf,plen,PSTR("<p>OK, values updated.</p>\n"));

                                }
SENDTCP:
                                make_tcp_ack_from_any(buf); // send ack for http get
                                make_tcp_ack_with_data(buf,plen); // send data
                                if (cmd==2){
                                        init_ip_arp_udp_tcp(mymac,myip,MYWWWPORT);
                                        haveGWip=0;
                                        haveNTPanswer=0; 
                                        prev_minutes=99;
                                        lcd_clrscr();
                                        lcd_puts_P("updating...");
                                }
                                continue;
                        }

                }
                // tcp port www end
                //
                // udp start, src port must be 123 (0x7b):
                if (buf[IP_PROTO_P]==IP_PROTO_UDP_V&&buf[UDP_SRC_PORT_H_P]==0&&buf[UDP_SRC_PORT_L_P]==0x7b){
                        if (client_ntp_process_answer(buf,&time,ntpclientportL)){
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
