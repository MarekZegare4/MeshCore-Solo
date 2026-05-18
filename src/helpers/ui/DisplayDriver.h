#pragma once

#include <stdint.h>
#include <string.h>

class DisplayDriver {
  int _w, _h;
protected:
  DisplayDriver(int w, int h) { _w = w; _h = h; }
public:
  enum Color { DARK=0, LIGHT, RED, GREEN, BLUE, YELLOW, ORANGE }; // on b/w screen, colors will be !=0 synonym of light

  int width() const { return _w; }
  int height() const { return _h; }

  virtual bool isOn() = 0;
  virtual void turnOn() = 0;
  virtual void turnOff() = 0;
  virtual void clear() = 0;
  virtual void startFrame(Color bkg = DARK) = 0;
  virtual void setTextSize(int sz) = 0;
  virtual void setColor(Color c) = 0;
  virtual void setCursor(int x, int y) = 0;
  virtual void print(const char* str) = 0;
  virtual void printWordWrap(const char* str, int max_width) { print(str); }   // fallback to basic print() if no override
  virtual void fillRect(int x, int y, int w, int h) = 0;
  virtual void drawRect(int x, int y, int w, int h) = 0;
  virtual void drawXbm(int x, int y, const uint8_t* bits, int w, int h) = 0;
  virtual uint16_t getTextWidth(const char* str) = 0;
  virtual int getCharWidth() const { return 6; }   // advance width of a typical character (xAdvance for monospace)
  virtual int getLineHeight() const { return 8; }  // pixel rows per line (cell height, not just glyph height)
  virtual void drawTextCentered(int mid_x, int y, const char* str) {   // helper method (override to optimise)
    int w = getTextWidth(str);
    setCursor(mid_x - w/2, y);
    print(str);
  }
  virtual void drawTextRightAlign(int x_anch, int y, const char* str) {
    int w = getTextWidth(str);
    setCursor(x_anch - w, y);
    print(str);
  }
  virtual void drawTextLeftAlign(int x_anch, int y, const char* str) {
    setCursor(x_anch, y);
    print(str);
  }
  
  static char transliterateCodepoint(uint32_t cp) {
    switch (cp) {
      // Polish
      case 0x0105: return 'a'; case 0x0104: return 'A';  // ą Ą
      case 0x0107: return 'c'; case 0x0106: return 'C';  // ć Ć
      case 0x0119: return 'e'; case 0x0118: return 'E';  // ę Ę
      case 0x0142: return 'l'; case 0x0141: return 'L';  // ł Ł
      case 0x0144: return 'n'; case 0x0143: return 'N';  // ń Ń
      case 0x00F3: return 'o'; case 0x00D3: return 'O';  // ó Ó
      case 0x015B: return 's'; case 0x015A: return 'S';  // ś Ś
      case 0x017A: return 'z'; case 0x0179: return 'Z';  // ź Ź
      case 0x017C: return 'z'; case 0x017B: return 'Z';  // ż Ż
      // Czech/Slovak
      case 0x010D: return 'c'; case 0x010C: return 'C';  // č Č
      case 0x0161: return 's'; case 0x0160: return 'S';  // š Š
      case 0x017E: return 'z'; case 0x017D: return 'Z';  // ž Ž
      case 0x0159: return 'r'; case 0x0158: return 'R';  // ř Ř
      case 0x011B: return 'e'; case 0x011A: return 'E';  // ě Ě
      case 0x016F: return 'u'; case 0x016E: return 'U';  // ů Ů
      case 0x010F: return 'd'; case 0x010E: return 'D';  // ď Ď
      case 0x0165: return 't'; case 0x0164: return 'T';  // ť Ť
      case 0x0148: return 'n'; case 0x0147: return 'N';  // ň Ň
      case 0x013E: return 'l'; case 0x013D: return 'L';  // ľ Ľ
      case 0x013A: return 'l'; case 0x0139: return 'L';  // ĺ Ĺ
      case 0x0155: return 'r'; case 0x0154: return 'R';  // ŕ Ŕ
      // German
      case 0x00E4: return 'a'; case 0x00C4: return 'A';  // ä Ä
      case 0x00F6: return 'o'; case 0x00D6: return 'O';  // ö Ö
      case 0x00FC: return 'u'; case 0x00DC: return 'U';  // ü Ü
      case 0x00DF: return 's';                            // ß
      // Scandinavian
      case 0x00E5: return 'a'; case 0x00C5: return 'A';  // å Å
      case 0x00F8: return 'o'; case 0x00D8: return 'O';  // ø Ø
      case 0x00E6: return 'a'; case 0x00C6: return 'A';  // æ Æ
      // Hungarian
      case 0x0151: return 'o'; case 0x0150: return 'O';  // ő Ő
      case 0x0171: return 'u'; case 0x0170: return 'U';  // ű Ű
      // Romanian
      case 0x0103: return 'a'; case 0x0102: return 'A';  // ă Ă
      case 0x0219: return 's'; case 0x0218: return 'S';  // ș Ș
      case 0x021B: return 't'; case 0x021A: return 'T';  // ț Ț
      // Croatian
      case 0x0111: return 'd'; case 0x0110: return 'D';  // đ Đ
      // Icelandic
      case 0x00F0: return 'd'; case 0x00D0: return 'D';  // ð Ð (eth)
      case 0x00FE: return 't'; case 0x00DE: return 'T';  // þ Þ (thorn)
      // Romanian cedilla variants (legacy encoding)
      case 0x0163: return 't'; case 0x0162: return 'T';  // ţ Ţ
      // Turkish
      case 0x011F: return 'g'; case 0x011E: return 'G';  // ğ Ğ
      case 0x015F: return 's'; case 0x015E: return 'S';  // ş Ş
      case 0x0131: return 'i';                            // ı
      // Baltic (Lithuanian/Latvian)
      case 0x0101: return 'a'; case 0x0100: return 'A';  // ā Ā
      case 0x0113: return 'e'; case 0x0112: return 'E';  // ē Ē
      case 0x012B: return 'i'; case 0x012A: return 'I';  // ī Ī
      case 0x016B: return 'u'; case 0x016A: return 'U';  // ū Ū
      case 0x0123: return 'g'; case 0x0122: return 'G';  // ģ Ģ
      case 0x0137: return 'k'; case 0x0136: return 'K';  // ķ Ķ
      case 0x013C: return 'l'; case 0x013B: return 'L';  // ļ Ļ
      case 0x0146: return 'n'; case 0x0145: return 'N';  // ņ Ņ
      case 0x0157: return 'r'; case 0x0156: return 'R';  // ŗ Ŗ
      case 0x0173: return 'u'; case 0x0172: return 'U';  // ų Ų
      case 0x0117: return 'e'; case 0x0116: return 'E';  // ė Ė
      case 0x012F: return 'i'; case 0x012E: return 'I';  // į Į
      // French/Spanish/Portuguese common accents
      case 0x00E0: case 0x00E1: case 0x00E2: case 0x00E3: return 'a';
      case 0x00C0: case 0x00C1: case 0x00C2: case 0x00C3: return 'A';
      case 0x00E8: case 0x00E9: case 0x00EA: case 0x00EB: return 'e';
      case 0x00C8: case 0x00C9: case 0x00CA: case 0x00CB: return 'E';
      case 0x00EC: case 0x00ED: case 0x00EE: case 0x00EF: return 'i';
      case 0x00CC: case 0x00CD: case 0x00CE: case 0x00CF: return 'I';
      case 0x00F2: case 0x00F4: case 0x00F5: return 'o';
      case 0x00D2: case 0x00D4: case 0x00D5: return 'O';
      case 0x00F9: case 0x00FA: case 0x00FB: return 'u';
      case 0x00D9: case 0x00DA: case 0x00DB: return 'U';
      case 0x00E7: return 'c'; case 0x00C7: return 'C';  // ç Ç
      case 0x00F1: return 'n'; case 0x00D1: return 'N';  // ñ Ñ
      case 0x00FD: return 'y'; case 0x00DD: return 'Y';  // ý Ý
      default: return '\xDB';  // CP437 full block █
    }
  }

