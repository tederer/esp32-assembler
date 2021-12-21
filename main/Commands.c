#include <ctype.h>
#include <sys/types.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Commands.h"
#include "StringUtils.h"

#define LF           0x0d
#define CR           0x0a
#define TAB          0x09
#define SPACE        0x20
#define COMMA        0x2c

static char UNSUPPORTED_JUMPR_R0_ERROR_MESSAGE[] = "The conditions \"eq\", \"le\" and \"gt\" are not supported by the ULP. Please use \"lt\" or \"ge\" instead.";
static char UNSUPPORTED_JUMPR_STAGECOUNT_ERROR_MESSAGE[] = "The conditions \"eq\" and \"gt\" are not supported by the ULP. Please use \"lt\", \"le\" or \"ge\" instead.";
static char UNSUPPORTED_COMMAND[] = "This command is not supported.";

typedef struct {
   char* pattern;
   Result (*getBytes)(uint8_t*);
} Command;

static Result moveRegisterToRegister(uint8_t *commandAsText);
static Result moveImmediateToRegister(uint8_t *commandAsText);
static Result andImmediate(uint8_t *commandAsText);
static Result andRegister(uint8_t *commandAsText);
static Result orImmediate(uint8_t *commandAsText);
static Result orRegister(uint8_t *commandAsText);
static Result addImmediate(uint8_t *commandAsText);
static Result addRegister(uint8_t *commandAsText);
static Result subImmediate(uint8_t *commandAsText);
static Result subRegister(uint8_t *commandAsText);
static Result nop(uint8_t *commandAsText);
static Result leftShiftRegister(uint8_t *commandAsText);
static Result leftShiftImmediate(uint8_t *commandAsText);
static Result rightShiftRegister(uint8_t *commandAsText);
static Result rightShiftImmediate(uint8_t *commandAsText);

static int aluOperation(char *instruction);
static int stageCountAluOperation(char *instruction);
static int absoluteJumpType(char *condition);

static Result aluOperationWithImmediateValue(uint8_t *commandAsText);
static Result aluOperationAmongRegisters(uint8_t *commandAsText);
static Result stageCountOperation(uint8_t *commandAsText);
static Result storeDataInMemory(uint8_t *commandAsText);
static Result loadDataFromMemory(uint8_t *commandAsText);
static Result jumpToAbsoluteAddress(uint8_t *commandAsText, bool isImmediate, bool isConditional);
static Result jumpConditionalUponR0ToRelativeAddress(uint8_t *commandAsText);
static Result jumpConditionalUponStageCountToRelativeAddress(uint8_t *commandAsText);
static Result adc(uint8_t *commandAsText);
static Result i2cReadWrite(uint8_t *commandAsText);
static Result readRegister(uint8_t *commandAsText);
static Result writeRegister(uint8_t *commandAsText);

static Result jumpRegister(uint8_t *commandAsText);
static Result jumpImmediate(uint8_t *commandAsText);
static Result jumpRegisterConditional(uint8_t *commandAsText);
static Result jumpImmediateConditional(uint8_t *commandAsText);
static Result unsupportedJumpRelativeConditionalBasedOnR0(uint8_t *commandAsText);
static Result unsupportedJumpRelativeConditionalBasedOnStageCount(uint8_t *commandAsText);
static Result stageReset(uint8_t *commandAsText);
static Result stageIncrement(uint8_t *commandAsText);
static Result stageDecrement(uint8_t *commandAsText);
static Result halt(uint8_t *commandAsText);
static Result wake(uint8_t *commandAsText);
static Result sleep(uint8_t *commandAsText);
static Result wait(uint8_t *commandAsText);
static Result tsens(uint8_t *commandAsText);

static Result waitCycles(int cycles);

