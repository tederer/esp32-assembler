#include <string.h>
#include <math.h>
#include <stdio.h>
#include <regex.h>

#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc_periph.h"
#include "driver/rtc_io.h"
#include "driver/uart.h"
#include "esp32/ulp.h"
#include "ulp_main.h"

#include "StringUtils.h"
#include "Commands.h"

#define SERIAL_PORT  UART_NUM_0
#define MILLIS(ms)   ((ms) * 1000)
#define LF           0x0d
#define CR           0x0a

#define ULP_PROGRAM_MAX_COMMAND_COUNT           50
#define ULP_PROGRAM_HEADER_SIZE_IN_BYTES        12
#define ULP_PROGRAM_COMMAND_SIZE_IN_BYTES       4
#define ULP_PROGRAM_HALT_COMMANDS_COUNT         2

static uint8_t ulpProgram[ULP_PROGRAM_HEADER_SIZE_IN_BYTES + (ULP_PROGRAM_MAX_COMMAND_COUNT + ULP_PROGRAM_HALT_COMMANDS_COUNT) * ULP_PROGRAM_COMMAND_SIZE_IN_BYTES];
// The reg_wr command disables the ULP timer to ensure that the ULP program gets executed only once (see technical reference manual "29.5 ULP Program Execution").
static char *HALT_COMMANDS[ULP_PROGRAM_HALT_COMMANDS_COUNT] = { "reg_wr 6, 24, 24, 0", "halt"};
static size_t nextCommandIndex = 0;
static bool userEnteredNewCommands = false;

static void appendHaltCommandsToUlpProgram(const uint8_t *program);
static void loadUlpProgram(const uint8_t *program);
static void startUlpProgram(size_t indexOfFirstCommand);
static void initSerialInterface();
static void handleCommands(void *parameters);
static void processNextLine(const uint8_t *line);
static void printCommands(const uint8_t *firstByteOfFirstCommand, size_t commandCount);
static void printUlpProgram(const uint8_t *programStart);
static void printRtcSlowMemory();
static void initializeUlpProgram();
static void setBytesInUlpProgram(size_t commandIndex, CommandBytes *commandBytes);
static void createVariable(const char *command);
static void createCommand(const char *command);
static bool runProgram(const char *command);
static void printHelp();
static bool regexMatches(const char *text, const char *pattern);

// ULP program binary according to https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/ulp.html?highlight=ulp%20magic#_CPPv415ulp_load_binary8uint32_tPK7uint8_t6size_t
struct UlpBinary {
   uint32_t magic;
   uint16_t textOffset;
   uint16_t textSize;
   uint16_t dataSize;
   uint16_t bssSize;
};

struct Command {
   char* pattern;
   void (*getBytes)(uint8_t*);
};

void app_main()
{
   //printUlpProgram(ulp_main_bin_start);
   initializeUlpProgram();
   xTaskCreate(handleCommands, "handle commands from serial interface", 4000, NULL, 10, NULL);
}

static void initializeUlpProgram() {
   printf("Initializing ULP program ...\n");
   struct UlpBinary* metaData = (struct UlpBinary*)ulpProgram;
   metaData->magic       = 0x00706c75;
   metaData->textOffset  = 12;
   metaData->textSize    = 0;
   metaData->dataSize    = 0;
   metaData->bssSize     = 0;

   Result noopCommand = getCommandBytesFor((uint8_t*)"nop");
   
   for(size_t commandIndex = 0; commandIndex < ULP_PROGRAM_MAX_COMMAND_COUNT; commandIndex++) {
      setBytesInUlpProgram(commandIndex, &(noopCommand.commandBytes));
   }

   nextCommandIndex = 0; 
   userEnteredNewCommands = false;     
}

static void appendHaltCommandsToUlpProgram(const uint8_t *program) {
   struct UlpBinary* metaData = (struct UlpBinary*)program;
   metaData->magic      = 0x00706c75;
   metaData->textOffset = 12;
   metaData->textSize   = (nextCommandIndex + ULP_PROGRAM_HALT_COMMANDS_COUNT) * ULP_PROGRAM_COMMAND_SIZE_IN_BYTES;
   metaData->dataSize   = 0;
   metaData->bssSize    = 0;

   size_t commandIndexOfFirstHaltCommand = nextCommandIndex;

   for(size_t index = 0; index < ULP_PROGRAM_HALT_COMMANDS_COUNT; index++) {
      Result command = getCommandBytesFor((uint8_t*)HALT_COMMANDS[index]);
      setBytesInUlpProgram(commandIndexOfFirstHaltCommand + index, &command.commandBytes);
   }
}

