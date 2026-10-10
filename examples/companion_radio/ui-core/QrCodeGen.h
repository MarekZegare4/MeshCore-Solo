#pragma once
// QR Code generator by Project Nayuki (MIT licence, notice below), as LVGL
// bundles it (lvgl/src/libs/qrcode), made header-only and free of LVGL: every
// function static inline, so a frontend includes it once and the linker keeps
// only what it calls. Used for your own contact's QR (ui-core/ContactQr.h).
/*
 * QR Code generator library (C)
 *
 * Copyright (c) Project Nayuki. (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * - The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 * - The Software is provided "as is", without warranty of any kind, express or
 *   implied, including but not limited to the warranties of merchantability,
 *   fitness for a particular purpose and noninfringement. In no event shall the
 *   authors or copyright holders be liable for any claim, damages or other
 *   liability, whether in an action of contract, tort or otherwise, arising from,
 *   out of or in connection with the Software or the use or other dealings in the
 *   Software.
 */

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QR_ASSERT(x) ((void)0)

/*
 * This library creates QR Code symbols, which is a type of two-dimension barcode.
 * Invented by Denso Wave and described in the ISO/IEC 18004 standard.
 * A QR Code structure is an immutable square grid of black and white cells.
 * The library provides functions to create a QR Code from text or binary data.
 * The library covers the QR Code Model 2 specification, supporting all versions (sizes)
 * from 1 to 40, all 4 error correction levels, and 4 character encoding modes.
 *
 * Ways to create a QR Code object:
 * - High level: Take the payload data and call qrcodegen_encodeText() or qrcodegen_encodeBinary().
 * - Low level: Custom-make the list of segments and call
 *   qrcodegen_encodeSegments() or qrcodegen_encodeSegmentsAdvanced().
 * (Note that all ways require supplying the desired error correction level and various byte buffers.)
 */


/*---- Enum and struct types----*/

/*
 * The error correction level in a QR Code symbol.
 */
enum qrcodegen_Ecc {
    // Must be declared in ascending order of error protection
    // so that an internal qrcodegen function works properly
    qrcodegen_Ecc_LOW = 0,   // The QR Code can tolerate about  7% erroneous codewords
    qrcodegen_Ecc_MEDIUM,    // The QR Code can tolerate about 15% erroneous codewords
    qrcodegen_Ecc_QUARTILE,  // The QR Code can tolerate about 25% erroneous codewords
    qrcodegen_Ecc_HIGH,      // The QR Code can tolerate about 30% erroneous codewords
};


/*
 * The mask pattern used in a QR Code symbol.
 */
enum qrcodegen_Mask {
    // A special value to tell the QR Code encoder to
    // automatically select an appropriate mask pattern
    qrcodegen_Mask_AUTO = -1,
    // The eight actual mask patterns
    qrcodegen_Mask_0 = 0,
    qrcodegen_Mask_1,
    qrcodegen_Mask_2,
    qrcodegen_Mask_3,
    qrcodegen_Mask_4,
    qrcodegen_Mask_5,
    qrcodegen_Mask_6,
    qrcodegen_Mask_7,
};


/*
 * Describes how a segment's data bits are interpreted.
 */
enum qrcodegen_Mode {
    qrcodegen_Mode_NUMERIC      = 0x1,
    qrcodegen_Mode_ALPHANUMERIC = 0x2,
    qrcodegen_Mode_BYTE         = 0x4,
    qrcodegen_Mode_KANJI        = 0x8,
    qrcodegen_Mode_ECI          = 0x7,
};


/*
 * A segment of character/binary/control data in a QR Code symbol.
 * The mid-level way to create a segment is to take the payload data
 * and call a factory function such as qrcodegen_makeNumeric().
 * The low-level way to create a segment is to custom-make the bit buffer
 * and initialize a qrcodegen_Segment struct with appropriate values.
 * Even in the most favorable conditions, a QR Code can only hold 7089 characters of data.
 * Any segment longer than this is meaningless for the purpose of generating QR Codes.
 * Moreover, the maximum allowed bit length is 32767 because
 * the largest QR Code (version 40) has 31329 modules.
 */
struct qrcodegen_Segment {
    // The mode indicator of this segment.
    enum qrcodegen_Mode mode;

    // The length of this segment's unencoded data. Measured in characters for
    // numeric/alphanumeric/kanji mode, bytes for byte mode, and 0 for ECI mode.
    // Always zero or positive. Not the same as the data's bit length.
    int numChars;

    // The data bits of this segment, packed in bitwise big endian.
    // Can be null if the bit length is zero.
    uint8_t * data;

    // The number of valid data bits used in the buffer. Requires
    // 0 <= bitLength <= 32767, and bitLength <= (capacity of data array) * 8.
    // The character count (numChars) must agree with the mode and the bit buffer length.
    int bitLength;
};



/*---- Macro constants and functions ----*/

#define qrcodegen_VERSION_MIN   1  // The minimum version number supported in the QR Code Model 2 standard
#define qrcodegen_VERSION_MAX  40  // The maximum version number supported in the QR Code Model 2 standard

// Calculates the number of bytes needed to store any QR Code up to and including the given version number,
// as a compile-time constant. For example, 'uint8_t buffer[qrcodegen_BUFFER_LEN_FOR_VERSION(25)];'
// can store any single QR Code from version 1 to 25 (inclusive). The result fits in an int (or int16).
// Requires qrcodegen_VERSION_MIN <= n <= qrcodegen_VERSION_MAX.
#define qrcodegen_BUFFER_LEN_FOR_VERSION(n)  ((((n) * 4 + 17) * ((n) * 4 + 17) + 7) / 8 + 1)

// The worst-case number of bytes needed to store one QR Code, up to and including
// version 40. This value equals 3918, which is just under 4 kilobytes.
// Use this more convenient value to avoid calculating tighter memory bounds for buffers.
#define qrcodegen_BUFFER_LEN_MAX  qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)



/*---- Functions (high level) to generate QR Codes ----*/

/*
 * Encodes the given text string to a QR Code, returning true if encoding succeeded.
 * If the data is too long to fit in any version in the given range
 * at the given ECC level, then false is returned.
 * - The input text must be encoded in UTF-8 and contain no NULs.
 * - The variables ecl and mask must correspond to enum constant values.
 * - Requires 1 <= minVersion <= maxVersion <= 40.
 * - The arrays tempBuffer and qrcode must each have a length
 *   of at least qrcodegen_BUFFER_LEN_FOR_VERSION(maxVersion).
 * - After the function returns, tempBuffer contains no useful data.
 * - If successful, the resulting QR Code may use numeric,
 *   alphanumeric, or byte mode to encode the text.
 * - In the most optimistic case, a QR Code at version 40 with low ECC
 *   can hold any UTF-8 string up to 2953 bytes, or any alphanumeric string
 *   up to 4296 characters, or any digit string up to 7089 characters.
 *   These numbers represent the hard upper limit of the QR Code standard.
 * - Please consult the QR Code specification for information on
 *   data capacities per version, ECC level, and text encoding mode.
 */
static inline bool qrcodegen_encodeText(const char * text, uint8_t tempBuffer[], uint8_t qrcode[],
                          enum qrcodegen_Ecc ecl, int minVersion, int maxVersion, enum qrcodegen_Mask mask, bool boostEcl);


