#include <string.h>
#include <math.h>
#include <stdio.h>

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
#define ULP_PROGRAM_SHARED_VARIABLES_COUNT      1
#define ULP_PROGRAM_HEADER_SIZE_IN_BYTES        12
#define ULP_PROGRAM_COMMAND_SIZE_IN_BYTES       4
#define ULP_PROGRAM_WAKE_COMMANDS_COUNT         4

static const char* TAG = "main";

static uint8_t ulpProgram[ULP_PROGRAM_HEADER_SIZE_IN_BYTES + (ULP_PROGRAM_MAX_COMMAND_COUNT + ULP_PROGRAM_WAKE_COMMANDS_COUNT) * ULP_PROGRAM_COMMAND_SIZE_IN_BYTES];
static char *WAKE_COMMANDS[ULP_PROGRAM_WAKE_COMMANDS_COUNT] = { "reg_rd 0x30, 0x13, 0x13", "and r0, r0, 1", "jumpr -8, 1, lt", "wake"};

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

static void       initUlp();
static void       loadUlpProgram(const uint8_t *program);
static void       loadCorrectedUlpProgram();
static void       startUlpProgram();
static void       initSerialInterface();
static void       handleCommands();
static void       processNextLine(uint8_t *line);
static void       printUlpProgram(const uint8_t *programStart);
static void       initializeUlpProgram();
static void       setBytesInUlpProgram(size_t commandIndex, CommandBytes *commandBytes);

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
   esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
   if (cause == ESP_SLEEP_WAKEUP_ULP) {
      ESP_LOGI(TAG, "return value = 0x%04X", ulp_returnValue & 0xffff);
   } else {
      ESP_LOGI(TAG, "first startup -> initializing ULP");
      initUlp();
      printUlpProgram(ulp_main_bin_start);
      initializeUlpProgram();
      printUlpProgram(ulpProgram);
      /*loadUlpProgram(ulpProgram);
      startUlpProgram();
   
      ESP_LOGI(TAG, "disabling all wakeup sources");
      ESP_ERROR_CHECK( esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL) );
      ESP_LOGI(TAG, "enabling ULP wakeup");
      ESP_ERROR_CHECK( esp_sleep_enable_ulp_wakeup() );
      ESP_LOGI(TAG, "Entering deep sleep");
      esp_deep_sleep_start();*/
   } 
   
   xTaskCreate(handleCommands, "handle commands from serial interface", 4000, NULL, 10, NULL);
}

static void initUlp()
{
   esp_deep_sleep_disable_rom_logging(); // suppress boot messages
}

static void initializeUlpProgram() {
   ESP_LOGI(TAG, "initializing ULP program");
   struct UlpBinary* metaData = (struct UlpBinary*)ulpProgram;
   metaData->magic       = 0x00706c75;
   metaData->textOffset  = 12;
   metaData->textSize    = (ULP_PROGRAM_SHARED_VARIABLES_COUNT + ULP_PROGRAM_MAX_COMMAND_COUNT + ULP_PROGRAM_WAKE_COMMANDS_COUNT) * ULP_PROGRAM_COMMAND_SIZE_IN_BYTES;
   metaData->dataSize    = 0;
   metaData->bssSize     = 0;

   // set all shared variables to 0
   CommandBytes zeroValue = {0x00, 0x00, 0x00, 0x00};
   size_t offset = ULP_PROGRAM_HEADER_SIZE_IN_BYTES / ULP_PROGRAM_COMMAND_SIZE_IN_BYTES;

   for(size_t sharedVariableIndex = 0; sharedVariableIndex < ULP_PROGRAM_SHARED_VARIABLES_COUNT; sharedVariableIndex++) {
      setBytesInUlpProgram(offset + sharedVariableIndex, &zeroValue);
   }

   // set all commands to nop
   Result noopCommand = getCommandBytesFor((uint8_t*)"nop");
   offset += ULP_PROGRAM_SHARED_VARIABLES_COUNT;

   for(size_t commandIndex = 0; commandIndex < ULP_PROGRAM_MAX_COMMAND_COUNT; commandIndex++) {
      setBytesInUlpProgram(offset + commandIndex, &(noopCommand.commandBytes));
   }

   // append wake code sequence
   offset += ULP_PROGRAM_MAX_COMMAND_COUNT;
   Result wakeCommands[ULP_PROGRAM_WAKE_COMMANDS_COUNT];
   wakeCommands[0] = getCommandBytesFor((uint8_t*)WAKE_COMMANDS[0]);
   wakeCommands[1] = getCommandBytesFor((uint8_t*)WAKE_COMMANDS[1]);
   wakeCommands[2] = getCommandBytesFor((uint8_t*)WAKE_COMMANDS[2]);
   wakeCommands[3] = getCommandBytesFor((uint8_t*)WAKE_COMMANDS[3]);

   for(size_t commandIndex = 0; commandIndex < ULP_PROGRAM_WAKE_COMMANDS_COUNT; commandIndex++) {
      setBytesInUlpProgram(offset + commandIndex, &(wakeCommands[commandIndex].commandBytes));
   }
}

