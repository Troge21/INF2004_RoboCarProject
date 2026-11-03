#ifndef BARCODE_CAPTURE_H
#define BARCODE_CAPTURE_H
#include "../app.h"
/* Exclusive timer alarm 2 / IRQ2; all calls and sampling run on core 0.
 * GPIO IRQ13 remains owned by the existing ultrasonic/encoder bench code. */
#define S3_BC_SAMPLE_US         500U
#define S3_BC_MAX_SAMPLE_GAP_US 2000U
#define S3_BC_EDGE_CAPACITY     64U
ER s3_capture_start(UW *now_us, BOOL *black);
/* E_OK=edge; E_TMOUT=empty (current sample); E_IO=lost samples/overflow,
 * queue flushed and outputs resync the decoder. Calls never wait. */
ER s3_capture_read(UW *now_us, BOOL *black);
#endif