Command commands[] = {
   {"add r[0-3] r[0-3] ([-]?(0x[0-9a-f]+|[0-9]+))",                                                                    addImmediate}, 
   {"add r[0-3] r[0-3] r[0-3]",                                                                                        addRegister}, 
   {"sub r[0-3] r[0-3] ([-]?(0x[0-9a-f]+|[0-9]+))",                                                                    subImmediate}, 
   {"sub r[0-3] r[0-3] r[0-3]",                                                                                        subRegister}, 
   {"and r[0-3] r[0-3] (0x[0-9a-f]+|[0-9]+)",                                                                          andImmediate}, 
   {"and r[0-3] r[0-3] r[0-3]",                                                                                        andRegister}, 
   {"or r[0-3] r[0-3] (0x[0-9a-f]+|[0-9]+)",                                                                           orImmediate}, 
   {"or r[0-3] r[0-3] r[0-3]",                                                                                         orRegister}, 
   {"move r[0-3] r[0-3]",                                                                                              moveRegisterToRegister}, 
   {"move r[0-3] ([-]?(0x[0-9a-f]+|[0-9]+))",                                                                          moveImmediateToRegister}, 
   {"lsh r[0-3] r[0-3] ([-]?(0x[0-9a-f]+|[0-9]+))",                                                                    leftShiftImmediate}, 
   {"lsh r[0-3] r[0-3] r[0-3]",                                                                                        leftShiftRegister}, 
   {"rsh r[0-3] r[0-3] ([-]?(0x[0-9a-f]+|[0-9]+))",                                                                    rightShiftImmediate}, 
   {"rsh r[0-3] r[0-3] r[0-3]",                                                                                        rightShiftRegister}, 
                                                  
   {"stage_rst",                                                                                                       stageReset},
   {"stage\\_inc (0x[0-9a-f]+|[0-9]+)",                                                                                stageIncrement},
   {"stage\\_dec (0x[0-9a-f]+|[0-9]+)",                                                                                stageDecrement},
                                               
   {"st r[0-3] r[0-3] (0x[0-9a-f]+|[0-9]+)",                                                                           storeDataInMemory},
   {"ld r[0-3] r[0-3] (0x[0-9a-f]+|[0-9]+)",                                                                           loadDataFromMemory},
                                                  
   {"jump r[0-3]",                                                                                                     jumpRegister},
   {"jump r[0-3] ((eq)|(ov))",                                                                                         jumpRegisterConditional},
   {"jump (0x[0-9a-f]+|[0-9]+)",                                                                                       jumpImmediate},
   {"jump (0x[0-9a-f]+|[0-9]+) ((eq)|(ov))",                                                                           jumpImmediateConditional},
                                                  
   {"jumpr [-]?(0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+) ((lt)|(ge))",                                                 jumpConditionalUponR0ToRelativeAddress},
   {"jumpr [-]?(0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+) ((eq)|(le)|(gt))",                                            unsupportedJumpRelativeConditionalBasedOnR0},
                                               
   {"jumps [-]?(0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+) ((lt)|(le)|(ge))",                                            jumpConditionalUponStageCountToRelativeAddress},
   {"jumps [-]?(0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+) ((eq)|(gt))",                                                 unsupportedJumpRelativeConditionalBasedOnStageCount},
                                                  
   {"halt",                                                                                                            halt},
   {"wake",                                                                                                            wake},
   {"sleep [0-4]",                                                                                                     sleep},
   {"wait (0x[0-9a-f]+|[0-9]+)",                                                                                       wait},
   {"nop",                                                                                                             nop}, 
   {"tsens r[0-3] (0x[0-9a-f]+|[0-9]+)",                                                                               tsens},
   {"adc r[0-3] (0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+)",                                                            adc},
   {"i2c_rd (0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+)",                      i2cReadWrite},
   {"i2c_wr (0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+)", i2cReadWrite},
   {"reg_rd (0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+)",                                           readRegister},
   {"reg_wr (0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+) (0x[0-9a-f]+|[0-9]+)",                      writeRegister},

   {NULL, NULL}
};

static bool isWhitespace(uint8_t character) {
   return character == SPACE || character == TAB || character == CR;
} 

static uint8_t* normalizeTokenSeparators(uint8_t *text) {
   for(size_t index = 0; *(text + index) != 0; index++) {
      if (*(text + index) == COMMA) {
         *(text + index) = SPACE;
      }
   }

   bool previousCharWasWhitespace = false;
   size_t insertationIndex = 0;
   
   for(size_t index = 0; *(text + index) != 0; index++) {
      uint8_t currentChar = *(text + index);
      bool currentCharIsWhitespace = isWhitespace(currentChar);

      if (!currentCharIsWhitespace) {
         *(text + (insertationIndex++)) = currentChar;
         previousCharWasWhitespace = false;
      } else {
         if (!previousCharWasWhitespace) {
            *(text + (insertationIndex++)) = currentChar;
         }
         previousCharWasWhitespace = true;
      }
   }
   
   *(text + insertationIndex) = 0;
   return text;
}

