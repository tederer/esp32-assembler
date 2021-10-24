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

#include "Commands.h"

#define SERIAL_PORT  UART_NUM_0
#define MILLIS(ms)   ((ms) * 1000)
#define LF           0x0d
#define CR           0x0a

static const char* TAG = "main";

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

static void initUlp();
static void loadUlpProgram();
static void loadGeneratedUlpProgramFromRam();
static void startUlpProgram();
static void initSerialInterface();
static void handleCommands();
static void processNextLine(uint8_t *line);

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
      ESP_LOGI(TAG, "returnValue = 0x%04X", ulp_returnValue);
   } else {
      ESP_LOGI(TAG, "first startup -> initializing ULP");
      initUlp();
      //loadUlpProgram();
      loadGeneratedUlpProgramFromRam();

      struct UlpBinary *ulpBinary = (struct UlpBinary*)ulp_main_bin_start;
      ESP_LOGI(TAG, "size in bytes = %d", ulp_main_bin_end - ulp_main_bin_start);
      ESP_LOGI(TAG, "magic         = %d", ulpBinary->magic);
      ESP_LOGI(TAG, "textOffset    = %d", ulpBinary->textOffset);
      ESP_LOGI(TAG, "textSize      = %d", ulpBinary->textSize);
      ESP_LOGI(TAG, "dataSize      = %d", ulpBinary->dataSize);
      ESP_LOGI(TAG, "bssSize       = %d", ulpBinary->bssSize);
      ESP_LOGI(TAG, "entryPoint    = %d", &ulp_entry - RTC_SLOW_MEM);

      char command[50];

      const uint8_t *codeStart = ulp_main_bin_start + ulpBinary->textOffset;

      for (size_t offset = 0; offset < ulpBinary->textSize; offset = offset + 4) {
         sprintf(command, "%2d: %02x %02x %02x %02x", offset / 4, *(codeStart + offset), *(codeStart + offset + 1), *(codeStart + offset + 2), *(codeStart + offset + 3));
         ESP_LOGI(TAG, "command %s", command);
      }

      /*startUlpProgram();
   
      ESP_LOGI(TAG, "disabling all wakeup sources");
      ESP_ERROR_CHECK( esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL) );
      ESP_LOGI(TAG, "enabling ULP wakeup");
      ESP_ERROR_CHECK( esp_sleep_enable_ulp_wakeup() );
      ESP_LOGI(TAG, "Entering deep sleep");
      esp_deep_sleep_start();  */ 
   } 
   
   //xTaskCreate(handleCommands, "handle commands from serial interface", 4000, NULL, 10, NULL);
}

static void initUlp()
{
   esp_deep_sleep_disable_rom_logging(); // suppress boot messages
}

static void loadUlpProgram()
{
   esp_err_t err = ulp_load_binary(0, ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t));
   ESP_ERROR_CHECK(err);
}

static void loadGeneratedUlpProgramFromRam()
{
   uint8_t programBinary[50];
   struct UlpBinary* metaData = (struct UlpBinary*)programBinary;
   metaData->magic            = 7367797;
   metaData->textOffset       = 12;
   metaData->textSize         = 20;
   metaData->dataSize         = 0;
   metaData->bssSize          = 0;
   uint8_t* codeStart         = programBinary + metaData->textOffset;
   size_t programSizeInBytes  = metaData->textOffset + metaData->textSize;
   size_t index               = 0;

   *(codeStart + index++) = 0x12;
   *(codeStart + index++) = 0x34;
   *(codeStart + index++) = 0x56;
   *(codeStart + index++) = 0x78;

   *(codeStart + index++) = 0x30;
   *(codeStart + index++) = 0x00;
   *(codeStart + index++) = 0xcc;
   *(codeStart + index++) = 0x29;
 
   *(codeStart + index++) = 0x10;
   *(codeStart + index++) = 0x00;
   *(codeStart + index++) = 0x40;
   *(codeStart + index++) = 0x72;
 
   *(codeStart + index++) = 0x04;
   *(codeStart + index++) = 0x00;
   *(codeStart + index++) = 0x40;
   *(codeStart + index++) = 0x80;
 
   *(codeStart + index++) = 0x01;
   *(codeStart + index++) = 0x00;
   *(codeStart + index++) = 0x00;
   *(codeStart + index++) = 0x90;

   ESP_LOGI(TAG, "sizw = %d", programSizeInBytes);
   char command[50];
   for (size_t offset = 0; offset < programSizeInBytes; offset = offset + 4) {
      sprintf(command, "%2d: %02x %02x %02x %02x", offset / 4, *(programBinary + offset), *(programBinary + offset + 1), *(programBinary + offset + 2), *(programBinary + offset + 3));
      ESP_LOGI(TAG, "command im RAM %s", command);
   }

   esp_err_t err = ulp_load_binary(0, programBinary, programSizeInBytes / sizeof(uint32_t));
   ESP_ERROR_CHECK(err);
}

static void startUlpProgram()
{
   ESP_LOGI(TAG, "starting ULP program");
   
   /* Set ULP wake up period to 1000ms */
   ulp_set_wakeup_period(0, MILLIS(1000));

   /* Start the ULP program */
   ESP_ERROR_CHECK(ulp_run(1));
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
   size_t maxLineLength = 20;
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

   CommandBytes commandBytes;
   getCommandBytesFor(line, &commandBytes);
   bool isValidCommand = true; 

   if (!isValidCommand) {
      printf("ERROR: unsupported command \"%s\"\n", line);
   }
}