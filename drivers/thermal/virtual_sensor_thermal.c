/*
 * Copyright (C) 2013 Lab126, Inc.  All rights reserved.
 * Author: Akwasi Boateng <boatenga@lab126.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <linux/atomic.h>

#include <linux/platform_data/mtk_thermal.h>
#include <linux/thermal_framework.h>

#ifdef CONFIG_AMAZON_METRICS_LOG
#include <linux/metricslog.h>
#define TMP103_METRICS_STR_LEN 128
static unsigned long virtual_sensor_temp = 25000;
#endif

#include "thermal_core.h"

#define DRIVER_NAME "virtual_sensor-thermal"
#define THERMAL_NAME "virtual_sensor"
#define BUF_SIZE 128
#define DMF 1000
#define MASK (0x001F)
#define TEMPERATURE_PRINT_INTERVAL_SECONDS	10

static LIST_HEAD(thermal_sensor_list);
static DEFINE_MUTEX(therm_lock);

#ifdef CONFIG_AMAZON_METRICS_LOG
unsigned long get_virtualsensor_temp(void)
{
	return virtual_sensor_temp/1000;
}
EXPORT_SYMBOL(get_virtualsensor_temp);
#endif

struct virtual_sensor_thermal_zone {
	struct thermal_zone_device *tz;
	struct work_struct therm_work;
	struct mtk_thermal_platform_data *pdata;
};

static int virtual_sensor_match_cdev(struct thermal_cooling_device *cdev,
				     struct trip_t *trip,
				     int *index)
{
	int i;
	if (!strlen(cdev->type))
		return -EINVAL;

	for (i = 0; i < THERMAL_MAX_TRIPS; i++)
		if (!strcmp(cdev->type, trip->cdev[i].type)) {
			*index = i;
			return 0;
		}
	return -ENODEV;
}

static int virtual_sensor_cdev_bind(struct thermal_zone_device *thermal,
				    struct thermal_cooling_device *cdev)
{
	struct virtual_sensor_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;
	struct trip_t *trip = NULL;
	struct cdev_t *cool_dev = NULL;
	int index = -1;

	unsigned long max_state, upper, lower;
	int i, ret = -EINVAL;

	cdev->ops->get_max_state(cdev, &max_state);

	for (i = 0; i < pdata->num_trips; i++) {
		trip = &pdata->trips[i];

		if (virtual_sensor_match_cdev(cdev, trip, &index))
			continue;

		if (index == -1)
			return -EINVAL;

		cool_dev = &(trip->cdev[index]);
		lower = cool_dev->lower;
		upper =  cool_dev->upper > max_state ? max_state : cool_dev->upper;
		ret = thermal_zone_bind_cooling_device(thermal,
						       i,
						       cdev,
						       upper,
						       lower);
		dev_info(&cdev->device, "%s bind to %d: %d-%s\n", cdev->type,
			 i, ret, ret ? "fail" : "succeed");
	}
	return ret;
}

static int virtual_sensor_cdev_unbind(struct thermal_zone_device *thermal,
				      struct thermal_cooling_device *cdev)
{
	struct virtual_sensor_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;
	struct trip_t *trip;
	int i, ret = -EINVAL;
	int index = -1;

	for (i = 0; i < pdata->num_trips; i++) {
		trip = &pdata->trips[i];
		if (virtual_sensor_match_cdev(cdev, trip, &index))
			continue;
		ret = thermal_zone_unbind_cooling_device(thermal, i, cdev);
		dev_info(&cdev->device, "%s unbind from %d: %s\n", cdev->type,
			 i, ret ? "fail" : "succeed");
	}
	return ret;
}


static int virtual_sensor_thermal_get_temp(struct thermal_zone_device *thermal,
					   unsigned long *t)
{
	struct thermal_dev *tdev;
	struct virtual_sensor_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;
	long temp = 0;
	long tempv = 0;
	int alpha, offset, weight;
	static unsigned sec_counter = 0; /* Timer counter: 1sec */

#ifdef CONFIG_AMAZON_METRICS_LOG
#define PREFIX "thermalsensor:def"
        char buf[TMP103_METRICS_STR_LEN];
        static unsigned int mask = 0x1FFF;
        unsigned int count;
        static atomic_t query_count;
        int i;
#endif

	if (!tzone || !pdata)
		return -EINVAL;

#ifdef CONFIG_AMAZON_METRICS_LOG
        count = atomic_read(&query_count);
        atomic_inc(&query_count);
        i = 0;