/*
 * Encodes the given binary data to a QR Code, returning true if encoding succeeded.
 * If the data is too long to fit in any version in the given range
 * at the given ECC level, then false is returned.
 * - The input array range dataAndTemp[0 : dataLen] should normally be
 *   valid UTF-8 text, but is not required by the QR Code standard.
 * - The variables ecl and mask must correspond to enum constant values.
 * - Requires 1 <= minVersion <= maxVersion <= 40.
 * - The arrays dataAndTemp and qrcode must each have a length
 *   of at least qrcodegen_BUFFER_LEN_FOR_VERSION(maxVersion).
 * - After the function returns, the contents of dataAndTemp may have changed,
 *   and does not represent useful data anymore.
 * - If successful, the resulting QR Code will use byte mode to encode the data.
 * - In the most optimistic case, a QR Code at version 40 with low ECC can hold any byte
 *   sequence up to length 2953. This is the hard upper limit of the QR Code standard.
 * - Please consult the QR Code specification for information on
 *   data capacities per version, ECC level, and text encoding mode.
 */
static inline bool qrcodegen_encodeBinary(uint8_t dataAndTemp[], size_t dataLen, uint8_t qrcode[],
                            enum qrcodegen_Ecc ecl, int minVersion, int maxVersion, enum qrcodegen_Mask mask, bool boostEcl);


/*---- Functions (low level) to generate QR Codes ----*/

/*
 * Renders a QR Code representing the given segments at the given error correction level.
 * The smallest possible QR Code version is automatically chosen for the output. Returns true if
 * QR Code creation succeeded, or false if the data is too long to fit in any version. The ECC level
 * of the result may be higher than the ecl argument if it can be done without increasing the version.
 * This function allows the user to create a custom sequence of segments that switches
 * between modes (such as alphanumeric and byte) to encode text in less space.
 * This is a low-level API; the high-level API is qrcodegen_encodeText() and qrcodegen_encodeBinary().
 * To save memory, the segments' data buffers can alias/overlap tempBuffer, and will
 * result in them being clobbered, but the QR Code output will still be correct.
 * But the qrcode array must not overlap tempBuffer or any segment's data buffer.
 */
static inline bool qrcodegen_encodeSegments(const struct qrcodegen_Segment segs[], size_t len,
                              enum qrcodegen_Ecc ecl, uint8_t tempBuffer[], uint8_t qrcode[]);


/*
 * Renders a QR Code representing the given segments with the given encoding parameters.
 * Returns true if QR Code creation succeeded, or false if the data is too long to fit in the range of versions.
 * The smallest possible QR Code version within the given range is automatically
 * chosen for the output. Iff boostEcl is true, then the ECC level of the result
 * may be higher than the ecl argument if it can be done without increasing the
 * version. The mask number is either between 0 to 7 (inclusive) to force that
 * mask, or -1 to automatically choose an appropriate mask (which may be slow).
 * This function allows the user to create a custom sequence of segments that switches
 * between modes (such as alphanumeric and byte) to encode text in less space.
 * This is a low-level API; the high-level API is qrcodegen_encodeText() and qrcodegen_encodeBinary().
 * To save memory, the segments' data buffers can alias/overlap tempBuffer, and will
 * result in them being clobbered, but the QR Code output will still be correct.
 * But the qrcode array must not overlap tempBuffer or any segment's data buffer.
 */
static inline bool qrcodegen_encodeSegmentsAdvanced(const struct qrcodegen_Segment segs[], size_t len, enum qrcodegen_Ecc ecl,
                                      int minVersion, int maxVersion, int mask, bool boostEcl, uint8_t tempBuffer[], uint8_t qrcode[]);


/*
 * Tests whether the given string can be encoded as a segment in alphanumeric mode.
 * A string is encodable iff each character is in the following set: 0 to 9, A to Z
 * (uppercase only), space, dollar, percent, asterisk, plus, hyphen, period, slash, colon.
 */
static inline bool qrcodegen_isAlphanumeric(const char * text);


/*
 * Tests whether the given string can be encoded as a segment in numeric mode.
 * A string is encodable iff each character is in the range 0 to 9.
 */
static inline bool qrcodegen_isNumeric(const char * text);


/*
 * Returns the number of bytes (uint8_t) needed for the data buffer of a segment
 * containing the given number of characters using the given mode. Notes:
 * - Returns SIZE_MAX on failure, i.e. numChars > INT16_MAX or
 *   the number of needed bits exceeds INT16_MAX (i.e. 32767).
 * - Otherwise, all valid results are in the range [0, ceil(INT16_MAX / 8)], i.e. at most 4096.
 * - It is okay for the user to allocate more bytes for the buffer than needed.
 * - For byte mode, numChars measures the number of bytes, not Unicode code points.
 * - For ECI mode, numChars must be 0, and the worst-case number of bytes is returned.
 *   An actual ECI segment can have shorter data. For non-ECI modes, the result is exact.
 */
static inline size_t qrcodegen_calcSegmentBufferSize(enum qrcodegen_Mode mode, size_t numChars);


/*
 * Returns a segment representing the given binary data encoded in
 * byte mode. All input byte arrays are acceptable. Any text string
 * can be converted to UTF-8 bytes and encoded as a byte mode segment.
 */
static inline struct qrcodegen_Segment qrcodegen_makeBytes(const uint8_t data[], size_t len, uint8_t buf[]);


/*
 * Returns a segment representing the given string of decimal digits encoded in numeric mode.
 */
static inline struct qrcodegen_Segment qrcodegen_makeNumeric(const char * digits, uint8_t buf[]);


/*
 * Returns a segment representing the given text string encoded in alphanumeric mode.
 * The characters allowed are: 0 to 9, A to Z (uppercase only), space,
 * dollar, percent, asterisk, plus, hyphen, period, slash, colon.
 */
static inline struct qrcodegen_Segment qrcodegen_makeAlphanumeric(const char * text, uint8_t buf[]);


/*
 * Returns a segment representing an Extended Channel Interpretation
 * (ECI) designator with the given assignment value.
 */
static inline struct qrcodegen_Segment qrcodegen_makeEci(long assignVal, uint8_t buf[]);


/*---- Functions to extract raw data from QR Codes ----*/

/*
 * Returns the side length of the given QR Code, assuming that encoding succeeded.
 * The result is in the range [21, 177]. Note that the length of the array buffer
 * is related to the side length - every 'uint8_t qrcode[]' must have length at least
 * qrcodegen_BUFFER_LEN_FOR_VERSION(version), which equals ceil(size^2 / 8 + 1).
 */
static inline int qrcodegen_getSize(const uint8_t qrcode[]);


/*
 * Returns the color of the module (pixel) at the given coordinates, which is false
 * for white or true for black. The top left corner has the coordinates (x=0, y=0).
 * If the given coordinates are out of bounds, then false (white) is returned.
 */
static inline bool qrcodegen_getModule(const uint8_t qrcode[], int x, int y);

/*
 * Returns the qrcode size of the specified version. Returns -1 on failure
 */
static inline int qrcodegen_version2size(int version);
/*
 * Returns the min version of the data that can be stored. Returns -1 on failure
 */
static inline int qrcodegen_getMinFitVersion(enum qrcodegen_Ecc ecl, size_t dataLen);


/*---- Forward declarations for private functions ----*/