  // convert UTF-8 to ASCII, transliterating accented/diacritic characters
  virtual void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) {
    size_t j = 0;
    const uint8_t* p = (const uint8_t*)src;
    while (*p && j < dest_size - 1) {
      uint8_t c = *p++;
      if (c < 0x80) {
        if (c >= 32) dest[j++] = c;
      } else {
        uint32_t cp = c;
        if ((c & 0xE0) == 0xC0) {
          cp = c & 0x1F;
          if (*p) cp = (cp << 6) | (*p++ & 0x3F);
        } else if ((c & 0xF0) == 0xE0) {
          cp = c & 0x0F;
          if (*p) cp = (cp << 6) | (*p++ & 0x3F);
          if (*p) cp = (cp << 6) | (*p++ & 0x3F);
        } else {
          while (*p && (*p & 0xC0) == 0x80) p++;
          dest[j++] = '\xDB';
          continue;
        }
        dest[j++] = transliterateCodepoint(cp);
      }
    }
    dest[j] = 0;
  }
  
  // draw text with ellipsis if it exceeds max_width
  virtual void drawTextEllipsized(int x, int y, int max_width, const char* str) {
    char temp_str[256];  // reasonable buffer size
    translateUTF8ToBlocks(temp_str, str, sizeof(temp_str));
    
    if (getTextWidth(temp_str) <= max_width) {
      setCursor(x, y);
      print(temp_str);
      return;
    }
    
    // for variable-width fonts (GxEPD), add space after ellipsis
    // for fixed-width fonts (OLED), keep tight spacing to save precious characters
    const char* ellipsis;
    // use a simple heuristic: if 'i' and 'l' have different widths, it's variable-width
    int i_width = getTextWidth("i");
    int l_width = getTextWidth("l");
    if (i_width != l_width) {
      ellipsis = "... ";  // variable-width fonts: add space
    } else {
      ellipsis = "...";   // fixed-width fonts: no space
    }
    
    int ellipsis_width = getTextWidth(ellipsis);
    int str_len = strlen(temp_str);
    
    while (str_len > 0 && getTextWidth(temp_str) > max_width - ellipsis_width) {
      temp_str[--str_len] = 0;
    }
    strcat(temp_str, ellipsis);
    
    setCursor(x, y);
    print(temp_str);
  }
  
  virtual void setBrightness(uint8_t level) { }  // level 0-4 (min to max), no-op default
  virtual void endFrame() = 0;
};
