#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

void serial_init();
void serial_write(char c);
void serial_writestring(const char* str);
void serial_writehex(uint64_t n);
void serial_writedec(uint64_t n);

#endif // SERIAL_H 