// Regarding all public and private functions defined in this source file:
// - They require all pointer/array arguments to be not null unless the array length is zero.
// - They only read input scalar/array arguments, write to output pointer/array
//   arguments, and return scalar values; they are "pure" functions.
// - They don't read mutable global variables or write to any global variables.
// - They don't perform I/O, read the clock, print to console, etc.
// - They allocate a small and constant amount of stack memory.
// - They don't allocate or free any memory on the heap.
// - They don't recurse or mutually recurse. All the code
//   could be inlined into the top-level public functions.
// - They run in at most quadratic time with respect to input arguments.
//   Most functions run in linear time, and some in constant time.
//   There are no unbounded loops or non-obvious termination conditions.
// - They are completely thread-safe if the caller does not give the
//   same writable buffer to concurrent calls to these functions.

static inline void appendBitsToBuffer(unsigned int val, int numBits, uint8_t buffer[], int * bitLen);

static inline void addEccAndInterleave(uint8_t data[], int version, enum qrcodegen_Ecc ecl, uint8_t result[]);
static inline int getNumDataCodewords(int version, enum qrcodegen_Ecc ecl);
static inline int getNumRawDataModules(int ver);

static inline void calcReedSolomonGenerator(int degree, uint8_t result[]);
static inline void calcReedSolomonRemainder(const uint8_t data[], int dataLen,
                                       const uint8_t generator[], int degree, uint8_t result[]);
static inline uint8_t finiteFieldMultiply(uint8_t x, uint8_t y);

static inline void initializeFunctionModules(int version, uint8_t qrcode[]);
static void drawWhiteFunctionModules(uint8_t qrcode[], int version);
static void drawFormatBits(enum qrcodegen_Ecc ecl, enum qrcodegen_Mask mask, uint8_t qrcode[]);
static inline int getAlignmentPatternPositions(int version, uint8_t result[7]);
static void fillRectangle(int left, int top, int width, int height, uint8_t qrcode[]);

static void drawCodewords(const uint8_t data[], int dataLen, uint8_t qrcode[]);
static void applyMask(const uint8_t functionModules[], uint8_t qrcode[], enum qrcodegen_Mask mask);
static long getPenaltyScore(const uint8_t qrcode[]);
static void addRunToHistory(unsigned char run, unsigned char history[7]);
static bool hasFinderLikePattern(const unsigned char runHistory[7]);

static inline bool getModule(const uint8_t qrcode[], int x, int y);
static inline void setModule(uint8_t qrcode[], int x, int y, bool isBlack);
static inline void setModuleBounded(uint8_t qrcode[], int x, int y, bool isBlack);
static bool getBit(int x, int i);

static inline int calcSegmentBitLength(enum qrcodegen_Mode mode, size_t numChars);
static inline int getTotalBits(const struct qrcodegen_Segment segs[], size_t len, int version);
static int numCharCountBits(enum qrcodegen_Mode mode, int version);



/*---- Private tables of constants ----*/

// The set of all legal characters in alphanumeric mode, where each character
// value maps to the index in the string. For checking text and encoding segments.
static const char * ALPHANUMERIC_CHARSET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

// For generating error correction codes.
static inline const int8_t ECC_CODEWORDS_PER_BLOCK[4][41] = {
    // Version: (note that index 0 is for padding, and is set to an illegal value)
    //0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40    Error correction level
    {-1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},  // Low
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},  // Medium
    {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},  // Quartile
    {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},  // High
};

#define qrcodegen_REED_SOLOMON_DEGREE_MAX 30  // Based on the table above

// For generating error correction codes.
static inline const int8_t NUM_ERROR_CORRECTION_BLOCKS[4][41] = {
    // Version: (note that index 0 is for padding, and is set to an illegal value)
    //0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40    Error correction level
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4,  4,  4,  4,  4,  6,  6,  6,  6,  7,  8,  8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},  // Low
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5,  5,  8,  9,  9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},  // Medium
    {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8,  8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},  // Quartile
    {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},  // High
};

// For automatic mask pattern selection.
static const int PENALTY_N1 =  3;
static const int PENALTY_N2 =  3;
static const int PENALTY_N3 = 40;
static const int PENALTY_N4 = 10;



/*---- High-level QR Code encoding functions ----*/

// Public function - see documentation comment in header file.
static inline bool qrcodegen_encodeText(const char * text, uint8_t tempBuffer[], uint8_t qrcode[],
                          enum qrcodegen_Ecc ecl, int minVersion, int maxVersion, enum qrcodegen_Mask mask, bool boostEcl)
{

    size_t textLen = strlen(text);
    if(textLen == 0)
        return qrcodegen_encodeSegmentsAdvanced(NULL, 0, ecl, minVersion, maxVersion, mask, boostEcl, tempBuffer, qrcode);
    size_t bufLen = qrcodegen_BUFFER_LEN_FOR_VERSION(maxVersion);

    struct qrcodegen_Segment seg;
    if(qrcodegen_isNumeric(text)) {
        if(qrcodegen_calcSegmentBufferSize(qrcodegen_Mode_NUMERIC, textLen) > bufLen)
            goto fail;
        seg = qrcodegen_makeNumeric(text, tempBuffer);
    }
    else if(qrcodegen_isAlphanumeric(text)) {
        if(qrcodegen_calcSegmentBufferSize(qrcodegen_Mode_ALPHANUMERIC, textLen) > bufLen)
            goto fail;
        seg = qrcodegen_makeAlphanumeric(text, tempBuffer);
    }
    else {
        if(textLen > bufLen)
            goto fail;
        for(size_t i = 0; i < textLen; i++)
            tempBuffer[i] = (uint8_t)text[i];
        seg.mode = qrcodegen_Mode_BYTE;
        seg.bitLength = calcSegmentBitLength(seg.mode, textLen);
        if(seg.bitLength == -1)
            goto fail;
        seg.numChars = (int)textLen;
        seg.data = tempBuffer;
    }
    return qrcodegen_encodeSegmentsAdvanced(&seg, 1, ecl, minVersion, maxVersion, mask, boostEcl, tempBuffer, qrcode);

fail:
    qrcode[0] = 0;  // Set size to invalid value for safety
    return false;
}


// Public function - see documentation comment in header file.
static inline bool qrcodegen_encodeBinary(uint8_t dataAndTemp[], size_t dataLen, uint8_t qrcode[],
                            enum qrcodegen_Ecc ecl, int minVersion, int maxVersion, enum qrcodegen_Mask mask, bool boostEcl)
{

    struct qrcodegen_Segment seg;
    seg.mode = qrcodegen_Mode_BYTE;
    seg.bitLength = calcSegmentBitLength(seg.mode, dataLen);
    if(seg.bitLength == -1) {
        qrcode[0] = 0;  // Set size to invalid value for safety
        return false;
    }
    seg.numChars = (int)dataLen;
    seg.data = dataAndTemp;
    return qrcodegen_encodeSegmentsAdvanced(&seg, 1, ecl, minVersion, maxVersion, mask, boostEcl, dataAndTemp, qrcode);
}


// Appends the given number of low-order bits of the given value to the given byte-based
// bit buffer, increasing the bit length. Requires 0 <= numBits <= 16 and val < 2^numBits.
static inline void appendBitsToBuffer(unsigned int val, int numBits, uint8_t buffer[], int * bitLen)
{
    QR_ASSERT(0 <= numBits && numBits <= 16 && (unsigned long)val >> numBits == 0);
    for(int i = numBits - 1; i >= 0; i--, (*bitLen)++)
        buffer[*bitLen >> 3] |= ((val >> i) & 1) << (7 - (*bitLen & 7));
}



/*---- Low-level QR Code encoding functions ----*/