static void loadUlpProgram(const uint8_t *program) {
   printf("Loading your program into RTC memory ...\n");
   struct UlpBinary* metaData = (struct UlpBinary*)program;
   uint32_t programSizeInBytes = ULP_PROGRAM_HEADER_SIZE_IN_BYTES + metaData->textSize + metaData->dataSize + metaData->bssSize;
   ESP_ERROR_CHECK(ulp_load_binary(0, program, programSizeInBytes / sizeof(uint32_t)));
}

static void startUlpProgram(size_t indexOfFirstCommand)
{
   printf("Starting at command index %d.\n", indexOfFirstCommand);
   ESP_ERROR_CHECK(ulp_run(indexOfFirstCommand));
}

static void initSerialInterface() {
   uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
   };
   
   ESP_ERROR_CHECK(uart_param_config(SERIAL_PORT, &uart_config));
   // Set pins for UART0 (TX: IO4, RX: IO5, RTS: IO18, CTS: IO19)
   ESP_ERROR_CHECK(uart_set_pin(SERIAL_PORT, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
   ESP_ERROR_CHECK(uart_driver_install(SERIAL_PORT, 1024, 0, 0, NULL, 0));
}

static void handleCommands(void *parameters) {
   size_t readBytes;
   uint8_t buffer[2];
   size_t maxLineLength = 40;
   uint8_t line[maxLineLength + 1];
   size_t insertationPosition = 0;
   
   vTaskDelay(100 / portTICK_PERIOD_MS);
   initSerialInterface();
   
   while (true) {
      readBytes = uart_read_bytes(SERIAL_PORT, buffer, 1, 1000 / portTICK_PERIOD_MS);
      if (readBytes > 0) {
         if (buffer[0] != LF) {
            line[insertationPosition++] = buffer[0];
         } else {
            line[insertationPosition] = 0;
            processNextLine(line);
            insertationPosition = 0;
         }

         if (insertationPosition == maxLineLength) {
            line[maxLineLength] = 0;
            printf("ERROR: Maximum line length (%d) reached -> ignoring \"%s\".\n", maxLineLength, line);
            insertationPosition = 0;
         }
      }
   }
   
   vTaskDelete(NULL);
}

static void printHelp() {
   printf("\nIn addition to the ULP instructions (see https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/ulp_instruction_set.html), the following commands are supported:\n\n");
   printf("var(<value>)                stores <value> at the current command index\n");
   printf("run <indexOfFirstCommand>   executes your program and displays the memory used by it\n");
   printf("list                        displays the memory used by your program\n");
   printf("reset                       removes all alreay entered commands\n\n");
   printf("For further details visit https://github.com/tederer/esp32-assembler.\n\n");
}

static void processNextLine(const uint8_t *line) {
   size_t lineLength = strlen((const char*)line);
   uint8_t copyOfLine[lineLength];
   strcpy((char*)copyOfLine, (char*)line);
   char *trimmedLineInLowerCase = (char*)toLowerCase(trim(copyOfLine));

   if (regexMatches(trimmedLineInLowerCase, "run [0-9]+")) {
      if (runProgram(trimmedLineInLowerCase)) {
         userEnteredNewCommands = false; 
         vTaskDelay(500 / portTICK_PERIOD_MS);
         printRtcSlowMemory();
      }
   } else if (strcmp(trimmedLineInLowerCase, "list") == 0) {
      printRtcSlowMemory();  
   } else if (strcmp(trimmedLineInLowerCase, "reset") == 0) {
      initializeUlpProgram();
   } else if ((strcmp(trimmedLineInLowerCase, "help") == 0) || (strlen(trimmedLineInLowerCase) == 0)) {
      printHelp(); 
   } else if (regexMatches(trimmedLineInLowerCase, "var\\([0-9]+\\)")) {
      createVariable(trimmedLineInLowerCase);
   } else {
      createCommand(trimmedLineInLowerCase);
   }
}

static void setBytesInUlpProgram(size_t commandIndex, CommandBytes *commandBytes) {
   size_t indexOfFirstByte = ULP_PROGRAM_HEADER_SIZE_IN_BYTES + (commandIndex * ULP_PROGRAM_COMMAND_SIZE_IN_BYTES);

   ulpProgram[indexOfFirstByte + 0] = commandBytes->byte0;
   ulpProgram[indexOfFirstByte + 1] = commandBytes->byte1;
   ulpProgram[indexOfFirstByte + 2] = commandBytes->byte2;
   ulpProgram[indexOfFirstByte + 3] = commandBytes->byte3;
}

static void printCommands(const uint8_t *firstByteOfFirstCommand, size_t commandCount) {
   char command[50];
   
   printf("\nmemory dump:\n\n");
   printf("     byte3  byte2  byte1  byte0\n");
   for (size_t commandIndex = 0; commandIndex < commandCount; commandIndex++) {
      uint8_t *firstByteOfCommand = firstByteOfFirstCommand + (commandIndex * ULP_PROGRAM_COMMAND_SIZE_IN_BYTES);
      sprintf(command, "%2d:     %02x     %02x     %02x     %02x", commandIndex, *(firstByteOfCommand + 3), *(firstByteOfCommand + 2), *(firstByteOfCommand + 1), *(firstByteOfCommand));
      printf("%s\n", command);
   }
   printf("\n");
}

static void printUlpProgram(const uint8_t *programStart) {
   struct UlpBinary *ulpBinary = (struct UlpBinary*)programStart;
   printf("magic      = %d\n", ulpBinary->magic);
   printf("textOffset = %d\n", ulpBinary->textOffset);
   printf("textSize   = %d\n", ulpBinary->textSize);
   printf("dataSize   = %d\n", ulpBinary->dataSize);
   printf("bssSize    = %d\n", ulpBinary->bssSize);
   
   printCommands(programStart + ulpBinary->textOffset, ulpBinary->textSize);
}

static void printRtcSlowMemory() {
   size_t commandCount = nextCommandIndex;

   if (commandCount == 0) {
      printf("No commands entered -> list is empty.\n");
      return;
   } 

   if (userEnteredNewCommands) {
      printf("Please run your program first!\n");
      return;
   }
   
   printCommands((uint8_t*)RTC_SLOW_MEM, commandCount);
}

static bool regexMatches(const char *text, const char *pattern) {
   bool matches = false;
   regex_t regex;
   char strictMatchingPattern[strlen(pattern) + 2];
   sprintf(strictMatchingPattern, "^%s$", pattern);
   if(regcomp(&regex, strictMatchingPattern, REG_EXTENDED) != 0) {
      printf("ERROR: Failed to compile regex pattern \"%s\".\n", pattern);
   } else {
      int result = regexec(&regex, text, 0, NULL, 0);
      regfree(&regex);
      matches = result == 0;
   }
   return matches;
}

static void createVariable(const char *command) {
   char copyOfCommand[strlen(command)];
   strcpy(copyOfCommand, command);
   strtok((char*)copyOfCommand, "(");
   char *valueStart = strtok(NULL, "(");
   uint32_t value = atoi(strtok(valueStart, ")"));
   
   if(value > 65535) {
      printf("ERROR: the value is too high for 16 bit (max: 65535).\n");
   } else {
      uint8_t byte0 = (value & 0x00ff);
      uint8_t byte1 = (value & 0xff00) >> 8;
      uint8_t byte2 = 0;
      uint8_t byte3 = 0;
      CommandBytes commandBytes = {byte0, byte1, byte2, byte3};

      size_t commandIndex = nextCommandIndex++;
      setBytesInUlpProgram(commandIndex, &commandBytes);
      printf("%u: variable (value = %d)\n", commandIndex, value);
      userEnteredNewCommands = true; 
   }
}

static void createCommand(const char *command) {
   Result result = getCommandBytesFor((uint8_t*)command);

   if (result.errorMessage != NULL) {
      printf("ERROR: %s (input=\"%s\")\n", result.errorMessage, command);
   } else {
      if (nextCommandIndex >= ULP_PROGRAM_MAX_COMMAND_COUNT) {
         printf("maximum number (%d) of commands reached -> cannot add this command\n", ULP_PROGRAM_MAX_COMMAND_COUNT);
      } else {
         size_t commandIndex = nextCommandIndex++;
         setBytesInUlpProgram(commandIndex, &(result.commandBytes));
         printf("%u: \"%s\"\n", commandIndex, command);
         userEnteredNewCommands = true;
      }
   }
}

static bool runProgram(const char *command) {
   bool executedProgram = false;
   char copyOfCommand[strlen(command)];
   strcpy(copyOfCommand, command);
   strtok((char*)copyOfCommand, " ");
   size_t indexOfFirstCommand = atoi(strtok(NULL, " "));

   if(indexOfFirstCommand >= nextCommandIndex) {
      if (nextCommandIndex == 0) {
         printf("ERROR: You need to enter at least one command before calling \"run\".\n");
      } else {
         printf("ERROR: Maximum allowed command index to start from is %d.\n", nextCommandIndex - 1);
      }
   } else {
      appendHaltCommandsToUlpProgram(ulpProgram);
      loadUlpProgram(ulpProgram);
      startUlpProgram(indexOfFirstCommand);
      executedProgram = true;
   }
   return executedProgram;
}