Result getCommandBytesFor(const uint8_t *line) {
   uint8_t copyOfLine[strlen((char*)line) + 1];
   strcpy((char*)copyOfLine, (char*)line);
   
   uint8_t* trimmedLine             = trim(copyOfLine);
   uint8_t* trimmedAndLowerCaseLine = toLowerCase(trimmedLine);
   uint8_t* normalizedLine          = normalizeTokenSeparators(trimmedAndLowerCaseLine);
   
   for (size_t i = 0; commands[i].pattern != NULL; i++) {
      regex_t regex;
      char pattern[strlen(commands[i].pattern) + 2];
      sprintf(pattern, "^%s$", commands[i].pattern);
      if(regcomp(&regex, pattern, REG_EXTENDED) != 0) {
         printf("ERROR: failed to compile regex pattern \"%s\"\n", commands[i].pattern);
      } else {
         int result = regexec(&regex, (char*)normalizedLine, 0, NULL, 0);
         regfree(&regex);
         if (result == 0) {
            return commands[i].getBytes(normalizedLine);
         }
      }
   }

   CommandBytes commandBytes = {0x00, 0x00, 0x00, 0x00};
   return (Result){commandBytes, UNSUPPORTED_COMMAND};
}

static Result addImmediate(uint8_t *commandAsText) {
   return aluOperationWithImmediateValue(commandAsText);
}

static Result subImmediate(uint8_t *commandAsText) {
   return aluOperationWithImmediateValue(commandAsText);
}

static Result andImmediate(uint8_t *commandAsText) {
   return aluOperationWithImmediateValue(commandAsText);
}

static Result orImmediate(uint8_t *commandAsText) {
   return aluOperationWithImmediateValue(commandAsText);
}

static Result addRegister(uint8_t *commandAsText) {
   return aluOperationAmongRegisters(commandAsText);
}

static Result subRegister(uint8_t *commandAsText) {
   return aluOperationAmongRegisters(commandAsText);
}

static Result andRegister(uint8_t *commandAsText) {
   return aluOperationAmongRegisters(commandAsText);
}

static Result orRegister(uint8_t *commandAsText) {
   return aluOperationAmongRegisters(commandAsText);
}

static Result nop(uint8_t *commandAsText) {
   return waitCycles(0);
}

static Result leftShiftRegister(uint8_t *commandAsText) {
   return aluOperationAmongRegisters(commandAsText);
}

static Result rightShiftRegister(uint8_t *commandAsText) {
   return aluOperationAmongRegisters(commandAsText);
}

static Result leftShiftImmediate(uint8_t *commandAsText) {
   return aluOperationWithImmediateValue(commandAsText);
}

static Result rightShiftImmediate(uint8_t *commandAsText) {
   return aluOperationWithImmediateValue(commandAsText);
}

static Result jumpRegister(uint8_t *commandAsText){
   return jumpToAbsoluteAddress(commandAsText, false, false);
}

static Result jumpImmediate(uint8_t *commandAsText){
   return jumpToAbsoluteAddress(commandAsText, true, false);
}

static Result jumpRegisterConditional(uint8_t *commandAsText){
   return jumpToAbsoluteAddress(commandAsText, false, true);
}

static Result jumpImmediateConditional(uint8_t *commandAsText){
   return jumpToAbsoluteAddress(commandAsText, true, true);
}

static int aluOperation(char *instruction) {
   if (strcmp(instruction, "add") == 0) {
      return 0;
   }
   if (strcmp(instruction, "sub") == 0) {
      return 1;
   }
   if (strcmp(instruction, "and") == 0) {
      return 2;
   }
   if (strcmp(instruction, "or") == 0) {
      return 3;
   }
   if (strcmp(instruction, "move") == 0) {
      return 4;
   }
   if (strcmp(instruction, "lsh") == 0) {
      return 5;
   }
   if (strcmp(instruction, "rsh") == 0) {
      return 6;
   }
   return -1;
}