// Public function - see documentation comment in header file.
static inline bool qrcodegen_encodeSegments(const struct qrcodegen_Segment segs[], size_t len,
                              enum qrcodegen_Ecc ecl, uint8_t tempBuffer[], uint8_t qrcode[])
{
    return qrcodegen_encodeSegmentsAdvanced(segs, len, ecl,
                                            qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, -1, true, tempBuffer, qrcode);
}


// Public function - see documentation comment in header file.
static inline bool qrcodegen_encodeSegmentsAdvanced(const struct qrcodegen_Segment segs[], size_t len, enum qrcodegen_Ecc ecl,
                                      int minVersion, int maxVersion, int mask, bool boostEcl, uint8_t tempBuffer[], uint8_t qrcode[])
{
    QR_ASSERT(segs != NULL || len == 0);
    QR_ASSERT(qrcodegen_VERSION_MIN <= minVersion && minVersion <= maxVersion && maxVersion <= qrcodegen_VERSION_MAX);
    QR_ASSERT(0 <= (int)ecl && (int)ecl <= 3 && -1 <= (int)mask && (int)mask <= 7);

    // Find the minimal version number to use
    int version, dataUsedBits;
    for(version = minVersion; ; version++) {
        int dataCapacityBits = getNumDataCodewords(version, ecl) * 8;  // Number of data bits available
        dataUsedBits = getTotalBits(segs, len, version);
        if(dataUsedBits != -1 && dataUsedBits <= dataCapacityBits)
            break;  // This version number is found to be suitable
        if(version >= maxVersion) {   // All versions in the range could not fit the given data
            qrcode[0] = 0;  // Set size to invalid value for safety
            return false;
        }
    }
    QR_ASSERT(dataUsedBits != -1);

    // Increase the error correction level while the data still fits in the current version number
    for(int i = (int)qrcodegen_Ecc_MEDIUM; i <= (int)qrcodegen_Ecc_HIGH; i++) {   // From low to high
        if(boostEcl && dataUsedBits <= getNumDataCodewords(version, (enum qrcodegen_Ecc)i) * 8)
            ecl = (enum qrcodegen_Ecc)i;
    }

    // Concatenate all segments to create the data bit string
    memset(qrcode, 0, qrcodegen_BUFFER_LEN_FOR_VERSION(version) * sizeof(qrcode[0]));
    int bitLen = 0;
    for(size_t i = 0; i < len; i++) {
        const struct qrcodegen_Segment * seg = &segs[i];
        appendBitsToBuffer((int)seg->mode, 4, qrcode, &bitLen);
        appendBitsToBuffer(seg->numChars, numCharCountBits(seg->mode, version), qrcode, &bitLen);
        for(int j = 0; j < seg->bitLength; j++)
            appendBitsToBuffer((seg->data[j >> 3] >> (7 - (j & 7))) & 1, 1, qrcode, &bitLen);
    }
    QR_ASSERT(bitLen == dataUsedBits);

    // Add terminator and pad up to a byte if applicable
    int dataCapacityBits = getNumDataCodewords(version, ecl) * 8;
    QR_ASSERT(bitLen <= dataCapacityBits);
    int terminatorBits = dataCapacityBits - bitLen;
    if(terminatorBits > 4)
        terminatorBits = 4;
    appendBitsToBuffer(0, terminatorBits, qrcode, &bitLen);
    appendBitsToBuffer(0, (8 - bitLen % 8) % 8, qrcode, &bitLen);
    QR_ASSERT(bitLen % 8 == 0);

    // Pad with alternating bytes until data capacity is reached
    for(uint8_t padByte = 0xEC; bitLen < dataCapacityBits; padByte ^= 0xEC ^ 0x11)
        appendBitsToBuffer(padByte, 8, qrcode, &bitLen);

    // Draw function and data codeword modules
    addEccAndInterleave(qrcode, version, ecl, tempBuffer);
    initializeFunctionModules(version, qrcode);
    drawCodewords(tempBuffer, getNumRawDataModules(version) / 8, qrcode);
    drawWhiteFunctionModules(qrcode, version);
    initializeFunctionModules(version, tempBuffer);

    // Handle masking
    if(mask == qrcodegen_Mask_AUTO) {   // Automatically choose best mask
        long minPenalty = LONG_MAX;
        for(int i = 0; i < 8; i++) {
            enum qrcodegen_Mask msk = (enum qrcodegen_Mask)i;
            applyMask(tempBuffer, qrcode, msk);
            drawFormatBits(ecl, msk, qrcode);
            long penalty = getPenaltyScore(qrcode);
            if(penalty < minPenalty) {
                mask = msk;
                minPenalty = penalty;
            }
            applyMask(tempBuffer, qrcode, msk);  // Undoes the mask due to XOR
        }
    }
    QR_ASSERT(0 <= (int)mask && (int)mask <= 7);
    applyMask(tempBuffer, qrcode, (enum qrcodegen_Mask)mask);
    drawFormatBits(ecl, (enum qrcodegen_Mask)mask, qrcode);
    return true;
}



/*---- Error correction code generation functions ----*/

// Appends error correction bytes to each block of the given data array, then interleaves
// bytes from the blocks and stores them in the result array. data[0 : dataLen] contains
// the input data. data[dataLen : rawCodewords] is used as a temporary work area and will
// be clobbered by this function. The final answer is stored in result[0 : rawCodewords].
static inline void addEccAndInterleave(uint8_t data[], int version, enum qrcodegen_Ecc ecl, uint8_t result[])
{
    // Calculate parameter numbers
    QR_ASSERT(0 <= (int)ecl && (int)ecl < 4 && qrcodegen_VERSION_MIN <= version && version <= qrcodegen_VERSION_MAX);
    int numBlocks = NUM_ERROR_CORRECTION_BLOCKS[(int)ecl][version];
    int blockEccLen = ECC_CODEWORDS_PER_BLOCK  [(int)ecl][version];
    int rawCodewords = getNumRawDataModules(version) / 8;
    int dataLen = getNumDataCodewords(version, ecl);
    int numShortBlocks = numBlocks - rawCodewords % numBlocks;
    int shortBlockDataLen = rawCodewords / numBlocks - blockEccLen;

    // Split data into blocks, calculate ECC, and interleave
    // (not concatenate) the bytes into a single sequence
    uint8_t generator[qrcodegen_REED_SOLOMON_DEGREE_MAX];
    calcReedSolomonGenerator(blockEccLen, generator);
    const uint8_t * dat = data;
    for(int i = 0; i < numBlocks; i++) {
        int datLen = shortBlockDataLen + (i < numShortBlocks ? 0 : 1);
        uint8_t * ecc = &data[dataLen]; // Temporary storage
        calcReedSolomonRemainder(dat, datLen, generator, blockEccLen, ecc);
        for(int j = 0, k = i; j < datLen; j++, k += numBlocks) {   // Copy data
            if(j == shortBlockDataLen)
                k -= numShortBlocks;
            result[k] = dat[j];
        }
        for(int j = 0, k = dataLen + i; j < blockEccLen; j++, k += numBlocks)   // Copy ECC
            result[k] = ecc[j];
        dat += datLen;
    }
}


// Returns the number of 8-bit codewords that can be used for storing data (not ECC),
// for the given version number and error correction level. The result is in the range [9, 2956].
static inline int getNumDataCodewords(int version, enum qrcodegen_Ecc ecl)
{
    int v = version, e = (int)ecl;
    QR_ASSERT(0 <= e && e < 4);
    return getNumRawDataModules(v) / 8
           - ECC_CODEWORDS_PER_BLOCK    [e][v]
           * NUM_ERROR_CORRECTION_BLOCKS[e][v];
}


