#ifndef __TIME_UTILS_H
#define __TIME_UTILS_H

char *epoch_to_str(uint64_t epoch);
uint8_t *epoch_to_clock_str(uint64_t epoch, uint8_t *buf, bool displayMode24);

#endif
