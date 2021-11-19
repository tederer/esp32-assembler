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

typedef struct {
   CommandBytes commandBytes;
   char*        errorMessage;
} Result;

/**
 * In case of a valid command Command.commandBytes contains the corresponding bytes and Command.errorMessage is NULL, 
 * otherwise Command.errorMessage points to an error message.
 */
Result getCommandBytesFor(const uint8_t *line);

#endif