#endif

	list_for_each_entry(tdev, &thermal_sensor_list, node) {
		temp = tdev->dev_ops->get_temp(tdev);
		alpha = tdev->tdp->alpha;
		offset = tdev->tdp->offset;
		weight = tdev->tdp->weight;

        #ifdef CONFIG_AMAZON_METRICS_LOG
                if (!(count & mask)) {
                        snprintf(buf, TMP103_METRICS_STR_LEN,
                                "%s:pcbmonitor=%d;CT;1,%s_temp=%lu;CT;1:NA",
                                PREFIX, i++, tdev->name, temp);
                        log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
                }
	#endif
		if (0 == sec_counter)
			pr_warning("%s %s t=%ld a=%d o=%d w=%d\n",
			       __func__,
			       tdev->name,
			       temp,
			       alpha,
			       offset,
			       weight);

		if (!tdev->off_temp)
			tdev->off_temp = temp - offset;
		else {
			tdev->off_temp = alpha * (temp - offset) +
				(DMF - alpha) * tdev->off_temp;
			tdev->off_temp /= DMF;
		}
		tempv += (weight * tdev->off_temp)/DMF;

		pr_debug("%s tempv=%ld\n", __func__, tempv);
	}

	sec_counter++;
	if (TEMPERATURE_PRINT_INTERVAL_SECONDS == sec_counter)
		sec_counter = 0;

#ifdef CONFIG_AMAZON_METRICS_LOG
        if (!(count & mask)) {
                snprintf(buf, TMP103_METRICS_STR_LEN,
                        "%s:pcbmonitor=%d;CT;1,pcb_virtual_sensor_temp=%lu;CT;1:NR",
                        PREFIX, i++, tempv);
                log_to_metrics(ANDROID_LOG_INFO, "ThermalEvent", buf);
        }
        if (tempv > pdata->trips[0].temp)
        /* Log in metrics around every 4 mins */
                mask = 0xFF;
        else
        /* Log in metrics around every 2 hours */
                mask = 0x1FFF;
#endif

#ifdef CONFIG_AMAZON_METRICS_LOG
	virtual_sensor_temp = (unsigned long) tempv;
#endif
	*t = tempv; /* back to unsigned expected by linux framework */
	return 0;
}
static int virtual_sensor_thermal_get_mode(struct thermal_zone_device *thermal,
					   enum thermal_device_mode *mode)
{
	struct virtual_sensor_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;
	if (!pdata)
		return -EINVAL;

	mutex_lock(&therm_lock);
	*mode = pdata->mode;
	mutex_unlock(&therm_lock);
	return 0;
}
static int virtual_sensor_thermal_set_mode(struct thermal_zone_device *thermal,
					   enum thermal_device_mode mode)
{
	struct virtual_sensor_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	mutex_lock(&therm_lock);
	pdata->mode = mode;
	if (mode == THERMAL_DEVICE_DISABLED) {
		tzone->tz->polling_delay = 0;
		thermal_zone_device_update(tzone->tz);
		mutex_unlock(&therm_lock);
		return 0;
	}
	schedule_work(&tzone->therm_work);
	mutex_unlock(&therm_lock);
	return 0;
}
static int virtual_sensor_thermal_get_trip_type(struct thermal_zone_device *thermal,
						int trip,
						enum thermal_trip_type *type)
{
	struct virtual_sensor_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*type = pdata->trips[trip].type;
	return 0;
}
static int virtual_sensor_thermal_get_trip_temp(struct thermal_zone_device *thermal,
						int trip,
						unsigned long *temp)
{
	struct virtual_sensor_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*temp = pdata->trips[trip].temp;
	return 0;
}
static int virtual_sensor_thermal_set_trip_temp(struct thermal_zone_device *thermal,
						int trip,
						unsigned long temp)
{
	struct virtual_sensor_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	pdata->trips[trip].temp = temp;
	return 0;
}
static int virtual_sensor_thermal_get_crit_temp(struct thermal_zone_device *thermal,
						unsigned long *temp)
{
	int i;
	struct virtual_sensor_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	for (i = 0; i < THERMAL_MAX_TRIPS; i++) {
		if (pdata->trips[i].type == THERMAL_TRIP_CRITICAL) {
			*temp = pdata->trips[i].temp;
			return 0;
		}
	}
	return -EINVAL;
}
static int virtual_sensor_thermal_get_trip_hyst(struct thermal_zone_device *thermal,
						int trip,
						unsigned long *hyst)
{
	struct virtual_sensor_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	*hyst = pdata->trips[trip].hyst;
	return 0;
}
static int virtual_sensor_thermal_set_trip_hyst(struct thermal_zone_device *thermal,
						int trip,
						unsigned long hyst)
{
	struct virtual_sensor_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	pdata->trips[trip].hyst = hyst;
	return 0;
}