static int stageCountAluOperation(char *instruction) {
   if (strcmp(instruction, "stage_inc") == 0) {
      return 0;
   }
   if (strcmp(instruction, "stage_dec") == 0) {
      return 1;
   }
   if (strcmp(instruction, "stage_rst") == 0) {
      return 2;
   }
   return -1;
}

static int absoluteJumpType(char *condition) {
   if (strcmp(condition, "eq") == 0) {
      return 1;
   }
   if (strcmp(condition, "ov") == 0) {
      return 2;
   }
   return -1;
}

static int relativeStageCountCondition(char *condition) {
   if (strcmp(condition, "le") == 0) {
      return 2;
   }
   if (strcmp(condition, "lt") == 0) {
      return 0;
   }
   if (strcmp(condition, "ge") == 0) {
      return 1;
   }
   return -1;
}

// byte3      byte2      byte1      byte0
// ------------------------------------------
// 1098 7654  3210 9876  5432 1098  7654 3210   position
// oooo 001a  aaa0 iiii  iiii iiii  iiii ssdd   content: o = opCode, a = ALU operation, i = signed immediate value, s = source register, d = destination register
static Result aluOperationWithImmediateValue(uint8_t *commandAsText) {
   int opCode                = 7;
   int bit25to27             = 1;
   char *operation           = strtok((char*)commandAsText, " ");
   int destinationRegister   = atoi(strtok( NULL, " ") + 1);
   int sourceRegister        = 0;
   if (strcmp(operation, "move") != 0) {
      sourceRegister = atoi(strtok( NULL, " ") + 1);
   }
   char *immediateText = strtok( NULL, " ");
   int16_t immediate         = (int16_t)strtol(immediateText, NULL, 0);
   int aluOperatation        = aluOperation(operation);
   
   uint8_t byte0             = (destinationRegister & 0x03) | ((sourceRegister & 0x03) << 2) | ((immediate & 0xf) << 4);
   uint8_t byte1             = (immediate & 0xff0) >> 4;
   uint8_t byte2             = ((aluOperatation & 0x7) << 5) | ((immediate & 0xf000) >> 12);
   uint8_t byte3             = (opCode << 4) | (bit25to27 << 1) | ((aluOperatation & 0x8) >> 3);

   CommandBytes commandBytes = {byte0, byte1, byte2, byte3};
   return (Result){commandBytes, NULL};
}

// byte3      byte2      byte1      byte0
// ------------------------------------------
// 1098 7654  3210 9876  5432 1098  7654 3210   position
// oooo 000a  aaa0 0000  0000 0000  00SS ssdd   content: o = opCode, a = ALU operation, i = immediate value, S = source register2, s = source register1, d = destination register
static Result aluOperationAmongRegisters(uint8_t *commandAsText) {
   int opCode                = 7;
   int bit25to27             = 0;
   char *operation           = strtok((char*)commandAsText, " ");
   int destinationRegister   = atoi(strtok( NULL, " ") + 1);
   int sourceRegister1       = atoi(strtok( NULL, " ") + 1);
   int sourceRegister2       = 0;
   if (strcmp(operation, "move") == 0) {
      sourceRegister2 = sourceRegister1; // According to the technical reference manual this should not be necessary but decoded code (generate by the compiler of IDF) sets Rsrc2 = Rsrc1 for move commands.
   } else {
      sourceRegister2 = atoi(strtok( NULL, " ") + 1);
   }
   int aluOperatation        = aluOperation(operation);
   
   uint8_t byte0             = (destinationRegister & 0x03) | ((sourceRegister1 & 0x03) << 2) | ((sourceRegister2 & 0x03) << 4);
   uint8_t byte1             = 0x00;
   uint8_t byte2             = (aluOperatation & 0x7) << 5;
   uint8_t byte3             = (opCode << 4) | (bit25to27 << 1) | ((aluOperatation & 0x8) >> 3);

   CommandBytes commandBytes = {byte0, byte1, byte2, byte3};
   return (Result){commandBytes, NULL};
}