static void loadCorrectedUlpProgram()
{
   ESP_LOGI(TAG, "fixing jumpr bug in ULP program");
   Result jumprCommand = getCommandBytesFor((uint8_t*)"jumpr -8, 1, lt");
   uint8_t correctedJumpr[4] = {
      jumprCommand.commandBytes.byte0, 
      jumprCommand.commandBytes.byte1, 
      jumprCommand.commandBytes.byte2, 
      jumprCommand.commandBytes.byte3};
   size_t programSize = ulp_main_bin_end - ulp_main_bin_start;
   size_t jumprStartPosition = (programSize - 1) - (2 * 4) + 1;
   uint8_t fixedProgram[programSize];
   for (size_t i = 0; i < programSize; i++) {
      int correctedJumprIndex = i - jumprStartPosition;
      uint8_t value = (correctedJumprIndex >= 0 && correctedJumprIndex < 4) ? correctedJumpr[correctedJumprIndex] : *(ulp_main_bin_start + i);
      *(fixedProgram + i) = value;
   }

   loadUlpProgram(fixedProgram);
}

static void loadUlpProgram(const uint8_t *program) {
   ESP_LOGI(TAG, "loading ULP program into ULP memory ...");
   struct UlpBinary* metaData = (struct UlpBinary*)program;
   uint32_t programSizeInBytes = ULP_PROGRAM_HEADER_SIZE_IN_BYTES + metaData->textSize + metaData->dataSize + metaData->bssSize;
   ESP_ERROR_CHECK(ulp_load_binary(0, program, programSizeInBytes / sizeof(uint32_t)));
}

static void startUlpProgram()
{
   ESP_LOGI(TAG, "starting ULP program");
   
   /* Set ULP wake up period to 1000ms */
   ulp_set_wakeup_period(0, MILLIS(1000));

   /* Start the ULP program */
   ESP_ERROR_CHECK(ulp_run(&ulp_entry - RTC_SLOW_MEM));
}

static void initSerialInterface() {
   uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
   };
   
   ESP_ERROR_CHECK(uart_driver_install(SERIAL_PORT, 1024, 0, 0, NULL, 0));
   ESP_ERROR_CHECK(uart_param_config(SERIAL_PORT, &uart_config));
   // Set pins for UART0 (TX: IO4, RX: IO5, RTS: IO18, CTS: IO19)
   ESP_ERROR_CHECK(uart_set_pin(SERIAL_PORT, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
   ESP_LOGI(TAG, "initialized serial port");
}

static void handleCommands(void * pvParameters) {
   initSerialInterface();
   
   size_t readBytes;
   uint8_t buffer[2];
   size_t maxLineLength = 40;
   uint8_t line[maxLineLength + 1];
   size_t insertationPosition = 0;

   while (1) {
      readBytes = uart_read_bytes(SERIAL_PORT, buffer, 1, 100 / portTICK_PERIOD_MS);
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
}

static void processNextLine(uint8_t *line) {
   size_t lineLength = strlen((const char*)line);
   uint8_t trimmedLineInLowerCase[lineLength];
   strcpy((char*)trimmedLineInLowerCase, (char*)line);
   toLowerCase(trim(trimmedLineInLowerCase));
   if (strcmp((const char*)trimmedLineInLowerCase, "run") == 0) {
      printf("run ULP program requested\n");
      // TODO start ULP program
   } else {
      // TODO handle command count
      Result result = getCommandBytesFor(trimmedLineInLowerCase);

      if (result.errorMessage != NULL) {
         printf("ERROR: %s (input=\"%s\")\n", result.errorMessage, trimmedLineInLowerCase);
      } else {
         printf("command bytes for \"%s\": 0x%02x, 0x%02x, 0x%02x, 0x%02x\n", 
            trimmedLineInLowerCase, 
            result.commandBytes.byte0, 
            result.commandBytes.byte1, 
            result.commandBytes.byte2, 
            result.commandBytes.byte3);
         setBytesInUlpProgram(0, &(result.commandBytes));
      }
   }
}

static void setBytesInUlpProgram(size_t commandIndex, CommandBytes *commandBytes) {
   ulpProgram[commandIndex * ULP_PROGRAM_COMMAND_SIZE_IN_BYTES + 0] = commandBytes->byte0;
   ulpProgram[commandIndex * ULP_PROGRAM_COMMAND_SIZE_IN_BYTES + 1] = commandBytes->byte1;
   ulpProgram[commandIndex * ULP_PROGRAM_COMMAND_SIZE_IN_BYTES + 2] = commandBytes->byte2;
   ulpProgram[commandIndex * ULP_PROGRAM_COMMAND_SIZE_IN_BYTES + 3] = commandBytes->byte3;
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