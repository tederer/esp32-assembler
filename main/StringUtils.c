#include <ctype.h>

#include "StringUtils.h"

#define CR           0x0a
#define TAB          0x09
#define SPACE        0x20

static bool isWhitespace(uint8_t character) {
   return character == SPACE || character == TAB || character == CR;
} 

uint8_t* trim(uint8_t* text) {
   size_t indexOfFirstNonWhitespace = 0;
   uint8_t currentChar = *(text + indexOfFirstNonWhitespace);

   while (currentChar != 0) {
      if (isWhitespace(currentChar)) {
         indexOfFirstNonWhitespace++;
         currentChar = *(text + indexOfFirstNonWhitespace);
      } else {
         break;
      }
   }

   if (indexOfFirstNonWhitespace > 0) {
      size_t offset = 0;
      while (*(text + indexOfFirstNonWhitespace + offset) != 0) {
         *(text + offset) = *(text + indexOfFirstNonWhitespace + offset);
         offset++;
      }
      *(text + offset) = 0;
   }

   size_t indexOfFirstTrailingWhitespace = strlen((char*)text);
   
   for (size_t index = strlen((char*)text) - 1; isWhitespace(*(text + index)); index--) {
      indexOfFirstTrailingWhitespace = index;
   }
   
   if (indexOfFirstTrailingWhitespace < strlen((char*)text)) {
      *(text + indexOfFirstTrailingWhitespace) = 0;
   }
   return text;
}

uint8_t* toLowerCase(uint8_t* text) {
   for(uint8_t* currentChar = text; *currentChar != 0; currentChar++) {
      uint8_t lowerCaseChar = tolower(*currentChar);
      *currentChar = lowerCaseChar;
   }
   return text;
}