/*
 * Copyright 2024-2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "isp_common.h"
#include "isp_debug.h"

#include <linux/time.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/fs.h>

static wait_queue_head_t g_evt_waitq_head;
wait_queue_head_t *g_evt_waitq_headp;

int isp_event_init(struct isp_event *p_event, int auto_matic,
		   int init_state)
{
	p_event->automatic = auto_matic;
	p_event->event = init_state;
	p_event->result = 0;

	if (!g_evt_waitq_headp) {
		g_evt_waitq_headp = &g_evt_waitq_head;
		init_waitqueue_head(g_evt_waitq_headp);
	}
	return 0;
};

int isp_event_signal(unsigned int result, struct isp_event *p_event)
{
	p_event->result = result;
	p_event->event = 1;

	if (g_evt_waitq_headp)
		wake_up_interruptible(g_evt_waitq_headp);
	else
		pr_err("no head");
	pr_debug("signal evt %d,result %d", p_event->event, result);
	return 0;
};

int isp_event_wait(struct isp_event *p_event, unsigned int timeout_ms)
{
	if (g_evt_waitq_headp) {
		int temp;

		if (p_event->event)
			goto quit;

		temp = wait_event_interruptible_timeout
		       ((*g_evt_waitq_headp),
			p_event->event,
			(timeout_ms * HZ / 1000));

		if (temp == 0)
			return -ETIME;
	} else {
		pr_err("no head");
	}
quit:
	if (p_event->automatic)
		p_event->event = 0;

	pr_debug("wait evt %d suc", p_event->event);
	return p_event->result;
};
