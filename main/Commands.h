#ifndef assembler_commands_h
#define assembler_commands_h

#include <stdint.h>
#include <stdbool.h>

typedef struct {
   uint8_t byte0;
   uint8_t byte1;
   uint8_t byte2;
   uint8_t byte3;
} CommandBytes;

/**
 * In case of a valid command in line the corresponding bytes get put into bytes and returns true, otherwise false gets returned.
 */
bool getCommandBytesFor(const uint8_t *line, CommandBytes* bytes);

#endif