/* On-board barcode software checks, enabled only in TEST=1 builds. */
#include "subsystem3.h"

#if APP_TEST
#include "../test_report.h"

EXPORT void subsystem3_test(void)
{
    /* Synthetic widths; this checks software, never sensor performance. */
    uint32_t widths[29], reversed[29];
    const unsigned patterns[3] = { 0x094, 0x109, 0x094 }; /* *A* */
    unsigned i, j;
    uint8_t code;
    TEST_INIT();
    for (i = 0; i < 3U; ++i) {
        for (j = 0; j < 9U; ++j)
            widths[i * 10U + j] = (patterns[i] & (1U << (8U - j))) ? 15000U : 5000U;
        if (i < 2U) widths[i * 10U + 9U] = 5000U;
    }
    TEST_CHECK("S3-U06", s3_barcode_decode(widths, 50000, 50000, &code) == S3_BC_OK &&
               s3_barcode_character(code) == 'A' && !(code & S3_BC_REVERSED),
               "synthetic forward *A*");
    for (i = 0; i < 29U; ++i) reversed[i] = widths[28U - i];
    TEST_CHECK("S3-U07", s3_barcode_decode(reversed, 50000, 50000, &code) == S3_BC_OK &&
               s3_barcode_character(code) == 'A' && (code & S3_BC_REVERSED),
               "synthetic reverse *A*");
    TEST_CHECK("S3-U08", s3_barcode_decode(widths, 1000, 50000, &code) == S3_BC_INVALID &&
               code == 0, "short leading quiet rejected");
    widths[0] = 15000;
    TEST_CHECK("S3-U09", s3_barcode_decode(widths, 50000, 50000, &code) == S3_BC_INVALID &&
               code == 0, "malformed start rejected");
    TEST_CHECK("S3-U10", s3_barcode_character(0) == '\0' &&
               s3_barcode_character(0xbf) == '\0', "invalid packed codes rejected");
    TEST_SUMMARY("subsystem3 software (sensor not tested)");
}
#endif
