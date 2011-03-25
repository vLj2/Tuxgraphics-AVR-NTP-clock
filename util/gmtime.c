#include <stdio.h>
/*
 * gmtime - convert the calendar time into broken down time
 */

#define	YEAR0		1900		/* the first year */
#define	EPOCH_YR	1970		/* EPOCH = Jan 1 1970 00:00:00 */
#define	SECS_DAY	86400  //(24L * 60L * 60L)
#define	LEAPYEAR(year)	(!((year) % 4) && (((year) % 100) || !((year) % 400)))
#define	YEARSIZE(year)	(LEAPYEAR(year) ? 366 : 365)

const char *day_abbrev[7] = {"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};

struct tm {
                     int     tm_sec;         /* seconds */
                     int     tm_min;         /* minutes */
                     int     tm_hour;        /* hours */
                     int     tm_mday;        /* day of the month */
                     int     tm_mon;         /* month */
                     int     tm_year;        /* year */
                     int     tm_wday;        /* day of the week */
                     int     tm_yday;        /* day in the year */
                     int     tm_isdst;       /* daylight saving time */
             };
// isleapyear = 0-1
// month=0-11
// return: how many days a month has
//
// We could do this if ram was no issue:
int monthlen1(isleapyear,month){
const int mlen[2][12] = {
		{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
		{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
	};
	return(mlen[isleapyear][month]);
}
//
int monthlen(isleapyear,month){
	if(month==1){
		return(28+isleapyear);
	}
	if (month>6){
		month--;
	}
	if (month%2==1){
		return(30);
	}
	return(31);
}

struct tm *
gmtime(const unsigned int timer)
{
	static struct tm br_time;
	register struct tm *timep = &br_time;
	unsigned int time = timer;
	register unsigned long dayclock, dayno;
	unsigned int year = EPOCH_YR;

	dayclock = (unsigned long)time % SECS_DAY;
	dayno = (unsigned long)time / SECS_DAY;

	timep->tm_sec = dayclock % 60;
	timep->tm_min = (dayclock % 3600) / 60;
	timep->tm_hour = dayclock / 3600;
	timep->tm_wday = (dayno + 4) % 7;	/* day 0 was a thursday */
	while (dayno >= YEARSIZE(year)) {
		dayno -= YEARSIZE(year);
		year++;
	}
	timep->tm_year = year - YEAR0;
	timep->tm_yday = dayno;
	timep->tm_mon = 0;
	while (dayno >= monthlen(LEAPYEAR(year),timep->tm_mon)) {
		dayno -= monthlen(LEAPYEAR(year),timep->tm_mon);
		timep->tm_mon++;
	}
	timep->tm_mday = dayno + 1;
	timep->tm_isdst = 0;
	printf("gmtime %d-%02d-%02d %02d:%02d:%02d %s\n",year,timep->tm_mon+1,timep->tm_mday,timep->tm_hour,timep->tm_min,timep->tm_sec,day_abbrev[timep->tm_wday]);

	return timep;
}

int main(int argc, char *argv[])
{
	unsigned int t;
	if (argc!=2){ // one argument is must
		printf("USAGE: gmtime Sec_since_19700101\n");
		return(1);
	}
	sscanf(argv[1], "%u", &t);
	printf ("OK: Sec since 1970-01-01 00:00= %u\n",t);
	gmtime(t);
	return(0);
}
