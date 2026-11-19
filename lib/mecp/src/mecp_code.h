// An MECP code, such as "B01".
#pragma once

#include <stdint.h>

namespace mecp {

struct Code {
  char category;   // 'A'-'Z'; '\0' for no code
  uint8_t number;  // 0-99

  // Default-constructs to no code.
  constexpr Code(char category = '\0', uint8_t number = 0) : category(category), number(number) {}

  // Parses the code at `p`, such as "B01"; Code() if `p` does not start with one. Stops at a NUL, so
  // never reads past the end of a string.
  explicit constexpr Code(const char *p)
      : category(isCode(p) ? p[0] : '\0'),
        number(isCode(p) ? static_cast<uint8_t>((p[1] - '0') * 10 + (p[2] - '0')) : 0) {}

  constexpr bool operator==(Code o) const { return category == o.category && number == o.number; }
  constexpr bool operator!=(Code o) const { return !(*this == o); }

private:
  static constexpr bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
  static constexpr bool isDigit(char c) { return c >= '0' && c <= '9'; }
  // A code ends at a space or the end of the string.
  static constexpr bool isDelimiter(char c) { return c == ' ' || c == '\0'; }
  // && stops at the first failure, so a NUL ends the check before anything past it is read.
  static constexpr bool isCode(const char *p) {
    return isUpper(p[0]) && isDigit(p[1]) && isDigit(p[2]) && isDelimiter(p[3]);
  }
};

}  // namespace mecp
