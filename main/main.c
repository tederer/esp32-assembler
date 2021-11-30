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

static const char* TAG = "main";

static uint8_t ulpProgram[ULP_PROGRAM_HEADER_SIZE_IN_BYTES + (ULP_PROGRAM_MAX_COMMAND_COUNT + ULP_PROGRAM_HALT_COMMANDS_COUNT) * ULP_PROGRAM_COMMAND_SIZE_IN_BYTES];
// The reg_wr command disables the ULP timer to ensure that the ULP program gets executed only once (see technical reference manual "29.5 ULP Program Execution").
static char *HALT_COMMANDS[ULP_PROGRAM_HALT_COMMANDS_COUNT] = { "reg_wr 6, 24, 24, 0", "halt"};
static size_t nextCommandIndex = 0;
static bool userWantsToStartUlpProgram = false;

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");

static void appendHaltCommandsToUlpProgram(const uint8_t *program);
static void loadUlpProgram(const uint8_t *program);
static void startUlpProgram(size_t indexOfFirstCommand);
static void initSerialInterface();
static void handleCommands(void *parameters);
static void processNextLine(const uint8_t *line);
static void printUlpProgram(const uint8_t *programStart);
static void printRtcSlowMemory(size_t commandsToPrint);
static void initializeUlpProgram();
static void setBytesInUlpProgram(size_t commandIndex, CommandBytes *commandBytes);
static void createVariable(const char *command);
static void createCommand(const char *command);
static void runProgram(const char *command);
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
   ESP_LOGI(TAG, "initializing ULP program");
   struct UlpBinary* metaData = (struct UlpBinary*)ulpProgram;
   metaData->magic       = 0x00706c75;
   metaData->textOffset  = 12;
   metaData->textSize    = 0;
   metaData->dataSize    = 0;
   metaData->bssSize     = 0;

   // set all commands to nop
   Result noopCommand = getCommandBytesFor((uint8_t*)"nop");
   
   for(size_t commandIndex = 0; commandIndex < ULP_PROGRAM_MAX_COMMAND_COUNT; commandIndex++) {
      setBytesInUlpProgram(commandIndex, &(noopCommand.commandBytes));
   }
}

static void appendHaltCommandsToUlpProgram(const uint8_t *program) {
   struct UlpBinary* metaData = (struct UlpBinary*)program;
   metaData->magic       = 0x00706c75;
   metaData->textOffset  = 12;
   metaData->textSize    = (nextCommandIndex + ULP_PROGRAM_HALT_COMMANDS_COUNT) * ULP_PROGRAM_COMMAND_SIZE_IN_BYTES;
   metaData->dataSize    = 0;
   metaData->bssSize     = 0;

   size_t commandIndexOfFirstHaltCommand = nextCommandIndex;

   for(size_t index = 0; index < ULP_PROGRAM_HALT_COMMANDS_COUNT; index++) {
      Result command = getCommandBytesFor((uint8_t*)HALT_COMMANDS[index]);
      setBytesInUlpProgram(commandIndexOfFirstHaltCommand + index, &command.commandBytes);
   }
}

static void loadUlpProgram(const uint8_t *program) {
   ESP_LOGI(TAG, "loading ULP program into ULP memory ...");
   struct UlpBinary* metaData = (struct UlpBinary*)program;
   uint32_t programSizeInBytes = ULP_PROGRAM_HEADER_SIZE_IN_BYTES + metaData->textSize + metaData->dataSize + metaData->bssSize;
   ESP_ERROR_CHECK(ulp_load_binary(0, program, programSizeInBytes / sizeof(uint32_t)));
}

static void startUlpProgram(size_t indexOfFirstCommand)
{
   ESP_LOGI(TAG, "starting ULP program (indexOfFirstCommand=%d)", indexOfFirstCommand);
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
   
   ESP_LOGI(TAG, "starting input reading loop");
   while (!userWantsToStartUlpProgram) {
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
            printf("ERROR: max line length (%d) reached -> ignoring \"%s\"\n", maxLineLength, line);
            insertationPosition = 0;
         }
      }
   }

   ESP_LOGI(TAG, "finished input reading loop");
   vTaskDelete(NULL);
}