// Returns the number of data bits that can be stored in a QR Code of the given version number, after
// all function modules are excluded. This includes remainder bits, so it might not be a multiple of 8.
// The result is in the range [208, 29648]. This could be implemented as a 40-entry lookup table.
static inline int getNumRawDataModules(int ver)
{
    QR_ASSERT(qrcodegen_VERSION_MIN <= ver && ver <= qrcodegen_VERSION_MAX);
    int result = (16 * ver + 128) * ver + 64;
    if(ver >= 2) {
        int numAlign = ver / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if(ver >= 7)
            result -= 36;
    }
    return result;
}



/*---- Reed-Solomon ECC generator functions ----*/

// Calculates the Reed-Solomon generator polynomial of the given degree, storing in result[0 : degree].
static inline void calcReedSolomonGenerator(int degree, uint8_t result[])
{
    // Start with the monomial x^0
    QR_ASSERT(1 <= degree && degree <= qrcodegen_REED_SOLOMON_DEGREE_MAX);
    memset(result, 0, degree * sizeof(result[0]));
    result[degree - 1] = 1;

    // Compute the product polynomial (x - r^0) * (x - r^1) * (x - r^2) * ... * (x - r^{degree-1}),
    // drop the highest term, and store the rest of the coefficients in order of descending powers.
    // Note that r = 0x02, which is a generator element of this field GF(2^8/0x11D).
    uint8_t root = 1;
    for(int i = 0; i < degree; i++) {
        // Multiply the current product by (x - r^i)
        for(int j = 0; j < degree; j++) {
            result[j] = finiteFieldMultiply(result[j], root);
            if(j + 1 < degree)
                result[j] ^= result[j + 1];
        }
        root = finiteFieldMultiply(root, 0x02);
    }
}


// Calculates the remainder of the polynomial data[0 : dataLen] when divided by the generator[0 : degree], where all
// polynomials are in big endian and the generator has an implicit leading 1 term, storing the result in result[0 : degree].
static inline void calcReedSolomonRemainder(const uint8_t data[], int dataLen,
                                       const uint8_t generator[], int degree, uint8_t result[])
{

    // Perform polynomial division
    QR_ASSERT(1 <= degree && degree <= qrcodegen_REED_SOLOMON_DEGREE_MAX);
    memset(result, 0, degree * sizeof(result[0]));
    for(int i = 0; i < dataLen; i++) {
        uint8_t factor = data[i] ^ result[0];
        memmove(&result[0], &result[1], (degree - 1) * sizeof(result[0]));
        result[degree - 1] = 0;
        for(int j = 0; j < degree; j++)
            result[j] ^= finiteFieldMultiply(generator[j], factor);
    }
}

#undef qrcodegen_REED_SOLOMON_DEGREE_MAX


// Returns the product of the two given field elements modulo GF(2^8/0x11D).
// All inputs are valid. This could be implemented as a 256*256 lookup table.
static inline uint8_t finiteFieldMultiply(uint8_t x, uint8_t y)
{
    // Russian peasant multiplication
    uint8_t z = 0;
    for(int i = 7; i >= 0; i--) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return z;
}



/*---- Drawing function modules ----*/

// Clears the given QR Code grid with white modules for the given
// version's size, then marks every function module as black.
static inline void initializeFunctionModules(int version, uint8_t qrcode[])
{
    // Initialize QR Code
    int qrsize = version * 4 + 17;
    memset(qrcode, 0, ((qrsize * qrsize + 7) / 8 + 1) * sizeof(qrcode[0]));
    qrcode[0] = (uint8_t)qrsize;

    // Fill horizontal and vertical timing patterns
    fillRectangle(6, 0, 1, qrsize, qrcode);
    fillRectangle(0, 6, qrsize, 1, qrcode);

    // Fill 3 finder patterns (all corners except bottom right) and format bits
    fillRectangle(0, 0, 9, 9, qrcode);
    fillRectangle(qrsize - 8, 0, 8, 9, qrcode);
    fillRectangle(0, qrsize - 8, 9, 8, qrcode);

    // Fill numerous alignment patterns
    uint8_t alignPatPos[7];
    int numAlign = getAlignmentPatternPositions(version, alignPatPos);
    for(int i = 0; i < numAlign; i++) {
        for(int j = 0; j < numAlign; j++) {
            // Don't draw on the three finder corners
            if(!((i == 0 && j == 0) || (i == 0 && j == numAlign - 1) || (i == numAlign - 1 && j == 0)))
                fillRectangle(alignPatPos[i] - 2, alignPatPos[j] - 2, 5, 5, qrcode);
        }
    }

    // Fill version blocks
    if(version >= 7) {
        fillRectangle(qrsize - 11, 0, 3, 6, qrcode);
        fillRectangle(0, qrsize - 11, 6, 3, qrcode);
    }
}


// Draws white function modules and possibly some black modules onto the given QR Code, without changing
// non-function modules. This does not draw the format bits. This requires all function modules to be previously
// marked black (namely by initializeFunctionModules()), because this may skip redrawing black function modules.
static void drawWhiteFunctionModules(uint8_t qrcode[], int version)
{
    // Draw horizontal and vertical timing patterns
    int qrsize = qrcodegen_getSize(qrcode);
    for(int i = 7; i < qrsize - 7; i += 2) {
        setModule(qrcode, 6, i, false);
        setModule(qrcode, i, 6, false);
    }

    // Draw 3 finder patterns (all corners except bottom right; overwrites some timing modules)
    for(int dy = -4; dy <= 4; dy++) {
        for(int dx = -4; dx <= 4; dx++) {
            int dist = abs(dx);
            if(abs(dy) > dist)
                dist = abs(dy);
            if(dist == 2 || dist == 4) {
                setModuleBounded(qrcode, 3 + dx, 3 + dy, false);
                setModuleBounded(qrcode, qrsize - 4 + dx, 3 + dy, false);
                setModuleBounded(qrcode, 3 + dx, qrsize - 4 + dy, false);
            }
        }
    }

    // Draw numerous alignment patterns
    uint8_t alignPatPos[7];
    int numAlign = getAlignmentPatternPositions(version, alignPatPos);
    for(int i = 0; i < numAlign; i++) {
        for(int j = 0; j < numAlign; j++) {
            if((i == 0 && j == 0) || (i == 0 && j == numAlign - 1) || (i == numAlign - 1 && j == 0))
                continue;  // Don't draw on the three finder corners
            for(int dy = -1; dy <= 1; dy++) {
                for(int dx = -1; dx <= 1; dx++)
                    setModule(qrcode, alignPatPos[i] + dx, alignPatPos[j] + dy, dx == 0 && dy == 0);
            }
        }
    }

    // Draw version blocks
    if(version >= 7) {
        // Calculate error correction code and pack bits
        int rem = version;  // version is uint6, in the range [7, 40]
        for(int i = 0; i < 12; i++)
            rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        long bits = (long)version << 12 | rem;  // uint18
        QR_ASSERT(bits >> 18 == 0);

        // Draw two copies
        for(int i = 0; i < 6; i++) {
            for(int j = 0; j < 3; j++) {
                int k = qrsize - 11 + j;
                setModule(qrcode, k, i, (bits & 1) != 0);
                setModule(qrcode, i, k, (bits & 1) != 0);
                bits >>= 1;
            }
        }
    }
}


