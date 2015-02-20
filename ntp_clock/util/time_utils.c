
#include "c_types.h"
#include "osapi.h"


#define IS_LEAP(year) (year%4 == 0)
#define SEC_IN_NON_LEAP (86400*365)
#define SEC_IN_LEAP (86400*366)
#define SEC_IN_YEAR(year) (IS_LEAP(year) ? SEC_IN_LEAP : SEC_IN_NON_LEAP)

char buf[30];

unsigned char calendar [] = {31, 28, 31, 30,31, 30, 31, 31,30, 31, 30, 31};
unsigned char calendar_leap [] = {31, 29, 31, 30,31, 30, 31, 31,30, 31, 30, 31};

unsigned char *get_calendar(int year) {
        return IS_LEAP(year) ? calendar_leap : calendar;
}

int ICACHE_FLASH_ATTR get_year(uint64_t *t) {
        int year=1970;
        while(*t>SEC_IN_YEAR(year)) {
                  *t-=SEC_IN_YEAR(year);
                  year++;
        }   
        return year;
}

int ICACHE_FLASH_ATTR get_month(uint64_t *t, int year) {
        unsigned char *cal = get_calendar(year);
        int i=0;
        while(*t > cal[i]*86400) {
                *t-=cal[i]*86400;
                i++;
        }   
        return i+1;
}

char * ICACHE_FLASH_ATTR epoch_to_str(uint64_t epoch) {
   int year=get_year(&epoch);
   unsigned char month=get_month(&epoch,year);
   unsigned char day=1+(epoch/86400);
   epoch=epoch%86400;
   unsigned char hour=epoch/3600;
   epoch%=3600;
   unsigned char min=epoch/60;
   unsigned char sec=epoch%60;
   

   os_sprintf(buf,"%02d:%02d:%02d %02d/%02d/%02d",hour,min,sec,month,day,year);
   return buf;
}

/*
 * Return a 5 byte string in the buffer provided which contains hours and minutes
 */
 
uint8_t * ICACHE_FLASH_ATTR epoch_to_clock_str(uint64_t epoch, uint8_t *buf, bool displayMode24) {
	int year=get_year(&epoch);
	unsigned char month=get_month(&epoch,year);
	unsigned char day=1+(epoch/86400);
	epoch=epoch%86400;
	unsigned char hour=epoch/3600;
	epoch%=3600;
	unsigned char min=epoch/60;
	if(displayMode24)
		os_sprintf(buf,"%02d%02d", hour, min);
	else{
		unsigned char hr12 = ((0 == hour) || (12 == hour)) ? 12 : hour % 12;
		os_sprintf(buf,"%2d%02d", hr12, min);
	}
	return buf;
}