static void processNextLine(const uint8_t *line) {
   size_t lineLength = strlen((const char*)line);
   uint8_t copyOfLine[lineLength];
   strcpy((char*)copyOfLine, (char*)line);
   char *trimmedLineInLowerCase = (char*)toLowerCase(trim(copyOfLine));

   if (regexMatches(trimmedLineInLowerCase, "run [0-9]+")) {
      runProgram(trimmedLineInLowerCase);
      vTaskDelay(500 / portTICK_PERIOD_MS);
      printRtcSlowMemory(nextCommandIndex + ULP_PROGRAM_HALT_COMMANDS_COUNT);
   } else if (strcmp(trimmedLineInLowerCase, "reset") == 0) {
      initializeUlpProgram();
      nextCommandIndex=0;      
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

static void printUlpProgram(const uint8_t *programStart) {
   struct UlpBinary *ulpBinary = (struct UlpBinary*)programStart;
   ESP_LOGI(TAG, "magic         = %d", ulpBinary->magic);
   ESP_LOGI(TAG, "textOffset    = %d", ulpBinary->textOffset);
   ESP_LOGI(TAG, "textSize      = %d", ulpBinary->textSize);
   ESP_LOGI(TAG, "dataSize      = %d", ulpBinary->dataSize);
   ESP_LOGI(TAG, "bssSize       = %d", ulpBinary->bssSize);
   
   char command[50];

   const uint8_t *codeStart = programStart + ulpBinary->textOffset;

   ESP_LOGI(TAG, "             byte3  byte2  byte1  byte0");
   for (size_t offset = 0; offset < ulpBinary->textSize; offset = offset + 4) {
      sprintf(command, "command %2d:     %02x     %02x     %02x     %02x", offset / 4, *(codeStart + offset + 3), *(codeStart + offset + 2), *(codeStart + offset + 1), *(codeStart + offset));
      ESP_LOGI(TAG, "%s", command);
   }
}

static void printRtcSlowMemory(size_t commandsToPrint) {
   char command[50];

   ESP_LOGI(TAG, "     byte3  byte2  byte1  byte0");
   for (size_t commandIndex = 0; commandIndex < commandsToPrint; commandIndex++) {
      uint8_t *firstByteOfCommand = (uint8_t*)RTC_SLOW_MEM + commandIndex * ULP_PROGRAM_COMMAND_SIZE_IN_BYTES;
      sprintf(command, "%2d:     %02x     %02x     %02x     %02x", commandIndex, *(firstByteOfCommand + 3), *(firstByteOfCommand + 2), *(firstByteOfCommand + 1), *(firstByteOfCommand));
      ESP_LOGI(TAG, "%s", command);
   }
}

static bool regexMatches(const char *text, const char *pattern) {
   bool matches = false;
   regex_t regex;
   char strictMatchingPattern[strlen(pattern) + 2];
   sprintf(strictMatchingPattern, "^%s$", pattern);
   if(regcomp(&regex, strictMatchingPattern, REG_EXTENDED) != 0) {
      printf("ERROR: failed to compile regex pattern \"%s\"\n", pattern);
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
      printf("[%u]: variable (value = %d)\n", commandIndex, value);
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
         printf("[%u]: \"%s\"\n", commandIndex, command);
      }
   }
}

static void runProgram(const char *command) {
   char copyOfCommand[strlen(command)];
   strcpy(copyOfCommand, command);
   strtok((char*)copyOfCommand, " ");
   size_t indexOfFirstCommand = atoi(strtok(NULL, " "));
   if(indexOfFirstCommand >= nextCommandIndex) {
      printf("ERROR: maximum allowed command index to start from is %d.\n", nextCommandIndex - 1);
   } else {
      appendHaltCommandsToUlpProgram(ulpProgram);
      loadUlpProgram(ulpProgram);
      startUlpProgram(indexOfFirstCommand);
   }
}