// Draws two copies of the format bits (with its own error correction code) based
// on the given mask and error correction level. This always draws all modules of
// the format bits, unlike drawWhiteFunctionModules() which might skip black modules.
static void drawFormatBits(enum qrcodegen_Ecc ecl, enum qrcodegen_Mask mask, uint8_t qrcode[])
{
    // Calculate error correction code and pack bits
    QR_ASSERT(0 <= (int)mask && (int)mask <= 7);
    static const int table[] = {1, 0, 3, 2};
    int data = table[(int)ecl] << 3 | (int)mask;  // errCorrLvl is uint2, mask is uint3
    int rem = data;
    for(int i = 0; i < 10; i++)
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = (data << 10 | rem) ^ 0x5412;  // uint15
    QR_ASSERT(bits >> 15 == 0);

    // Draw first copy
    for(int i = 0; i <= 5; i++)
        setModule(qrcode, 8, i, getBit(bits, i));
    setModule(qrcode, 8, 7, getBit(bits, 6));
    setModule(qrcode, 8, 8, getBit(bits, 7));
    setModule(qrcode, 7, 8, getBit(bits, 8));
    for(int i = 9; i < 15; i++)
        setModule(qrcode, 14 - i, 8, getBit(bits, i));

    // Draw second copy
    int qrsize = qrcodegen_getSize(qrcode);
    for(int i = 0; i < 8; i++)
        setModule(qrcode, qrsize - 1 - i, 8, getBit(bits, i));
    for(int i = 8; i < 15; i++)
        setModule(qrcode, 8, qrsize - 15 + i, getBit(bits, i));
    setModule(qrcode, 8, qrsize - 8, true);  // Always black
}


