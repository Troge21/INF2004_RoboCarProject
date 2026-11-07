/* Internal line-sensor operations used by the existing S3 task. */
#ifndef LINE_DETECTION_H
#define LINE_DETECTION_H

#include "subsystem3.h"

ER s3_line_init(void);
ER s3_line_configure(void);
/* Read the sensors and publish a snapshot. The task supplies a non-NULL
 * destination, which also receives the local reading for diagnostics. */
void s3_line_sample(S3_LINE_DATA *sample);

#endif /* LINE_DETECTION_H */
