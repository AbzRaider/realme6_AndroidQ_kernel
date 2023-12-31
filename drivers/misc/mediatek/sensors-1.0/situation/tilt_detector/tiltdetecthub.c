/* tiltdetecthub motion sensor driver
 *
 * Copyright (C) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#define pr_fmt(fmt) "[tiltdetecthub] " fmt

#include <hwmsensor.h>
#include "tiltdetecthub.h"
#include <situation.h>
#include <SCP_sensorHub.h>
#include <linux/notifier.h>
#include "scp_helper.h"

static struct situation_init_info tiltdetecthub_init_info;

static int tilt_detect_get_data(int *probability, int *status)
{
	int err = 0;
	struct data_unit_t data;
	uint64_t time_stamp = 0;

	err = sensor_get_data_from_hub(ID_TILT_DETECTOR, &data);
	if (err < 0) {
		pr_err("sensor_get_data_from_hub fail!!\n");
		return -1;
	}
	time_stamp		= data.time_stamp;
	*probability	= data.gesture_data_t.probability;
	return 0;
}
static int tilt_detect_open_report_data(int open)
{
	int ret = 0;
#if defined CONFIG_MTK_SCP_SENSORHUB_V1
	if (open == 1)
		ret = sensor_set_delay_to_hub(ID_TILT_DETECTOR, 120);
#elif defined CONFIG_NANOHUB

#else

#endif
	ret = sensor_enable_to_hub(ID_TILT_DETECTOR, open);
	return ret;
}
static int tilt_detect_batch(int flag,
	int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs)
{
	return sensor_batch_to_hub(ID_TILT_DETECTOR,
		flag, samplingPeriodNs, maxBatchReportLatencyNs);
}

static int tilt_detect_flush(void)
{
	return sensor_flush_to_hub(ID_TILT_DETECTOR);
}

static int tilt_detect_recv_data(struct data_unit_t *event, void *reserved)
{
	int err = 0;

	if (event->flush_action == FLUSH_ACTION)
		err = situation_flush_report(ID_TILT_DETECTOR);
#ifdef ODM_HQ_EDIT
	else if (event->flush_action == DATA_ACTION)
		err = situation_data_report_t(ID_TILT_DETECTOR,
			!event->tilt_event.state, (int64_t)event->time_stamp);
	printk("tilt_detect state[%d]\n",!event->tilt_event.state);
#else
	else if (event->flush_action == DATA_ACTION)
		err = situation_data_report_t(ID_TILT_DETECTOR,
			event->tilt_event.state, (int64_t)event->time_stamp);
#endif
	return err;
}

static int tiltdetecthub_local_init(void)
{
	struct situation_control_path ctl = {0};
	struct situation_data_path data = {0};
	int err = 0;

	ctl.open_report_data = tilt_detect_open_report_data;
	ctl.batch = tilt_detect_batch;
	ctl.flush = tilt_detect_flush;
	ctl.is_support_wake_lock = true;
	ctl.is_support_batch = false;
	err = situation_register_control_path(&ctl, ID_TILT_DETECTOR);
	if (err) {
		pr_err("register tilt_detect control path err\n");
		goto exit;
	}

	data.get_data = tilt_detect_get_data;
	err = situation_register_data_path(&data, ID_TILT_DETECTOR);
	if (err) {
		pr_err("register tilt_detect data path err\n");
		goto exit;
	}
	err = scp_sensorHub_data_registration(ID_TILT_DETECTOR,
		tilt_detect_recv_data);
	if (err) {
		pr_err("SCP_sensorHub_data_registration fail!!\n");
		goto exit;
	}
	return 0;
exit:
	return -1;
}
static int tiltdetecthub_local_uninit(void)
{
	return 0;
}

static struct situation_init_info tiltdetecthub_init_info = {
	.name = "tilt_detect_hub",
	.init = tiltdetecthub_local_init,
	.uninit = tiltdetecthub_local_uninit,
};

static int __init tiltdetecthub_init(void)
{
	situation_driver_add(&tiltdetecthub_init_info, ID_TILT_DETECTOR);
	return 0;
}

static void __exit tiltdetecthub_exit(void)
{
	pr_debug("%s\n", __func__);
}

module_init(tiltdetecthub_init);
module_exit(tiltdetecthub_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GLANCE_GESTURE_HUB driver");
MODULE_AUTHOR("hongxu.zhao@mediatek.com");
