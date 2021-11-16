#ifndef assembler_string_h
#define assembler_string_h

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/**
 * Replaces every character in text by its lower case representation and returns the pointer to the first character.
 * The replacement happens in place -> the returned pointer is the same pointer as text.
 */
uint8_t* toLowerCase(uint8_t* text);

/**
 * Removes leading and trailing whitespaces and returns the pointer to the first whitespace that is not a whitespace.
 * Trimming happens in place.
 */
uint8_t* trim(uint8_t* text);

#endif