// byte3      byte2      byte1      byte0
// ------------------------------------------
// 1098 7654  3210 9876  5432 1098  7654 3210   position
// oooo 010a  aaa0 0000  0000 iiii  iiii 0000   content: o = opCode, a = ALU operation, i = immediate value
static Result stageCountOperation(uint8_t *commandAsText) {
   int opCode                = 7;
   int bit25to27             = 2;
   char *operation           = strtok((char*)commandAsText, " ");
   int immediate             = 0;
   if (strcmp(operation, "stage_rst") != 0) {
      immediate = strtol(strtok( NULL, " "), NULL, 0);
   }
   int aluOperatation        = stageCountAluOperation(operation);
   
   uint8_t byte0             = (immediate & 0x0f) << 4;
   uint8_t byte1             = (immediate & 0xf0) >> 4;
   uint8_t byte2             = (aluOperatation & 0x7) << 5;
   uint8_t byte3             = (opCode << 4) | (bit25to27 << 1) | ((aluOperatation & 0x8) >> 3);

   CommandBytes commandBytes = {byte0, byte1, byte2, byte3};
   return (Result){commandBytes, NULL};
}

// byte3      byte2      byte1      byte0
// ------------------------------------------
// 1098 7654  3210 9876  5432 1098  7654 3210   position
// oooo 1000  000k kkkk  kkkk kk00  0000 ddss   content: o = opCode, k = offset in 32-bit words, s = source register, d = destination register
static Result storeDataInMemory(uint8_t *commandAsText) {
   int opCode                = 6;
   int bit25to27             = 4;
   strtok((char*)commandAsText, " ");
   int sourceRegister        = atoi(strtok( NULL, " ") + 1);
   int destinationRegister   = atoi(strtok( NULL, " ") + 1);
   int offsetInBytes         = strtol(strtok( NULL, " "), NULL, 0);
   int offsetInWords         = offsetInBytes / 4;
   
   uint8_t byte0             = (sourceRegister & 0x03) | ((destinationRegister & 0x03) << 2);
   uint8_t byte1             = (offsetInWords & 0x03f) << 2;
   uint8_t byte2             = (offsetInWords & 0x7c0) >> 6;
   uint8_t byte3             = (opCode << 4) | (bit25to27 << 1);

   CommandBytes commandBytes = {byte0, byte1, byte2, byte3};
   return (Result){commandBytes, NULL};
}

// byte3      byte2      byte1      byte0
// ------------------------------------------
// 1098 7654  3210 9876  5432 1098  7654 3210   position
// oooo 0000  000k kkkk  kkkk kk00  0000 ddss   content: o = opCode, k = offset in 32-bit words, s = source register, d = destination register
static Result loadDataFromMemory(uint8_t *commandAsText) {
   int opCode                = 13;
   int bit25to27             = 0;
   strtok((char*)commandAsText, " ");
   int destinationRegister   = atoi(strtok( NULL, " ") + 1);
   int sourceRegister        = atoi(strtok( NULL, " ") + 1);
   int offsetInBytes         = strtol(strtok( NULL, " "), NULL, 0);
   int offsetInWords         = offsetInBytes / 4;
   
   uint8_t byte0             = (destinationRegister & 0x03) | ((sourceRegister & 0x03) << 2);
   uint8_t byte1             = (offsetInWords & 0x03f) << 2;
   uint8_t byte2             = (offsetInWords & 0x3c0) >> 6;
   uint8_t byte3             = (opCode << 4) | (bit25to27 << 1);

   CommandBytes commandBytes = {byte0, byte1, byte2, byte3};
   return (Result){commandBytes, NULL};
}

