/* Subsystem 3: stationary digital IR sensing; line/barcode processing pending. */

#include "subsystem3.h"

/* Describe which sensors see black. These patterns alone do not identify
 * a junction or a barcode, and do not request motor actions. */
LOCAL const char *ir_pattern_name(BOOL left_on_black, BOOL centre_on_black,
                                  BOOL right_on_black)
{
	/* Pack L/C/R into three bits: left=4, centre=2, right=1. */
	UINT pattern = (left_on_black ? 4U : 0U)
		     | (centre_on_black ? 2U : 0U)
		     | (right_on_black ? 1U : 0U);

	switch (pattern) {
	case 0: return "NO_BLACK";
	case 1: return "RIGHT_ONLY";
	case 2: return "CENTRE_ONLY";
	case 3: return "CENTRE_RIGHT";
	case 4: return "LEFT_ONLY";
	case 5: return "LEFT_RIGHT";
	case 6: return "LEFT_CENTRE";
	case 7: return "ALL_BLACK";
	default: return "UNKNOWN";
	}
}

/* Stationary IR test: print black-line detections and the observed pattern.
 * Bench testing confirmed that logic 1 means black for all three sensors. */
LOCAL void ir_task(INT stacd, void *exinf)
{
	const UINT pins[] = {
		S3_IR_LEFT_PIN, S3_IR_CENTRE_PIN, S3_IR_RIGHT_PIN
	};
	UINT i;
	const UW report_period_ms = 5000;
	UW report_elapsed_ms = report_period_ms; /* print the first sample */
	BOOL left_on_black, centre_on_black, right_on_black;
	ER er;

	(void)stacd; (void)exinf;

	/* This task owns configuration and reads of the three IR GPIO inputs. */
	for (i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
		er = gpio_set_pin(pins[i], GPIO_MODE_IN);
		if (er < E_OK) {
			CHECK(er, "IR gpio_set_pin");
			tk_ext_tsk();
			return;
		}
	}

	if (S3_IR_LOG_ENABLED) {
		tm_printf((UB*)"IR: left=GP%u centre=GP%u right=GP%u, sample=%u ms, report=%u ms, 1=black\n",
		  S3_IR_LEFT_PIN, S3_IR_CENTRE_PIN, S3_IR_RIGHT_PIN,
		  S3_IR_PERIOD_MS, report_period_ms);
	}

	while (1) {
		/* Convert electrical levels into explicit black-line detections. */
		left_on_black = (gpio_get_val(S3_IR_LEFT_PIN) == S3_IR_BLACK_LEVEL);
		centre_on_black = (gpio_get_val(S3_IR_CENTRE_PIN) == S3_IR_BLACK_LEVEL);
		right_on_black = (gpio_get_val(S3_IR_RIGHT_PIN) == S3_IR_BLACK_LEVEL);

		if (S3_IR_LOG_ENABLED && report_elapsed_ms >= report_period_ms) {
			tm_printf((UB*)"IR black: L=%d C=%d R=%d | pattern=%s\n",
				  left_on_black, centre_on_black, right_on_black,
				  ir_pattern_name(left_on_black, centre_on_black, right_on_black));
			report_elapsed_ms = 0;
		}
		tk_dly_tsk(S3_IR_PERIOD_MS);
		if (S3_IR_LOG_ENABLED) report_elapsed_ms += S3_IR_PERIOD_MS;
	}
}

EXPORT ER subsystem3_init(void)
{
    return E_OK;
}

EXPORT ER subsystem3_start(void)
{
    ID tid = app_start_task(ir_task, S3_PRI_IR, TP_PRC1, "ir_task");
    return tid < E_OK ? tid : E_OK;
}