#ifdef CONFIG_AUSTIN_PROJECT
void last_kmsg_thermal_shutdown(void)
{
	int rc;
	char *argv[] = {
		"/sbin/crashreport",
		"thermal_shutdown",
		NULL
	};

	pr_err("%s: start to save last kmsg\n", __func__);
	//UMH_WAIT_PROC UMH_WAIT_EXEC
	rc = call_usermodehelper(argv[0], argv, NULL, UMH_WAIT_EXEC);
	pr_err("%s: save last kmsg finish\n", __func__);

	if (rc < 0)
		pr_err("call /sbin/crashreport failed, rc = %d\n", rc);

	msleep(6000); /* 6000ms */
}
EXPORT_SYMBOL_GPL(last_kmsg_thermal_shutdown);
#endif

static int virtual_sensor_thermal_notify(struct thermal_zone_device *thermal,
					 int trip,
					 enum thermal_trip_type type)
{
	char data[20];
	char *envp[] = { data, NULL};
	snprintf(data, sizeof(data), "%s", "SHUTDOWN_WARNING");
	kobject_uevent_env(&thermal->device.kobj, KOBJ_CHANGE, envp);

#ifdef CONFIG_AUSTIN_PROJECT
	pr_err("%s: thermal_shutdown notify\n", __func__);
	last_kmsg_thermal_shutdown();
	pr_err("%s: thermal_shutdown notify end\n", __func__);
#endif
	return 0;
}
static struct thermal_zone_device_ops virtual_sensor_tz_dev_ops = {
	.bind = virtual_sensor_cdev_bind,
	.unbind = virtual_sensor_cdev_unbind,
	.get_temp = virtual_sensor_thermal_get_temp,
	.get_mode = virtual_sensor_thermal_get_mode,
	.set_mode = virtual_sensor_thermal_set_mode,
	.get_trip_type = virtual_sensor_thermal_get_trip_type,
	.get_trip_temp = virtual_sensor_thermal_get_trip_temp,
	.set_trip_temp = virtual_sensor_thermal_set_trip_temp,
	.get_crit_temp = virtual_sensor_thermal_get_crit_temp,
	.get_trip_hyst = virtual_sensor_thermal_get_trip_hyst,
	.set_trip_hyst = virtual_sensor_thermal_set_trip_hyst,
	.notify = virtual_sensor_thermal_notify,
};
static int params_show(struct device *dev,
		       struct device_attribute *devattr, char *buf)
{
	int o = 0;
	int a = 0;
	int w = 0;
	char pbufo[BUF_SIZE];
	char pbufa[BUF_SIZE];
	char pbufw[BUF_SIZE];
	int alpha, offset, weight;
	struct thermal_dev *tdev;

	o += sprintf(pbufo + o, "offsets ");
	a += sprintf(pbufa + a, "alphas ");
	w += sprintf(pbufw + w, "weights ");

	list_for_each_entry(tdev, &thermal_sensor_list, node) {
		alpha = tdev->tdp->alpha;
		offset = tdev->tdp->offset;
		weight = tdev->tdp->weight;

		o += sprintf(pbufo + o, "%d ", offset);
		a += sprintf(pbufa + a, "%d ", alpha);
		w += sprintf(pbufw + w, "%d ", weight);
	}
	return sprintf(buf, "%s\n%s\n%s\n", pbufo, pbufa, pbufw);
}
static ssize_t trips_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	return sprintf(buf, "%d\n", thermal->trips);
}
static ssize_t trips_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int trips = 0;
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	struct virtual_sensor_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;
	if (sscanf(buf, "%d\n", &trips) != 1)
		return -EINVAL;
	if (trips < 0)
		return -EINVAL;

	pdata->num_trips = trips;
	thermal->trips = pdata->num_trips;
	return count;
}
static ssize_t polling_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	return sprintf(buf, "%d\n", thermal->polling_delay);
}
static ssize_t polling_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int polling_delay = 0;
	struct thermal_zone_device *thermal = container_of(dev, struct thermal_zone_device, device);
	struct virtual_sensor_thermal_zone *tzone = thermal->devdata;
	struct mtk_thermal_platform_data *pdata = tzone->pdata;

	if (!tzone || !pdata)
		return -EINVAL;

	if (sscanf(buf, "%d\n", &polling_delay) != 1)
		return -EINVAL;
	if (polling_delay < 0)
		return -EINVAL;

	pdata->polling_delay = polling_delay;
	thermal->polling_delay = pdata->polling_delay;
	thermal_zone_device_update(thermal);
	return count;
}