// byte3      byte2      byte1      byte0
// ------------------------------------------
// 1098 7654  3210 9876  5432 1098  7654 3210   position
// oooo 000t  ttg0 0000  000k kkkk  kkkk kkdd   content: o = opCode, t = jump type, g = immediate/destination register, k = immediate address in 32-bit words, d = destination register
static Result jumpToAbsoluteAddress(uint8_t *commandAsText, bool isImmediate, bool isConditional) {
   int opCode                       = 8;
   int bit25to27                    = 0;
   strtok((char*)commandAsText, " ");
   int immediateInWords             = 0;
   int destinationRegister          = 0; 
   int addressInDestinationRegister = isImmediate ? 0 : 1;
   if (isImmediate) {
      int immediateInBytes          = strtol(strtok( NULL, " "), NULL, 0);
      immediateInWords              = immediateInBytes / 4;
   } else {
      destinationRegister           = atoi(strtok( NULL, " ") + 1);
   }
   int jumpType                     = 0;
   if (isConditional) {
      jumpType                      = absoluteJumpType(strtok( NULL, " "));
   }
   
   uint8_t byte0                    = (destinationRegister & 0x3) | ((immediateInWords & 0x3f) << 2);
   uint8_t byte1                    = (immediateInWords & 0x7c0) >> 6;
   uint8_t byte2                    = ((jumpType & 0x3) << 6) | (addressInDestinationRegister << 5);
   uint8_t byte3                    = (opCode << 4) | (bit25to27 << 1) | ((jumpType & 0x4) >> 2);

   CommandBytes commandBytes = {byte0, byte1, byte2, byte3};
   return (Result){commandBytes, NULL};
}

// byte3      byte2      byte1      byte0
// ------------------------------------------
// 1098 7654  3210 9876  5432 1098  7654 3210   position
// oooo 001k  ssss sssc  tttt tttt  tttt tttt   content: o = opCode, k = sign (0 -> PC + steps, 1 -> PC - steps), s = relative step in 32-bit words, c = condition, t = threshold
static Result jumpConditionalUponR0ToRelativeAddress(uint8_t *commandAsText) {
   int opCode                       = 8;
   int bit25to27                    = 1;
   strtok((char*)commandAsText, " ");
   int stepInBytes                  = strtol(strtok( NULL, " "), NULL, 0);
   bool incrementProgramCounter     = (stepInBytes & 0x80) == 0;
   stepInBytes                      = (stepInBytes * (incrementProgramCounter ? 1 : -1)) & 0x7f;
   int stepInWords                  = stepInBytes / 4;
   int threshold                    = strtol(strtok( NULL, " "), NULL, 0);
   char *conditionAsText            = strtok( NULL, " ");
   int condition                    = (strcmp(conditionAsText, "lt") == 0) ? 0 : 1;

   uint8_t byte0                    = threshold & 0xff;
   uint8_t byte1                    = (threshold & 0xff00) >> 8;
   uint8_t byte2                    = (stepInWords << 1) | condition;
   uint8_t byte3                    = (opCode << 4) | (bit25to27 << 1) | (incrementProgramCounter ? 0 : 1);

   CommandBytes commandBytes = {byte0, byte1, byte2, byte3};
   return (Result){commandBytes, NULL};
}

// byte3      byte2      byte1      byte0
// ------------------------------------------
// 1098 7654  3210 9876  5432 1098  7654 3210   position
// oooo 010k  ssss sssc  c000 0000  tttt tttt   content: o = opCode, k = sign (0 -> PC + steps, 1 -> PC - steps), s = relative step in 32-bit words, c = condition, t = threshold
static Result jumpConditionalUponStageCountToRelativeAddress(uint8_t *commandAsText) {
   int opCode                       = 8;
   int bit25to27                    = 2;
   strtok((char*)commandAsText, " ");
   int stepInBytes                  = strtol(strtok( NULL, " "), NULL, 0);
   bool incrementProgramCounter     = (stepInBytes & 0x80) == 0;
   stepInBytes                      = (stepInBytes * (incrementProgramCounter ? 1 : -1)) & 0x7f;
   int stepInWords                  = stepInBytes / 4;
   int threshold                    = strtol(strtok( NULL, " "), NULL, 0);
   int condition                    = relativeStageCountCondition(strtok( NULL, " "));

   uint8_t byte0                    = threshold & 0xff;
   uint8_t byte1                    = (condition & 0x1) << 7;
   uint8_t byte2                    = (stepInWords << 1) | ((condition & 0x2) >> 1);
   uint8_t byte3                    = (opCode << 4) | (bit25to27 << 1) | (incrementProgramCounter ? 0 : 1);

   CommandBytes commandBytes = {byte0, byte1, byte2, byte3};
   return (Result){commandBytes, NULL};
}

