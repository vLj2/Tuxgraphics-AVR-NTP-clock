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
#include "ip_arp_udp_tcp.h"
#include "websrv_help_functions.h"
#include "enc28j60.h"
#include "timeout.h"
#include "net.h"

// Note: This software implements a web server and from where you can
// send a request to an ntp server.
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
// ntp1.curie.fr  193.49.205.19
// ntp3.curie.fr  193.49.205.18
// tick.utoronto.ca 128.100.56.135
// ntp.nasa.gov 198.123.30.132
// timekeeper.isi.edu 128.9.176.30
// tick.mit.edu 18.145.0.30
//
// -------- never change anything below this line ---------
// global string buffer
#define STR_BUFFER_SIZE 20
static char gStrbuf[STR_BUFFER_SIZE+1];
static uint16_t gPlen;

static char ntp_unix_time_str[11];
static uint8_t ntpclientportL=0; // lower 8 bytes of local port number
static uint8_t ntp_client_attempts=0;
static uint8_t haveNTPanswer=0;
static uint8_t send_ntp_req_from_idle_loop=0;
// this is were we keep time (in unix gmtime format):
// Note: this value may jump a few seconds when a new ntp answer comes.
// You need to keep this in mid if you build an alarm clock. Do not match
// on "==" use ">=" and set a state that indicates if you already triggered the alarm.
static uint32_t time=0;
#define GETTIMEOFDAY_TO_NTP_OFFSET 2208988800UL
// eth/ip buffer:
#define BUFFER_SIZE 590
static uint8_t buf[BUFFER_SIZE+1];

// set output to VCC, red LED off
#define LEDOFF PORTB|=(1<<PORTB1)
// set output to GND, red LED on
#define LEDON PORTB&=~(1<<PORTB1)
// to test the state of the LED
#define LEDISOFF PORTB&(1<<PORTB1)
// 

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
        // unix time to printable string
        plen=fill_tcp_data_p(buf,plen,PSTR("\
function t2s(n){\n\
var t = new Date(n*1000);\n\
document.write(t.toLocaleString());\n\
}\n\
"));
        return(plen);
}

uint16_t print_webpage_ok(void)
{
        uint16_t plen;
        plen=http200ok();
        plen=fill_tcp_data_p(buf,plen,PSTR("<a href=/>OK</a>"));
        return(plen);
}

uint16_t print_webpage_ntp_req(void)
{
        uint16_t plen;
        plen=http200ok();
        plen=fill_tcp_data_p(buf,plen,PSTR("<a href=/>[home]</a>"));
        plen=fill_tcp_data_p(buf,plen,PSTR("<h2>send ntp request</h2><pre>\n"));
        plen=fill_tcp_data_p(buf,plen,PSTR("ntp server: "));
        mk_net_str(gStrbuf,ntpip,4,'.',10);
        plen=fill_tcp_data(buf,plen,gStrbuf);
        plen=fill_tcp_data_p(buf,plen,PSTR("<form action=/r method=get>\n"));
        plen=fill_tcp_data_p(buf,plen,PSTR("<input type=submit value=\"send\">\n"));
        plen=fill_tcp_data_p(buf,plen,PSTR("</form>\n"));
        plen=fill_tcp_data_p(buf,plen,PSTR("</pre><br><hr>tuxgraphics.org"));
        return(plen);
}

