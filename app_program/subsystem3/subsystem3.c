/*
 *	subsystem3.c
 *	Subsystem 3 -- Consumer example and stationary IR sensor test.
 *
 *	APIs shown: tk_cre_mbx, tk_cre_mtx
 */

#include "subsystem3.h"

EXPORT ID		mbx_samples;	/* IF-01 */
EXPORT ID		mtx_stats;	/* IF-04 */
EXPORT APP_STATS	app_stats;

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

	tm_printf((UB*)"IR: left=GP%u centre=GP%u right=GP%u, period=%u ms, 1=black\n",
		  S3_IR_LEFT_PIN, S3_IR_CENTRE_PIN, S3_IR_RIGHT_PIN,
		  S3_IR_PERIOD_MS);

	while (1) {
		/* Convert electrical levels into explicit black-line detections. */
		left_on_black = (gpio_get_val(S3_IR_LEFT_PIN) == S3_IR_BLACK_LEVEL);
		centre_on_black = (gpio_get_val(S3_IR_CENTRE_PIN) == S3_IR_BLACK_LEVEL);
		right_on_black = (gpio_get_val(S3_IR_RIGHT_PIN) == S3_IR_BLACK_LEVEL);

		tm_printf((UB*)"IR black: L=%d C=%d R=%d | pattern=%s\n",
			  left_on_black, centre_on_black, right_on_black,
			  ir_pattern_name(left_on_black, centre_on_black, right_on_black));
		tk_dly_tsk(S3_IR_PERIOD_MS);
	}
}

EXPORT ER subsystem3_init(void)
{
	T_CMBX	cmbx = {0};
	T_CMTX	cmtx = {0};

	/* Mailbox: passes a POINTER to a message (zero copy).  The message
	   memory must stay valid until the receiver is done with it, which
	   is why subsystem 2 takes it from its memory pool. */
	cmbx.mbxatr = TA_TFIFO | TA_MFIFO;	/* FIFO waiters, FIFO messages */
	mbx_samples = tk_cre_mbx(&cmbx);
	if (mbx_samples < E_OK) {
		return mbx_samples;
	}

	/* Mutex: only one task at a time may touch app_stats.  TA_INHERIT
	   (priority inheritance) avoids priority inversion: a low-priority
	   holder is temporarily raised to the waiter's priority. */
	cmtx.mtxatr = TA_INHERIT;
	mtx_stats = tk_cre_mtx(&cmtx);
	return (mtx_stats < E_OK) ? mtx_stats : E_OK;
}

EXPORT ER subsystem3_start(void)
{
	/* Pinned to core 0 (TP_PRC1): opposite core to the producer */
	ID tid = app_start_task(consumer_task, S3_PRI_CONSUMER, TP_PRC1,
				"consumer_task");
	if (tid < E_OK) {
		return tid;
	}

	/* Keep consuming S2 samples while the separate task tests our IR pins. */
	tid = app_start_task(ir_task, S3_PRI_IR, TP_PRC1,
			     "ir_task");
	return (tid < E_OK) ? tid : E_OK;
}