// byte3      byte2      byte1      byte0
// ------------------------------------------
// 1098 7654  3210 9876  5432 1098  7654 3210   position
// oooo 0000  0000 0000  0000 0000  0smm mmdd   content: o = opCode, s = selected ADC, m = SARADC pad, d = destination register
static Result adc(uint8_t *commandAsText) {
   int opCode                       = 5;
   strtok((char*)commandAsText, " ");
   int destinationRegister          = atoi(strtok( NULL, " ") + 1);
   int sarSelect                    = strtol(strtok( NULL, " "), NULL, 0);
   int pad                          = strtol(strtok( NULL, " "), NULL, 0);

   uint8_t byte0                    = (destinationRegister & 0x3) | (pad << 2) | ((sarSelect & 0x1) << 6);
   uint8_t byte1                    = 0x00;
   uint8_t byte2                    = 0x00;
   uint8_t byte3                    = opCode << 4;

   CommandBytes commandBytes = {byte0, byte1, byte2, byte3};
   return (Result){commandBytes, NULL};
}

// byte3      byte2      byte1      byte0
// ------------------------------------------
// 1098 7654  3210 9876  5432 1098  7654 3210   position
// oooo r0ss  sshh hlll  dddd dddd  aaaa aaaa   content: o = opCode, r = communication direction, s = select register, h = bit mask (high part), l = bit mask (low part), d = data, a = slave register address
static Result i2cReadWrite(uint8_t *commandAsText) {
   int opCode                       = 3;
   char *operation                  = strtok((char*)commandAsText, " ");
   int readWrite                    = (strcmp(operation, "i2c_wr") == 0) ? 1 : 0;
   int subAddress                   = strtol(strtok( NULL, " "), NULL, 0);
   int data                         = 0;
   if (readWrite == 1) {
      data = strtol(strtok( NULL, " "), NULL, 0);
   }
   int maskHighPart                 = strtol(strtok( NULL, " "), NULL, 0);
   int maskLowPart                  = strtol(strtok( NULL, " "), NULL, 0);
   int slaveRegister                = strtol(strtok( NULL, " "), NULL, 0);

   uint8_t byte0                    = subAddress & 0xff;
   uint8_t byte1                    = data & 0xff;
   uint8_t byte2                    = maskLowPart | (maskHighPart << 3) | ((slaveRegister & 0x3) << 6);
   uint8_t byte3                    = (opCode << 4) | (readWrite << 3) | ((slaveRegister & 0xc) >> 2);

   CommandBytes commandBytes = {byte0, byte1, byte2, byte3};
   return (Result){commandBytes, NULL};
}

// byte3      byte2      byte1      byte0
// ------------------------------------------
// 1098 7654  3210 9876  5432 1098  7654 3210   position
// oooo hhhh  hlll ll00  0000 00aa  aaaa aaaa   content: o = opCode, h = register end bit number, l = register start bit number, a = register address
static Result readRegister(uint8_t *commandAsText) {
   int opCode                       = 2;
   strtok((char*)commandAsText, " ");
   int registerAddress              = strtol(strtok( NULL, " "), NULL, 0);
   int endBitNumber                 = strtol(strtok( NULL, " "), NULL, 0);
   int startBitNumber               = strtol(strtok( NULL, " "), NULL, 0);
   
   uint8_t byte0                    = registerAddress & 0xff;
   uint8_t byte1                    = (registerAddress & 0x300) >> 8;
   uint8_t byte2                    = (startBitNumber << 2) | ((endBitNumber & 0x1) << 7);
   uint8_t byte3                    = (opCode << 4) | ((endBitNumber & 0x1e) >> 1);

   CommandBytes commandBytes = {byte0, byte1, byte2, byte3};
   return (Result){commandBytes, NULL};
}