// prepare the main webpage by writing the data to the tcp send buffer
uint16_t print_webpage(void)
{
        uint16_t plen;
        uint8_t i;
        plen=http200ok();
        plen=fill_tcp_data_p(buf,plen,PSTR("<a href=/m>[new ntp query]</a> "));
        plen=fill_tcp_data_p(buf,plen,PSTR("<script src=t1.js></script>"));
        plen=fill_tcp_data_p(buf,plen,PSTR("<h2>NTP response status</h2>\n<pre>\n"));
        // convert number to string:
        plen=fill_tcp_data_p(buf,plen,PSTR("\nGW mac known: "));
        if (client_waiting_gw()==0){
                plen=fill_tcp_data_p(buf,plen,PSTR("yes"));
        }else{
                plen=fill_tcp_data_p(buf,plen,PSTR("no"));
        }
        plen=fill_tcp_data_p(buf,plen,PSTR("\nNumber of requests: "));
        // convert number to string:
        itoa(ntp_client_attempts,gStrbuf,10);
        plen=fill_tcp_data(buf,plen,gStrbuf);
        plen=fill_tcp_data_p(buf,plen,PSTR("\nSuccessful answers: "));
        // convert number to string:
        itoa(haveNTPanswer,gStrbuf,10);
        plen=fill_tcp_data(buf,plen,gStrbuf);
        plen=fill_tcp_data_p(buf,plen,PSTR("\n\nTime received in UNIX sec since 1970:\n</pre>"));
        if (haveNTPanswer){
                i=10;
                // convert a 32bit integer into a printable string, avr-lib has
                // not function for that therefore we do it step by step:
                ntp_unix_time_str[i]='\0';
                while(time&&i){
                        i--;
                        ntp_unix_time_str[i]=(char)((time%10 & 0xf))+0x30;
                        time=time/10;
                }
                plen=fill_tcp_data(buf,plen,&ntp_unix_time_str[i]);
                plen=fill_tcp_data_p(buf,plen,PSTR(" [<script>t2s("));
                plen=fill_tcp_data(buf,plen,&ntp_unix_time_str[i]);
                plen=fill_tcp_data_p(buf,plen,PSTR(")</script>]"));
        }else{
                plen=fill_tcp_data_p(buf,plen,PSTR("none"));
        }
        plen=fill_tcp_data_p(buf,plen,PSTR("<br><hr>tuxgraphics.org"));
        return(plen);
}

// analyse the url given
// return values: are used to show different pages (see main program)
//
int8_t analyse_get_url(char *str)
{
        // the first slash:
        if (str[0] == '/' && str[1] == ' '){
                // end of url, display just the web page
                return(0);
        }
        if (strncmp("/t1.js",str,6)==0){
                gPlen=print_t1js();
                return(10);
        }
        if (strncmp("/m ",str,3)==0){
                gPlen=print_webpage_ntp_req();
                return(10);
        }
        if (strncmp("/r ",str,3)==0){
                // we can't call here client_ntp_request as this would mess up the buf variable
                // for our currently worked on web-page.
                send_ntp_req_from_idle_loop=1;
                return(1);
        }
        return(-1); // Unauthorized
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


        /*initialize enc28j60*/
        enc28j60Init(mymac);
        enc28j60clkout(2); // change clkout from 6.25MHz to 12.5MHz
        _delay_loop_1(0); // 60us
        
        /* Magjack leds configuration, see enc28j60 datasheet, page 11 */
        // LEDB=yellow LEDA=green
        //
        // 0x476 is PHLCON LEDA=links status, LEDB=receive/transmit
        // enc28j60PhyWrite(PHLCON,0b0000 0100 0111 01 10);
        enc28j60PhyWrite(PHLCON,0x476);
        
        DDRB|= (1<<DDB1); // LED, enable PB1, LED as output
        LEDOFF;

        //init the web server ethernet/ip layer:
        init_ip_arp_udp_tcp(mymac,myip,MYWWWPORT);
        // init the web client:
        client_set_gwip(gwip);  // e.g internal IP of dsl router
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
                // check if ip packets are for us:
                if(eth_type_is_ip_and_my_ip(buf,pktlen)==0){
                        // we are idle here:
                        if (send_ntp_req_from_idle_loop && client_waiting_gw()==0){
                                LEDON;
                                ntpclientportL++; // new src port
                                send_ntp_req_from_idle_loop=0;
                                client_ntp_request(buf,ntpip,ntpclientportL);
                                ntp_client_attempts++;
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
                                haveNTPanswer++;
                        }
                }
        }
        return (0);
}