// Calculates and stores an ascending list of positions of alignment patterns
// for this version number, returning the length of the list (in the range [0,7]).
// Each position is in the range [0,177), and are used on both the x and y axes.
// This could be implemented as lookup table of 40 variable-length lists of unsigned bytes.
static inline int getAlignmentPatternPositions(int version, uint8_t result[7])
{
    if(version == 1)
        return 0;
    int numAlign = version / 7 + 2;
    int step = (version == 32) ? 26 :
               (version * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
    for(int i = numAlign - 1, pos = version * 4 + 10; i >= 1; i--, pos -= step)
        result[i] = pos;
    result[0] = 6;
    return numAlign;
}


// Sets every pixel in the range [left : left + width] * [top : top + height] to black.
static void fillRectangle(int left, int top, int width, int height, uint8_t qrcode[])
{
    for(int dy = 0; dy < height; dy++) {
        for(int dx = 0; dx < width; dx++)
            setModule(qrcode, left + dx, top + dy, true);
    }
}



/*---- Drawing data modules and masking ----*/

// Draws the raw codewords (including data and ECC) onto the given QR Code. This requires the initial state of
// the QR Code to be black at function modules and white at codeword modules (including unused remainder bits).
static void drawCodewords(const uint8_t data[], int dataLen, uint8_t qrcode[])
{
    int qrsize = qrcodegen_getSize(qrcode);
    int i = 0;  // Bit index into the data
    // Do the funny zigzag scan
    for(int right = qrsize - 1; right >= 1; right -= 2) {   // Index of right column in each column pair
        if(right == 6)
            right = 5;
        for(int vert = 0; vert < qrsize; vert++) {   // Vertical counter
            for(int j = 0; j < 2; j++) {
                int x = right - j;  // Actual x coordinate
                bool upward = ((right + 1) & 2) == 0;
                int y = upward ? qrsize - 1 - vert : vert;  // Actual y coordinate
                if(!getModule(qrcode, x, y) && i < dataLen * 8) {
                    bool black = getBit(data[i >> 3], 7 - (i & 7));
                    setModule(qrcode, x, y, black);
                    i++;
                }
                // If this QR Code has any remainder bits (0 to 7), they were assigned as
                // 0/false/white by the constructor and are left unchanged by this method
            }
        }
    }
    QR_ASSERT(i == dataLen * 8);
}


// XORs the codeword modules in this QR Code with the given mask pattern.
// The function modules must be marked and the codeword bits must be drawn
// before masking. Due to the arithmetic of XOR, calling applyMask() with
// the same mask value a second time will undo the mask. A final well-formed
// QR Code needs exactly one (not zero, two, etc.) mask applied.
static void applyMask(const uint8_t functionModules[], uint8_t qrcode[], enum qrcodegen_Mask mask)
{
    QR_ASSERT(0 <= (int)mask && (int)mask <= 7);  // Disallows qrcodegen_Mask_AUTO
    int qrsize = qrcodegen_getSize(qrcode);
    for(int y = 0; y < qrsize; y++) {
        for(int x = 0; x < qrsize; x++) {
            if(getModule(functionModules, x, y))
                continue;
            bool invert;
            switch((int)mask) {
                case 0:
                    invert = (x + y) % 2 == 0;
                    break;
                case 1:
                    invert = y % 2 == 0;
                    break;
                case 2:
                    invert = x % 3 == 0;
                    break;
                case 3:
                    invert = (x + y) % 3 == 0;
                    break;
                case 4:
                    invert = (x / 3 + y / 2) % 2 == 0;
                    break;
                case 5:
                    invert = x * y % 2 + x * y % 3 == 0;
                    break;
                case 6:
                    invert = (x * y % 2 + x * y % 3) % 2 == 0;
                    break;
                case 7:
                    invert = ((x + y) % 2 + x * y % 3) % 2 == 0;
                    break;
                default:
                    QR_ASSERT(false);
                    return;
            }
            bool val = getModule(qrcode, x, y);
            setModule(qrcode, x, y, val ^ invert);
        }
    }
}


// Calculates and returns the penalty score based on state of the given QR Code's current modules.
// This is used by the automatic mask choice algorithm to find the mask pattern that yields the lowest score.
static long getPenaltyScore(const uint8_t qrcode[])
{
    int qrsize = qrcodegen_getSize(qrcode);
    long result = 0;

    // Adjacent modules in row having same color, and finder-like patterns
    for(int y = 0; y < qrsize; y++) {
        unsigned char runHistory[7] = {0};
        bool color = false;
        unsigned char runX = 0;
        for(int x = 0; x < qrsize; x++) {
            if(getModule(qrcode, x, y) == color) {
                runX++;
                if(runX == 5)
                    result += PENALTY_N1;
                else if(runX > 5)
                    result++;
            }
            else {
                addRunToHistory(runX, runHistory);
                if(!color && hasFinderLikePattern(runHistory))
                    result += PENALTY_N3;
                color = getModule(qrcode, x, y);
                runX = 1;
            }
        }
        addRunToHistory(runX, runHistory);
        if(color)
            addRunToHistory(0, runHistory);  // Dummy run of white
        if(hasFinderLikePattern(runHistory))
            result += PENALTY_N3;
    }
    // Adjacent modules in column having same color, and finder-like patterns
    for(int x = 0; x < qrsize; x++) {
        unsigned char runHistory[7] = {0};
        bool color = false;
        unsigned char runY = 0;
        for(int y = 0; y < qrsize; y++) {
            if(getModule(qrcode, x, y) == color) {
                runY++;
                if(runY == 5)
                    result += PENALTY_N1;
                else if(runY > 5)
                    result++;
            }
            else {
                addRunToHistory(runY, runHistory);
                if(!color && hasFinderLikePattern(runHistory))
                    result += PENALTY_N3;
                color = getModule(qrcode, x, y);
                runY = 1;
            }
        }
        addRunToHistory(runY, runHistory);
        if(color)
            addRunToHistory(0, runHistory);  // Dummy run of white
        if(hasFinderLikePattern(runHistory))
            result += PENALTY_N3;
    }

    // 2*2 blocks of modules having same color
    for(int y = 0; y < qrsize - 1; y++) {
        for(int x = 0; x < qrsize - 1; x++) {
            bool  color = getModule(qrcode, x, y);
            if(color == getModule(qrcode, x + 1, y) &&
               color == getModule(qrcode, x, y + 1) &&
               color == getModule(qrcode, x + 1, y + 1))
                result += PENALTY_N2;
        }
    }

    // Balance of black and white modules
    int black = 0;
    for(int y = 0; y < qrsize; y++) {
        for(int x = 0; x < qrsize; x++) {
            if(getModule(qrcode, x, y))
                black++;
        }
    }
    int total = qrsize * qrsize;  // Note that size is odd, so black/total != 1/2
    // Compute the smallest integer k >= 0 such that (45-5k)% <= black/total <= (55+5k)%
    int k = (int)((labs(black * 20L - total * 10L) + total - 1) / total) - 1;
    result += k * PENALTY_N4;
    return result;
}


// Inserts the given value to the front of the given array, which shifts over the
// existing values and deletes the last value. A helper function for getPenaltyScore().
static void addRunToHistory(unsigned char run, unsigned char history[7])
{
    memmove(&history[1], &history[0], 6 * sizeof(history[0]));
    history[0] = run;
}


// Tests whether the given run history has the pattern of ratio 1:1:3:1:1 in the middle, and
// surrounded by at least 4 on either or both ends. A helper function for getPenaltyScore().
// Must only be called immediately after a run of white modules has ended.
static bool hasFinderLikePattern(const unsigned char runHistory[7])
{
    unsigned char n = runHistory[1];
    // The maximum QR Code size is 177, hence the run length n <= 177.
    // Arithmetic is promoted to int, so n*4 will not overflow.
    return n > 0 && runHistory[2] == n && runHistory[4] == n && runHistory[5] == n
           && runHistory[3] == n * 3 && (runHistory[0] >= n * 4 || runHistory[6] >= n * 4);
}



/*---- Basic QR Code information ----*/

// Public function - see documentation comment in header file.
static inline int qrcodegen_getSize(const uint8_t qrcode[])
{
    QR_ASSERT(qrcode != NULL);
    int result = qrcode[0];
    QR_ASSERT((qrcodegen_VERSION_MIN * 4 + 17) <= result
           && result <= (qrcodegen_VERSION_MAX * 4 + 17));
    return result;
}


// Public function - see documentation comment in header file.
static inline bool qrcodegen_getModule(const uint8_t qrcode[], int x, int y)
{
    QR_ASSERT(qrcode != NULL);
    int qrsize = qrcode[0];
    return (0 <= x && x < qrsize && 0 <= y && y < qrsize) && getModule(qrcode, x, y);
}


// Gets the module at the given coordinates, which must be in bounds.
static inline bool getModule(const uint8_t qrcode[], int x, int y)
{
    int qrsize = qrcode[0];
    QR_ASSERT(21 <= qrsize && qrsize <= 177 && 0 <= x && x < qrsize && 0 <= y && y < qrsize);
    int index = y * qrsize + x;
    return getBit(qrcode[(index >> 3) + 1], index & 7);
}


// Sets the module at the given coordinates, which must be in bounds.
static inline void setModule(uint8_t qrcode[], int x, int y, bool isBlack)
{
    int qrsize = qrcode[0];
    QR_ASSERT(21 <= qrsize && qrsize <= 177 && 0 <= x && x < qrsize && 0 <= y && y < qrsize);
    int index = y * qrsize + x;
    int bitIndex = index & 7;
    int byteIndex = (index >> 3) + 1;
    if(isBlack)
        qrcode[byteIndex] |= 1 << bitIndex;
    else
        qrcode[byteIndex] &= (1 << bitIndex) ^ 0xFF;
}


// Sets the module at the given coordinates, doing nothing if out of bounds.
static inline void setModuleBounded(uint8_t qrcode[], int x, int y, bool isBlack)
{
    int qrsize = qrcode[0];
    if(0 <= x && x < qrsize && 0 <= y && y < qrsize)
        setModule(qrcode, x, y, isBlack);
}


// Returns true iff the i'th bit of x is set to 1. Requires x >= 0 and 0 <= i <= 14.
static bool getBit(int x, int i)
{
    return ((x >> i) & 1) != 0;
}



/*---- Segment handling ----*/

// Public function - see documentation comment in header file.
static inline bool qrcodegen_isAlphanumeric(const char * text)
{
    QR_ASSERT(text != NULL);
    for(; *text != '\0'; text++) {
        if(strchr(ALPHANUMERIC_CHARSET, *text) == NULL)
            return false;
    }
    return true;
}


// Public function - see documentation comment in header file.
static inline bool qrcodegen_isNumeric(const char * text)
{
    QR_ASSERT(text != NULL);
    for(; *text != '\0'; text++) {
        if(*text < '0' || *text > '9')
            return false;
    }
    return true;
}


// Public function - see documentation comment in header file.
static inline size_t qrcodegen_calcSegmentBufferSize(enum qrcodegen_Mode mode, size_t numChars)
{
    int temp = calcSegmentBitLength(mode, numChars);
    if(temp == -1)
        return SIZE_MAX;
    QR_ASSERT(0 <= temp && temp <= INT16_MAX);
    return ((size_t)temp + 7) / 8;
}


// Returns the number of data bits needed to represent a segment
// containing the given number of characters using the given mode. Notes:
// - Returns -1 on failure, i.e. numChars > INT16_MAX or
//   the number of needed bits exceeds INT16_MAX (i.e. 32767).
// - Otherwise, all valid results are in the range [0, INT16_MAX].
// - For byte mode, numChars measures the number of bytes, not Unicode code points.
// - For ECI mode, numChars must be 0, and the worst-case number of bits is returned.
//   An actual ECI segment can have shorter data. For non-ECI modes, the result is exact.
static inline int calcSegmentBitLength(enum qrcodegen_Mode mode, size_t numChars)
{
    // All calculations are designed to avoid overflow on all platforms
    if(numChars > (unsigned int)INT16_MAX)
        return -1;
    long result = (long)numChars;
    if(mode == qrcodegen_Mode_NUMERIC)
        result = (result * 10 + 2) / 3;  // ceil(10/3 * n)
    else if(mode == qrcodegen_Mode_ALPHANUMERIC)
        result = (result * 11 + 1) / 2;  // ceil(11/2 * n)
    else if(mode == qrcodegen_Mode_BYTE)
        result *= 8;
    else if(mode == qrcodegen_Mode_KANJI)
        result *= 13;
    else if(mode == qrcodegen_Mode_ECI && numChars == 0)
        result = 3 * 8;
    else {  // Invalid argument
        QR_ASSERT(false);
        return -1;
    }
    QR_ASSERT(result >= 0);
    if((unsigned int)result > (unsigned int)INT16_MAX)
        return -1;
    return (int)result;
}


// Public function - see documentation comment in header file.
static inline struct qrcodegen_Segment qrcodegen_makeBytes(const uint8_t data[], size_t len, uint8_t buf[])
{
    QR_ASSERT(data != NULL || len == 0);
    struct qrcodegen_Segment result;
    result.mode = qrcodegen_Mode_BYTE;
    result.bitLength = calcSegmentBitLength(result.mode, len);
    QR_ASSERT(result.bitLength != -1);
    result.numChars = (int)len;
    if(len > 0)
        memcpy(buf, data, len * sizeof(buf[0]));
    result.data = buf;
    return result;
}


// Public function - see documentation comment in header file.
static inline struct qrcodegen_Segment qrcodegen_makeNumeric(const char * digits, uint8_t buf[])
{
    QR_ASSERT(digits != NULL);
    struct qrcodegen_Segment result;
    size_t len = strlen(digits);
    result.mode = qrcodegen_Mode_NUMERIC;
    int bitLen = calcSegmentBitLength(result.mode, len);
    QR_ASSERT(bitLen != -1);
    result.numChars = (int)len;
    if(bitLen > 0)
        memset(buf, 0, ((size_t)bitLen + 7) / 8 * sizeof(buf[0]));
    result.bitLength = 0;

    unsigned int accumData = 0;
    int accumCount = 0;
    for(; *digits != '\0'; digits++) {
        char c = *digits;
        QR_ASSERT('0' <= c && c <= '9');
        accumData = accumData * 10 + (unsigned int)(c - '0');
        accumCount++;
        if(accumCount == 3) {
            appendBitsToBuffer(accumData, 10, buf, &result.bitLength);
            accumData = 0;
            accumCount = 0;
        }
    }
    if(accumCount > 0)   // 1 or 2 digits remaining
        appendBitsToBuffer(accumData, accumCount * 3 + 1, buf, &result.bitLength);
    QR_ASSERT(result.bitLength == bitLen);
    result.data = buf;
    return result;
}


// Public function - see documentation comment in header file.
static inline struct qrcodegen_Segment qrcodegen_makeAlphanumeric(const char * text, uint8_t buf[])
{
    QR_ASSERT(text != NULL);
    struct qrcodegen_Segment result;
    size_t len = strlen(text);
    result.mode = qrcodegen_Mode_ALPHANUMERIC;
    int bitLen = calcSegmentBitLength(result.mode, len);
    QR_ASSERT(bitLen != -1);
    result.numChars = (int)len;
    if(bitLen > 0)
        memset(buf, 0, ((size_t)bitLen + 7) / 8 * sizeof(buf[0]));
    result.bitLength = 0;

    unsigned int accumData = 0;
    int accumCount = 0;
    for(; *text != '\0'; text++) {
        const char * temp = strchr(ALPHANUMERIC_CHARSET, *text);
        QR_ASSERT(temp != NULL);
        accumData = accumData * 45 + (unsigned int)(temp - ALPHANUMERIC_CHARSET);
        accumCount++;
        if(accumCount == 2) {
            appendBitsToBuffer(accumData, 11, buf, &result.bitLength);
            accumData = 0;
            accumCount = 0;
        }
    }
    if(accumCount > 0)   // 1 character remaining
        appendBitsToBuffer(accumData, 6, buf, &result.bitLength);
    QR_ASSERT(result.bitLength == bitLen);
    result.data = buf;
    return result;
}


// Public function - see documentation comment in header file.
static inline struct qrcodegen_Segment qrcodegen_makeEci(long assignVal, uint8_t buf[])
{
    struct qrcodegen_Segment result;
    result.mode = qrcodegen_Mode_ECI;
    result.numChars = 0;
    result.bitLength = 0;
    if(assignVal < 0) {
        QR_ASSERT(false);
    }
    else if(assignVal < (1 << 7)) {
        memset(buf, 0, 1 * sizeof(buf[0]));
        appendBitsToBuffer(assignVal, 8, buf, &result.bitLength);
    }
    else if(assignVal < (1 << 14)) {
        memset(buf, 0, 2 * sizeof(buf[0]));
        appendBitsToBuffer(2, 2, buf, &result.bitLength);
        appendBitsToBuffer(assignVal, 14, buf, &result.bitLength);
    }
    else if(assignVal < 1000000L) {
        memset(buf, 0, 3 * sizeof(buf[0]));
        appendBitsToBuffer(6, 3, buf, &result.bitLength);
        appendBitsToBuffer(assignVal >> 10, 11, buf, &result.bitLength);
        appendBitsToBuffer(assignVal & 0x3FF, 10, buf, &result.bitLength);
    }
    else {
        QR_ASSERT(false);
    }
    result.data = buf;
    return result;
}


// Calculates the number of bits needed to encode the given segments at the given version.
// Returns a non-negative number if successful. Otherwise returns -1 if a segment has too
// many characters to fit its length field, or the total bits exceeds INT16_MAX.
static inline int getTotalBits(const struct qrcodegen_Segment segs[], size_t len, int version)
{
    QR_ASSERT(segs != NULL || len == 0);
    long result = 0;
    for(size_t i = 0; i < len; i++) {
        int numChars  = segs[i].numChars;
        int bitLength = segs[i].bitLength;
        QR_ASSERT(0 <= numChars  && numChars  <= INT16_MAX);
        QR_ASSERT(0 <= bitLength && bitLength <= INT16_MAX);
        int ccbits = numCharCountBits(segs[i].mode, version);
        QR_ASSERT(0 <= ccbits && ccbits <= 16);
        if(numChars >= (1L << ccbits))
            return -1;  // The segment's length doesn't fit the field's bit width
        result += 4L + ccbits + bitLength;
        if(result > INT16_MAX)
            return -1;  // The sum might overflow an int type
    }
    QR_ASSERT(0 <= result && result <= INT16_MAX);
    return (int)result;
}


// Returns the bit width of the character count field for a segment in the given mode
// in a QR Code at the given version number. The result is in the range [0, 16].
static int numCharCountBits(enum qrcodegen_Mode mode, int version)
{
    QR_ASSERT(qrcodegen_VERSION_MIN <= version && version <= qrcodegen_VERSION_MAX);
    int i = (version + 7) / 17;
    switch(mode) {
        case qrcodegen_Mode_NUMERIC     : {
                static const int temp[] = {10, 12, 14};
                return temp[i];
            }
        case qrcodegen_Mode_ALPHANUMERIC: {
                static const int temp[] = { 9, 11, 13};
                return temp[i];
            }
        case qrcodegen_Mode_BYTE        : {
                static const int temp[] = { 8, 16, 16};
                return temp[i];
            }
        case qrcodegen_Mode_KANJI       : {
                static const int temp[] = { 8, 10, 12};
                return temp[i];
            }
        case qrcodegen_Mode_ECI         :
            return 0;
        default:
            QR_ASSERT(false);
            return -1;  // Dummy value
    }
}

static inline int qrcodegen_getMinFitVersion(enum qrcodegen_Ecc ecl, size_t dataLen)
{
    struct qrcodegen_Segment seg;
    seg.mode = qrcodegen_Mode_BYTE;
    seg.bitLength = calcSegmentBitLength(seg.mode, dataLen);
    seg.numChars = (int)dataLen;

    for(int version = qrcodegen_VERSION_MIN; version <= qrcodegen_VERSION_MAX; version++) {
        int dataCapacityBits = getNumDataCodewords(version, ecl) * 8;  // Number of data bits available
        int dataUsedBits = getTotalBits(&seg, 1, version);
        if(dataUsedBits != -1 && dataUsedBits <= dataCapacityBits)
            return version;
    }
    return -1;
}

static inline int qrcodegen_version2size(int version)
{
    if(version < qrcodegen_VERSION_MIN || version > qrcodegen_VERSION_MAX) {
        return -1;
    }

    return ((version - 1) * 4 + 21);
}

#undef QR_ASSERT