// byte3      byte2      byte1      byte0
// ------------------------------------------
// 1098 7654  3210 9876  5432 1098  7654 3210   position
// oooo hhhh  hlll lldd  dddd ddaa  aaaa aaaa   content: o = opCode, h = register end bit number, l = register start bit number, a = register address
static Result writeRegister(uint8_t *commandAsText) {
   int opCode                       = 1;
   strtok((char*)commandAsText, " ");
   int registerAddress              = strtol(strtok( NULL, " "), NULL, 0);
   int endBitNumber                 = strtol(strtok( NULL, " "), NULL, 0);
   int startBitNumber               = strtol(strtok( NULL, " "), NULL, 0);
   int data                         = strtol(strtok( NULL, " "), NULL, 0);
   
   uint8_t byte0                    = registerAddress & 0xff;
   uint8_t byte1                    = (registerAddress & 0x300) >> 8 | ((data & 0x3f) << 2);
   uint8_t byte2                    = (startBitNumber << 2) | ((endBitNumber & 0x1) << 7) | ((data & 0xc0) >> 6);
   uint8_t byte3                    = (opCode << 4) | ((endBitNumber & 0x1e) >> 1);

   CommandBytes commandBytes = {byte0, byte1, byte2, byte3};
   return (Result){commandBytes, NULL};
}

// The conditions eq, le and gt of jumpr are not supported by the ULP. The compiler replaces them by modified jumpr commands using lt and ge.
// For details visit https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/ulp_instruction_set.html#jumpr-jump-to-a-relative-offset-condition-based-on-r0.
static Result unsupportedJumpRelativeConditionalBasedOnR0(uint8_t *commandAsText){
   CommandBytes commandBytes = {0, 0, 0, 0};
   return (Result){commandBytes, UNSUPPORTED_JUMPR_R0_ERROR_MESSAGE};
}

// The conditions eq and gt of jumps are not supported by the ULP. The compiler replaces them by modified jumpr commands using lt, le and ge.
// For details visit https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/ulp_instruction_set.html#jumps-jump-to-a-relative-address-condition-based-on-stage-count.
static Result unsupportedJumpRelativeConditionalBasedOnStageCount(uint8_t *commandAsText){
   CommandBytes commandBytes = {0, 0, 0, 0};
   return (Result){commandBytes, UNSUPPORTED_JUMPR_STAGECOUNT_ERROR_MESSAGE};
}

static Result stageReset(uint8_t *commandAsText){
   return stageCountOperation(commandAsText);
}

static Result stageIncrement(uint8_t *commandAsText){
   return stageCountOperation(commandAsText);
}

static Result stageDecrement(uint8_t *commandAsText){
   return stageCountOperation(commandAsText);
}

static Result halt(uint8_t *commandAsText){
   CommandBytes commandBytes = {0x00, 0x00, 0x00, 0xb0};
   return (Result){commandBytes, NULL};
}

static Result wake(uint8_t *commandAsText){
   CommandBytes commandBytes = {0x01, 0x00, 0x00, 0x90};
   return (Result){commandBytes, NULL};
}

static Result sleep(uint8_t *commandAsText){
   strtok((char*)commandAsText, " ");
   int reg                   = strtol(strtok( NULL, " "), NULL, 0);
   CommandBytes commandBytes = {reg, 0x00, 0x00, 0x92};
   return (Result){commandBytes, NULL};
}

static Result wait(uint8_t *commandAsText){
   strtok((char*)commandAsText, " ");
   int cycles = strtol(strtok( NULL, " "), NULL, 0);
   return waitCycles(cycles);
}

static Result waitCycles(int cycles){
   uint8_t byte0             = cycles & 0xff;
   uint8_t byte1             = (cycles & 0xff00) >> 8;
   CommandBytes commandBytes = {byte0, byte1, 0x00, 0x40};
   return (Result){commandBytes, NULL};
}

static Result tsens(uint8_t *commandAsText){
   strtok((char*)commandAsText, " ");
   int reg                   = atoi(strtok( NULL, " ") + 1);
   int waitCycles            = strtol(strtok( NULL, " "), NULL, 0);
   uint8_t byte0             = reg | ((waitCycles & 0x3f) << 2);
   uint8_t byte1             = (waitCycles & 0x3fc0) >> 6;
   CommandBytes commandBytes = {byte0, byte1, 0x00, 0xa0};
   return (Result){commandBytes, NULL};
}

static Result moveImmediateToRegister(uint8_t *commandAsText) {
   return aluOperationWithImmediateValue(commandAsText);
}

static Result moveRegisterToRegister(uint8_t *commandAsText) {
   return aluOperationAmongRegisters(commandAsText);
}