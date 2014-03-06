/*********************************************
 * vim:sw=8:ts=8:si:et
 * To use the above modeline in vim you must have "set modeline" in your .vimrc
 * Author: Guido Socher 
 * Copyright: GPL V2
 *
 *********************************************/
//@{
#ifndef TIMECONVERSIONS_H
#define TIMECONVERSIONS_H

// Number of seconds between 1-Jan-1900 and 1-Jan-1970, unix time starts 1970
// and ntp time starts 1900.
#define GETTIMEOFDAY_TO_NTP_OFFSET 2208988800UL

extern uint8_t gmtime(const uint32_t time,uint8_t show24,char *day, char *clock);


#endif /* TIMECONVERSIONS_H */
//@}
