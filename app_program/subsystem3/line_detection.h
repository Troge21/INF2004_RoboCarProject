/* Internal line-sensor operations used by the existing S3 task. */
#ifndef LINE_DETECTION_H
#define LINE_DETECTION_H

#include "subsystem3.h"

ER s3_line_init(void);
ER s3_line_configure(void);
/* S3 owner only: sample the two steering AO channels and publish raw/validity
 * record. Other tasks only use subsystem3_get_line(), without driver calls. */
void s3_line_sample(void);

#endif /* LINE_DETECTION_H */