static DEVICE_ATTR(trips, S_IRUGO | S_IWUSR, trips_show, trips_store);
static DEVICE_ATTR(polling, S_IRUGO | S_IWUSR, polling_show, polling_store);
static DEVICE_ATTR(params, S_IRUGO, params_show, NULL);

static int virtual_sensor_create_sysfs(struct virtual_sensor_thermal_zone *tzone)
{
	int ret = 0;
	ret = device_create_file(&tzone->tz->device, &dev_attr_params);
	if (ret)
		pr_err("%s Failed to create polling attr\n", __func__);
	ret = device_create_file(&tzone->tz->device, &dev_attr_polling);
	if (ret)
		pr_err("%s Failed to create polling attr\n", __func__);
	ret = device_create_file(&tzone->tz->device, &dev_attr_trips);
	if (ret)
		pr_err("%s Failed to create trips attr\n", __func__);
	return ret;
}
static void virtual_sensor_thermal_work(struct work_struct *work)
{
	struct virtual_sensor_thermal_zone *tzone;
	struct mtk_thermal_platform_data *pdata;

	mutex_lock(&therm_lock);
	tzone = container_of(work, struct virtual_sensor_thermal_zone, therm_work);
	if (!tzone)
		return;
	pdata = tzone->pdata;
	if (!pdata)
		return;
	if (pdata->mode == THERMAL_DEVICE_ENABLED)
		thermal_zone_device_update(tzone->tz);
	mutex_unlock(&therm_lock);
}
static int virtual_sensor_thermal_probe(struct platform_device *pdev)
{
	int ret;
	struct virtual_sensor_thermal_zone *tzone;
	struct mtk_thermal_platform_data *pdata = NULL;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata)
		return -EINVAL;

	tzone = devm_kzalloc(&pdev->dev, sizeof(*tzone), GFP_KERNEL);
	if (!tzone)
		return -ENOMEM;
	memset(tzone, 0, sizeof(*tzone));

	tzone->pdata = pdata;
	tzone->tz = thermal_zone_device_register(THERMAL_NAME,
						 pdata->num_trips,
						 MASK,
						 tzone,
						 &virtual_sensor_tz_dev_ops,
						 NULL,
						 0,
						 pdata->polling_delay);
	if (IS_ERR(tzone->tz)) {
		pr_err("%s Failed to register thermal zone device\n", __func__);
		kfree(tzone);
		return -EINVAL;
	}

	tzone->tz->trips = pdata->num_trips;
	ret = virtual_sensor_create_sysfs(tzone);
	INIT_WORK(&tzone->therm_work, virtual_sensor_thermal_work);
	platform_set_drvdata(pdev, tzone);
	pdata->mode = THERMAL_DEVICE_ENABLED;
	return ret;
}
static int virtual_sensor_thermal_remove(struct platform_device *pdev)
{
	struct virtual_sensor_thermal_zone *tzone = platform_get_drvdata(pdev);
	if (tzone) {
		cancel_work_sync(&tzone->therm_work);
		if (tzone->tz)
			thermal_zone_device_unregister(tzone->tz);
		kfree(tzone);
	}
	return 0;
}
int thermal_dev_register(struct thermal_dev *tdev)
{
	if (unlikely(IS_ERR_OR_NULL(tdev))) {
		pr_err("%s: NULL sensor thermal device\n", __func__);
		return -ENODEV;
	}
	if (!tdev->dev_ops->get_temp) {
		pr_err("%s: Error getting get_temp()\n", __func__);
		return -EINVAL;
	}
	mutex_lock(&therm_lock);
	list_add_tail(&tdev->node, &thermal_sensor_list);
	mutex_unlock(&therm_lock);
	return 0;
}
EXPORT_SYMBOL(thermal_dev_register);

static struct platform_driver virtual_sensor_thermal_zone_driver = {
	.probe = virtual_sensor_thermal_probe,
	.remove = virtual_sensor_thermal_remove,
	.suspend = NULL,
	.resume = NULL,
	.shutdown   = NULL,
	.driver     = {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init virtual_sensor_thermal_init(void)
{
	return platform_driver_register(&virtual_sensor_thermal_zone_driver);
}
static void __exit virtual_sensor_thermal_exit(void)
{
	platform_driver_unregister(&virtual_sensor_thermal_zone_driver);
}

late_initcall(virtual_sensor_thermal_init);
module_exit(virtual_sensor_thermal_exit);

MODULE_DESCRIPTION("VIRTUAL_SENSOR pcb virtual sensor thermal zone driver");
MODULE_AUTHOR("Akwasi Boateng <boatenga@amazon.com>");
MODULE_LICENSE("GPL");
