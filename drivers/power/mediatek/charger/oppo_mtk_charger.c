/*
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

/*
 *
 * Filename:
 * ---------
 *    mtk_charger.c
 *
 * Project:
 * --------
 *   Android_Software
 *
 * Description:
 * ------------
 *   This Module defines functions of Battery charging
 *
 * Author:
 * -------
 * Wy Chuang
 *
 */
#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/wakelock.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>

#include "mtk_charger_intf.h"
#include <mt-plat/charger_type.h>
#include <mt-plat/mtk_battery.h>
#include <mt-plat/mtk_boot.h>
#include <musb_core.h>
#include <pmic.h>
#include <mtk_gauge_time_service.h>


#if defined(VENDOR_EDIT) && defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Add for charging */
#include "../../oppo/oppo_charger.h"
#include "../../../misc/mediatek/usb_c/tcpc/inc/tcpci.h"
extern int charger_ic_flag;
extern int mt6370_check_charging_enable(void);
extern int mt6370_suspend_charger(bool suspend);
extern int mt6370_set_rechg_voltage(int rechg_mv);
extern int mt6370_reset_charger(void);
extern int mt6370_set_chging_term_disable(bool disable);
extern int mt6370_aicl_enable(bool enable);
extern int mt6370_set_register(unsigned char addr, unsigned char mask, unsigned char data);
extern int battery_meter_get_charger_voltage(void);
extern bool pmic_chrdet_status(void);
extern int mt_get_chargerid_volt(void);
extern void mt_set_chargerid_switch_val(int value);
extern int mt_get_chargerid_switch_val(void);
extern bool oppo_chg_get_shortc_hw_gpio_status(void);
static int oppo_mt6370_reset_charger(void);
static int oppo_mt6370_enable_charging(void);
static int oppo_mt6370_disable_charging(void);
static int oppo_mt6370_float_voltage_write(int vflaot_mv);
static int oppo_mt6370_suspend_charger(void);
static int oppo_mt6370_unsuspend_charger(void);
static int oppo_mt6370_charging_current_write_fast(int chg_curr);
static int oppo_mt6370_set_termchg_current(int term_curr);
static int oppo_mt6370_set_rechg_voltage(int rechg_mv);
#endif /*VENDOR_EDIT && CONFIG_OPPO_CHARGER_MT6370_TYPEC */

static struct charger_manager *pinfo;
static struct list_head consumer_head = LIST_HEAD_INIT(consumer_head);
static DEFINE_MUTEX(consumer_mutex);

#define USE_FG_TIMER 1

bool is_power_path_supported(void)
{
	if (pinfo == NULL)
		return false;

	if (pinfo->data.power_path_support == true)
		return true;

	return false;
}

bool is_disable_charger(void)
{
	if (pinfo == NULL)
		return true;

	if (pinfo->disable_charger == true || IS_ENABLED(CONFIG_POWER_EXT))
		return true;
	else
		return false;
}

void BATTERY_SetUSBState(int usb_state_value)
{
	if (is_disable_charger()) {
		chr_err("[BATTERY_SetUSBState] in FPGA/EVB, no service\r\n");
	} else {
		if ((usb_state_value < USB_SUSPEND) || ((usb_state_value > USB_CONFIGURED))) {
			chr_err("[BATTERY] BAT_SetUSBState Fail! Restore to default value\r\n");
			usb_state_value = USB_UNCONFIGURED;
		} else {
			chr_err("[BATTERY] BAT_SetUSBState Success! Set %d\r\n",
					usb_state_value);
			if (pinfo)
				pinfo->usb_state = usb_state_value;
		}
	}
}

unsigned int set_chr_input_current_limit(int current_limit)
{
	return 500;
}

int get_chr_temperature(int *min_temp, int *max_temp)
{
	*min_temp = 25;
	*max_temp = 30;

	return 0;
}

int set_chr_boost_current_limit(unsigned int current_limit)
{
	return 0;
}

int set_chr_enable_otg(unsigned int enable)
{
	return 0;
}

int mtk_chr_is_charger_exist(unsigned char *exist)
{
	if (mt_get_charger_type() == CHARGER_UNKNOWN)
		*exist = 0;
	else
		*exist = 1;
	return 0;
}

/*=============== fix me==================*/
int chargerlog_level = CHRLOG_ERROR_LEVEL;

int chr_get_debug_level(void)
{
	return chargerlog_level;
}

#ifdef MTK_CHARGER_EXP
#include <linux/string.h>

char chargerlog[1000];
#define LOG_LENGTH 500
int chargerlog_level = 10;
int chargerlogIdx;

int charger_get_debug_level(void)
{
	return chargerlog_level;
}

void charger_log(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsprintf(chargerlog + chargerlogIdx, fmt, args);
	va_end(args);
	chargerlogIdx = strlen(chargerlog);
	if (chargerlogIdx >= LOG_LENGTH) {
		chr_err("%s", chargerlog);
		chargerlogIdx = 0;
		memset(chargerlog, 0, 1000);
	}
}

void charger_log_flash(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsprintf(chargerlog + chargerlogIdx, fmt, args);
	va_end(args);
	chr_err("%s", chargerlog);
	chargerlogIdx = 0;
	memset(chargerlog, 0, 1000);
}
#endif

void _wake_up_charger(struct charger_manager *info)
{
#if defined(VENDOR_EDIT) && defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	return;
#else
	unsigned long flags;

	if (info == NULL)
		return;

	spin_lock_irqsave(&info->slock, flags);
	if (wake_lock_active(&info->charger_wakelock) == 0)
		wake_lock(&info->charger_wakelock);
	spin_unlock_irqrestore(&info->slock, flags);
	info->charger_thread_timeout = true;
	wake_up(&info->wait_que);
#endif /* VENDOR_EDIT && CONFIG_OPPO_CHARGER_MT6370_TYPEC */
}

/* charger_manager ops  */
static int _mtk_charger_change_current_setting(struct charger_manager *info)
{
	if (info != NULL && info->change_current_setting)
		return info->change_current_setting(info);

	return 0;
}

static int _mtk_charger_do_charging(struct charger_manager *info, bool en)
{
	if (info != NULL && info->do_charging)
		info->do_charging(info, en);
	return 0;
}
/* charger_manager ops end */


/* user interface */
struct charger_consumer *charger_manager_get_by_name(struct device *dev,
	const char *name)
{
	struct charger_consumer *puser;

	puser = kzalloc(sizeof(struct charger_consumer), GFP_KERNEL);
	if (puser == NULL)
		return NULL;

	mutex_lock(&consumer_mutex);
	puser->dev = dev;

	list_add(&puser->list, &consumer_head);
	if (pinfo != NULL)
		puser->cm = pinfo;

	mutex_unlock(&consumer_mutex);

	return puser;
}
EXPORT_SYMBOL(charger_manager_get_by_name);

int charger_manager_enable_high_voltage_charging(struct charger_consumer *consumer,
	bool en)
{
	struct charger_manager *info = consumer->cm;
	struct list_head *pos;
	struct list_head *phead = &consumer_head;
	struct charger_consumer *ptr;

	if (!info)
		return -EINVAL;

	pr_debug("[%s] %s, %d\n", __func__, dev_name(consumer->dev), en);

	if (!en && consumer->hv_charging_disabled == false)
		consumer->hv_charging_disabled = true;
	else if (en && consumer->hv_charging_disabled == true)
		consumer->hv_charging_disabled = false;
	else {
		pr_info("[%s] already set: %d %d\n", __func__,
			consumer->hv_charging_disabled, en);
		return 0;
	}

	mutex_lock(&consumer_mutex);
	list_for_each(pos, phead) {
		ptr = container_of(pos, struct charger_consumer, list);
		if (ptr->hv_charging_disabled == true) {
			info->enable_hv_charging = false;
			break;
		}
		if (list_is_last(pos, phead))
			info->enable_hv_charging = true;
	}
	mutex_unlock(&consumer_mutex);

	pr_info("%s: user: %s, en = %d\n", __func__, dev_name(consumer->dev),
		info->enable_hv_charging);
	_wake_up_charger(info);

	return 0;
}
EXPORT_SYMBOL(charger_manager_enable_high_voltage_charging);

int charger_manager_enable_power_path(struct charger_consumer *consumer,
	int idx, bool en)
{
	int ret = 0;
	bool is_en = true;
	struct charger_manager *info = consumer->cm;
	struct charger_device *chg_dev;


	if (!info)
		return -EINVAL;

	switch (idx) {
	case MAIN_CHARGER:
		chg_dev = info->chg1_dev;
		break;
	case SLAVE_CHARGER:
		chg_dev = info->chg2_dev;
		break;
	case DIRECT_CHARGER:
		chg_dev = info->dc_chg;
		break;
	default:
		return -EINVAL;
	}

	ret = charger_dev_is_powerpath_enabled(chg_dev, &is_en);
	if (ret < 0) {
		chr_err("%s: get is power path enabled failed\n", __func__);
		return ret;
	}
	if (is_en == en) {
		chr_err("%s: power path is already en = %d\n", __func__, is_en);
		return 0;
	}

	pr_info("%s: enable power path = %d\n", __func__, en);
	return charger_dev_enable_powerpath(chg_dev, en);
}

static int _charger_manager_enable_charging(struct charger_consumer *consumer,
	int idx, bool en)
{
	struct charger_manager *info = consumer->cm;

	chr_err("%s: dev:%s idx:%d en:%d\n", __func__, dev_name(consumer->dev),
		idx, en);

	if (info != NULL) {
		struct charger_data *pdata;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			return -ENOTSUPP;

		if (en == false) {
				_mtk_charger_do_charging(info, en);
			pdata->disable_charging_count++;
		} else {
			if (pdata->disable_charging_count == 1) {
				_mtk_charger_do_charging(info, en);
				pdata->disable_charging_count = 0;
			} else if (pdata->disable_charging_count > 1)
				pdata->disable_charging_count--;
		}
		chr_err("%s: dev:%s idx:%d en:%d cnt:%d\n", __func__, dev_name(consumer->dev),
			idx, en, pdata->disable_charging_count);

		return 0;
	}
	return -EBUSY;

}

int charger_manager_enable_charging(struct charger_consumer *consumer,
	int idx, bool en)
{
	struct charger_manager *info = consumer->cm;
	int ret = 0;

#if defined(VENDOR_EDIT) && defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	return -EBUSY;
#endif

	mutex_lock(&info->charger_lock);
	ret = _charger_manager_enable_charging(consumer, idx, en);
	mutex_unlock(&info->charger_lock);
	return ret;
}

int charger_manager_set_input_current_limit(struct charger_consumer *consumer,
	int idx, int input_current)
{
	struct charger_manager *info = consumer->cm;

#if defined(VENDOR_EDIT) && defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	return -EBUSY;
#endif

	if (info != NULL) {
		struct charger_data *pdata;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			return -ENOTSUPP;

		pdata->thermal_input_current_limit = input_current;
		chr_err("%s: dev:%s idx:%d en:%d\n", __func__, dev_name(consumer->dev),
		idx, input_current);
		_mtk_charger_change_current_setting(info);
		_wake_up_charger(info);
		return 0;
	}
	return -EBUSY;
}

int charger_manager_set_charging_current_limit(struct charger_consumer *consumer,
	int idx, int charging_current)
{
	struct charger_manager *info = consumer->cm;

#if defined(VENDOR_EDIT) && defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	return -EBUSY;
#endif

	if (info != NULL) {
		struct charger_data *pdata;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			return -ENOTSUPP;

		pdata->thermal_charging_current_limit = charging_current;
		chr_err("%s: dev:%s idx:%d en:%d\n", __func__, dev_name(consumer->dev),
		idx, charging_current);
		_mtk_charger_change_current_setting(info);
		_wake_up_charger(info);
		return 0;
	}
	return -EBUSY;
}

int charger_manager_get_charger_temperature(struct charger_consumer *consumer,
	int idx, int *tchg_min,	int *tchg_max)
{
	struct charger_manager *info = consumer->cm;

#if defined(VENDOR_EDIT) && defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	return -EBUSY;
#endif

	if (info != NULL) {
		struct charger_data *pdata;

		if (!upmu_get_rgs_chrdet()) {
			pr_debug("[%s] No cable in, skip it\n", __func__);
			*tchg_min = -127;
			*tchg_max = -127;
			return -EINVAL;
		}

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			return -ENOTSUPP;

		*tchg_min = pdata->junction_temp_min;
		*tchg_max = pdata->junction_temp_max;

		return 0;
	}
	return -EBUSY;
}

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
int charger_manager_force_charging_current(struct charger_consumer *consumer,
	int idx, int charging_current)
{
	struct charger_manager *info = consumer->cm;

	if (info != NULL) {
		struct charger_data *pdata;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg1_data;
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg2_data;
		else
			return -ENOTSUPP;

		pdata->force_charging_current = charging_current;
		_mtk_charger_change_current_setting(info);
		_wake_up_charger(info);
		return 0;
	}
	return -EBUSY;
}

int charger_manager_set_pe30_input_current_limit(struct charger_consumer *consumer,
	int idx, int input_current)
{
	struct charger_manager *info = consumer->cm;

	if (info != NULL) {
		mtk_pe30_set_charging_current_limit(info, input_current / 1000);
		return 0;
	}
	return -EBUSY;
}

int charger_manager_get_pe30_input_current_limit(struct charger_consumer *consumer,
	int idx, int *input_current, int *min_current, int *max_current)
{
	struct charger_manager *info = consumer->cm;

	if (info != NULL) {
		*input_current = mtk_pe30_get_charging_current_limit(info) * 1000;
		*min_current = info->data.cc_end * 1000;
		*max_current = info->data.cc_init * 1000;
		return 0;
	}
	return -EBUSY;
}
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

int charger_manager_get_current_charging_type(struct charger_consumer *consumer)
{
	struct charger_manager *info = consumer->cm;

	if (info != NULL) {
	if (mtk_pe20_get_is_connect(info))
		return 2;

	if (mtk_is_pe30_running(info))
		return 3;
	}

	return 0;
}

int charger_manager_get_zcv(struct charger_consumer *consumer, int idx, u32 *uV)
{
	struct charger_manager *info = consumer->cm;
	int ret = 0;
	struct charger_device *pchg;


	if (info != NULL) {
		if (idx == MAIN_CHARGER) {
			pchg = info->chg1_dev;
			ret = charger_dev_get_zcv(pchg, uV);
		} else if (idx == SLAVE_CHARGER) {
			pchg = info->chg2_dev;
			ret = charger_dev_get_zcv(pchg, uV);
		} else
			ret = -1;

	} else {
		chr_err("charger_manager_get_zcv info is null\n");
	}
	chr_err("charger_manager_get_zcv zcv:%d ret:%d\n", *uV, ret);

	return 0;
}

int charger_manager_enable_kpoc_shutdown(struct charger_consumer *consumer,
	bool en)
{
#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	struct charger_manager *info = consumer->cm;

	if (en)
		atomic_set(&info->enable_kpoc_shdn, 1);
	else
		atomic_set(&info->enable_kpoc_shdn, 0);
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */
	return 0;
}

int register_charger_manager_notifier(struct charger_consumer *consumer,
	struct notifier_block *nb)
{
	int ret = 0;
	struct charger_manager *info = consumer->cm;


	mutex_lock(&consumer_mutex);
	if (info != NULL)
	ret = srcu_notifier_chain_register(&info->evt_nh, nb);
	else
		consumer->pnb = nb;
	mutex_unlock(&consumer_mutex);

	return ret;
}

int unregister_charger_manager_notifier(struct charger_consumer *consumer,
				struct notifier_block *nb)
{
	int ret = 0;
	struct charger_manager *info = consumer->cm;

	mutex_lock(&consumer_mutex);
	if (info != NULL)
		ret = srcu_notifier_chain_unregister(&info->evt_nh, nb);
	else
		consumer->pnb = NULL;
	mutex_unlock(&consumer_mutex);

	return ret;
}

/* user interface end*/

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
/* factory mode */
#define CHARGER_DEVNAME "charger_ftm"
#define GET_IS_SLAVE_CHARGER_EXIST _IOW('k', 13, int)

static struct class *charger_class;
static struct cdev *charger_cdev;
static int charger_major;
static dev_t charger_devno;

static int is_slave_charger_exist(void)
{
	if (get_charger_by_name("secondary_chg") == NULL)
		return 0;
	return 1;
}

static long charger_ftm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	int out_data = 0;
	void __user *user_data = (void __user *)arg;

	switch (cmd) {
	case GET_IS_SLAVE_CHARGER_EXIST:
		out_data = is_slave_charger_exist();
		ret = copy_to_user(user_data, &out_data, sizeof(out_data));
		chr_err("[%s] GET_IS_SLAVE_CHARGER_EXIST: %d\n", __func__, out_data);
		break;
	default:
		chr_err("[%s] Error ID\n", __func__);
		break;
	}

	return ret;
}
#ifdef CONFIG_COMPAT
static long charger_ftm_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;

	switch (cmd) {
	case GET_IS_SLAVE_CHARGER_EXIST:
		ret = file->f_op->unlocked_ioctl(file, cmd, arg);
		break;
	default:
		chr_err("[%s] Error ID\n", __func__);
		break;
	}

	return ret;
}
#endif
static int charger_ftm_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int charger_ftm_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations charger_ftm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = charger_ftm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = charger_ftm_compat_ioctl,
#endif
	.open = charger_ftm_open,
	.release = charger_ftm_release,
};

void charger_ftm_init(void)
{
	struct class_device *class_dev = NULL;
	int ret = 0;

	ret = alloc_chrdev_region(&charger_devno, 0, 1, CHARGER_DEVNAME);
	if (ret < 0) {
		chr_err("[%s]Can't get major num for charger_ftm\n", __func__);
		return;
	}

	charger_cdev = cdev_alloc();
	if (!charger_cdev) {
		chr_err("[%s]cdev_alloc fail\n", __func__);
		goto unregister;
	}
	charger_cdev->owner = THIS_MODULE;
	charger_cdev->ops = &charger_ftm_fops;

	ret = cdev_add(charger_cdev, charger_devno, 1);
	if (ret < 0) {
		chr_err("[%s] cdev_add failed\n", __func__);
		goto free_cdev;
	}

	charger_major = MAJOR(charger_devno);
	charger_class = class_create(THIS_MODULE, CHARGER_DEVNAME);
	if (IS_ERR(charger_class)) {
		chr_err("[%s] class_create failed\n", __func__);
		goto free_cdev;
	}

	class_dev = (struct class_device *)device_create(charger_class,
				NULL, charger_devno, NULL, CHARGER_DEVNAME);
	if (IS_ERR(class_dev)) {
		chr_err("[%s] device_create failed\n", __func__);
		goto free_class;
	}

	pr_debug("%s done\n", __func__);
	return;

free_class:
	class_destroy(charger_class);
free_cdev:
	cdev_del(charger_cdev);
unregister:
	unregister_chrdev_region(charger_devno, 1);
}
/* factory mode end */
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

/* internal algorithm common function */
bool is_dual_charger_supported(struct charger_manager *info)
{
	if (info->chg2_dev == NULL)
		return false;
	return true;
}

int charger_enable_vbus_ovp(struct charger_manager *pinfo, bool enable)
{
	int ret = 0;
	u32 sw_ovp = 0;

	if (enable)
		sw_ovp = pinfo->data.max_charger_voltage_setting;
	else
		sw_ovp = 15000000;

	/* Enable/Disable SW OVP status */
	pinfo->data.max_charger_voltage = sw_ovp;

	chr_err("[charger_enable_vbus_ovp] en:%d ovp:%d\n",
			    enable, sw_ovp);
	return ret;
}

bool is_typec_adapter(struct charger_manager *info)
{
	if (info->pd_type == PD_CONNECT_TYPEC_ONLY_SNK &&
			tcpm_inquire_typec_remote_rp_curr(info->tcpc) != 500 &&
			info->chr_type != STANDARD_HOST &&
			info->chr_type != CHARGING_HOST &&
			mtk_pe20_get_is_connect(info) == false &&
			mtk_pe_get_is_connect(info) == false &&
			info->enable_type_c == true)
		return true;

	return false;
}

/* internal algorithm common function end */

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
/* sw jeita */
void do_sw_jeita_state_machine(struct charger_manager *info)
{
	struct sw_jeita_data *sw_jeita;

	sw_jeita = &info->sw_jeita;
	sw_jeita->pre_sm = sw_jeita->sm;
	sw_jeita->charging = true;
	/* JEITA battery temp Standard */

	if (info->battery_temperature >= info->data.temp_t4_threshold) {
		chr_err("[SW_JEITA] Battery Over high Temperature(%d) !!\n\r",
			    info->data.temp_t4_threshold);

		sw_jeita->sm = TEMP_ABOVE_T4;
		sw_jeita->charging = false;
	} else if (info->battery_temperature > info->data.temp_t3_threshold) {	/* control 45c to normal behavior */
		if ((sw_jeita->sm == TEMP_ABOVE_T4)
		    && (info->battery_temperature >= info->data.temp_t4_thres_minus_x_degree)) {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n\r",
				    info->data.temp_t4_thres_minus_x_degree, info->data.temp_t4_threshold);

			sw_jeita->charging = false;
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n\r",
				    info->data.temp_t3_threshold, info->data.temp_t4_threshold);

			sw_jeita->sm = TEMP_T3_TO_T4;
		}
	} else if (info->battery_temperature >= info->data.temp_t2_threshold) {
		if (((sw_jeita->sm == TEMP_T3_TO_T4)
		     && (info->battery_temperature >= info->data.temp_t3_thres_minus_x_degree))
		    || ((sw_jeita->sm == TEMP_T1_TO_T2)
			&& (info->battery_temperature <= info->data.temp_t2_thres_plus_x_degree))) {
			chr_err("[SW_JEITA] Battery Temperature not recovery to normal temperature charging mode yet!!\n\r");
		} else {
			chr_err("[SW_JEITA] Battery Normal Temperature between %d and %d !!\n\r",
				    info->data.temp_t2_threshold, info->data.temp_t3_threshold);
			sw_jeita->sm = TEMP_T2_TO_T3;
		}
	} else if (info->battery_temperature >= info->data.temp_t1_threshold) {
		if ((sw_jeita->sm == TEMP_T0_TO_T1 || sw_jeita->sm == TEMP_BELOW_T0)
		    && (info->battery_temperature <= info->data.temp_t1_thres_plus_x_degree)) {
			if (sw_jeita->sm == TEMP_T0_TO_T1) {
				chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n\r",
					    info->data.temp_t1_thres_plus_x_degree, info->data.temp_t2_threshold);
			}
			if (sw_jeita->sm == TEMP_BELOW_T0) {
				chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n\r",
					    info->data.temp_t1_threshold, info->data.temp_t1_thres_plus_x_degree);
				sw_jeita->charging = false;
			}
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n\r",
				    info->data.temp_t1_threshold, info->data.temp_t2_threshold);

			sw_jeita->sm = TEMP_T1_TO_T2;
		}
	} else if (info->battery_temperature >= info->data.temp_t0_threshold) {
		if ((sw_jeita->sm == TEMP_BELOW_T0)
		    && (info->battery_temperature <= info->data.temp_t0_thres_plus_x_degree)) {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n\r",
				    info->data.temp_t0_threshold, info->data.temp_t0_thres_plus_x_degree);

			sw_jeita->charging = false;
		} else {
			chr_err("[SW_JEITA] Battery Temperature between %d and %d !!\n\r",
				    info->data.temp_t0_threshold, info->data.temp_t1_threshold);

			sw_jeita->sm = TEMP_T0_TO_T1;
		}
	} else {
		chr_err("[SW_JEITA] Battery below low Temperature(%d) !!\n\r",
			    info->data.temp_t0_threshold);
		sw_jeita->sm = TEMP_BELOW_T0;
		sw_jeita->charging = false;
	}

	/* set CV after temperature changed */
	/* In normal range, we adjust CV dynamically */
	if (sw_jeita->sm != TEMP_T2_TO_T3) {
		if (sw_jeita->sm == TEMP_ABOVE_T4)
			sw_jeita->cv = info->data.jeita_temp_above_t4_cv_voltage;
		else if (sw_jeita->sm == TEMP_T3_TO_T4)
			sw_jeita->cv = info->data.jeita_temp_t3_to_t4_cv_voltage;
		else if (sw_jeita->sm == TEMP_T2_TO_T3)
			sw_jeita->cv = 0;
		else if (sw_jeita->sm == TEMP_T1_TO_T2)
			sw_jeita->cv = info->data.jeita_temp_t1_to_t2_cv_voltage;
		else if (sw_jeita->sm == TEMP_T0_TO_T1)
			sw_jeita->cv = info->data.jeita_temp_t0_to_t1_cv_voltage;
		else if (sw_jeita->sm == TEMP_BELOW_T0)
			sw_jeita->cv = info->data.jeita_temp_below_t0_cv_voltage;
		else
			sw_jeita->cv = info->data.battery_cv;
	} else {
		sw_jeita->cv = 0;
	}

	chr_err("[SW_JEITA]preState:%d newState:%d tmp:%d cv:%d\n\r",
		sw_jeita->pre_sm, sw_jeita->sm, info->battery_temperature, sw_jeita->cv);
}

static ssize_t show_sw_jeita(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	chr_err("show_sw_jeita: %d\n", pinfo->enable_sw_jeita);
	return sprintf(buf, "%d\n", pinfo->enable_sw_jeita);
}

static ssize_t store_sw_jeita(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_sw_jeita = false;
		else
			pinfo->enable_sw_jeita = true;

	} else {
		chr_err("store_sw_jeita: format error!\n");
	}
	return size;
}

static DEVICE_ATTR(sw_jeita, 0664, show_sw_jeita,
		   store_sw_jeita);
/* sw jeita end*/

/* pump express series */
bool mtk_is_pep_series_connect(struct charger_manager *info)
{
	if (mtk_is_pe30_running(info) || mtk_pe20_get_is_connect(info))
		return true;

	return false;
}

static ssize_t show_pe20(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	chr_err("show_pe20: %d\n", pinfo->enable_pe_2);
	return sprintf(buf, "%d\n", pinfo->enable_pe_2);
}

static ssize_t store_pe20(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_pe_2 = false;
		else
			pinfo->enable_pe_2 = true;

	} else {
		chr_err("store_pe20: format error!\n");
	}
	return size;
}

static DEVICE_ATTR(pe20, 0664, show_pe20,
		   store_pe20);

static ssize_t show_pe30(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	chr_err("show_pe30: %d\n", pinfo->enable_pe_3);
	return sprintf(buf, "%d\n", pinfo->enable_pe_3);
}

static ssize_t store_pe30(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_pe_3 = false;
		else
			pinfo->enable_pe_3 = true;

	} else {
		chr_err("store_pe30: format error!\n");
	}
	return size;
}

static DEVICE_ATTR(pe30, 0664, show_pe30,
		   store_pe30);

/* pump express series end*/

static ssize_t show_charger_log_level(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	chr_err("[show_charger_log_level] show chargerlog_level : %d\n", chargerlog_level);
	return sprintf(buf, "%d\n", chargerlog_level);
}

static ssize_t store_charger_log_level(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	unsigned long val = 0;
	int ret;

	chr_err("[store_charger_log_level]\n");

	if (buf != NULL && size != 0) {
		chr_err("[store_charger_log_level] buf is %s\n", buf);
		ret = kstrtoul(buf, 10, &val);
		if (val < 0) {
			chr_err("[store_charger_log_level] val is %d ??\n", (int)val);
			val = 0;
		}
		chargerlog_level = val;
		chr_err("[FG_daemon_log_level] gFG_daemon_log_level=%d\n", chargerlog_level);
	}
	return size;
}
static DEVICE_ATTR(charger_log_level, 0664, show_charger_log_level, store_charger_log_level);

static ssize_t show_pdc_max_watt_level(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	return sprintf(buf, "%d\n", mtk_pdc_get_max_watt(pinfo));
}

static ssize_t store_pdc_max_watt_level(struct device *dev, struct device_attribute *attr,
					 const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		mtk_pdc_set_max_watt(pinfo, temp);
		chr_err("[store_pdc_max_watt]:%d\n", temp);
	} else
		chr_err("[store_pdc_max_watt]: format error!\n");

	return size;
}
static DEVICE_ATTR(pdc_max_watt, 0664, show_pdc_max_watt_level, store_pdc_max_watt_level);
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

int mtk_get_dynamic_cv(struct charger_manager *info, unsigned int *cv)
{
	int ret = 0;
	u32 _cv, _cv_temp;
	unsigned int vbat_threshold[4] = {3400000, 0, 0, 0};
	u32 vbat_bif = 0, vbat_auxadc = 0, vbat = 0;
	u32 retry_cnt = 0;

	if (pmic_is_bif_exist()) {
		do {
			vbat_auxadc = battery_get_bat_voltage() * 1000;
			ret = pmic_get_bif_battery_voltage(&vbat_bif);
			vbat_bif = vbat_bif * 1000;
			if (ret >= 0 && vbat_bif != 0 && vbat_bif < vbat_auxadc) {
				vbat = vbat_bif;
				chr_err("%s: use BIF vbat = %duV, dV to auxadc = %duV\n",
					__func__, vbat, vbat_auxadc - vbat_bif);
				break;
			}
			retry_cnt++;
		} while (retry_cnt < 5);

		if (retry_cnt == 5) {
			ret = 0;
			vbat = vbat_auxadc;
			chr_err("%s: use AUXADC vbat = %duV, since BIF vbat = %duV\n",
				__func__, vbat_auxadc, vbat_bif);
		}

		/* Adjust CV according to the obtained vbat */
		vbat_threshold[1] = info->data.bif_threshold1;
		vbat_threshold[2] = info->data.bif_threshold2;
		_cv_temp = info->data.bif_cv_under_threshold2;

		if (!info->enable_dynamic_cv && vbat >= vbat_threshold[2]) {
			_cv = info->data.battery_cv;
			goto out;
		}

		if (vbat < vbat_threshold[1])
			_cv = 4608000;
		else if (vbat >= vbat_threshold[1] && vbat < vbat_threshold[2])
			_cv = _cv_temp;
		else {
			_cv = info->data.battery_cv;
			info->enable_dynamic_cv = false;
		}
out:
		*cv = _cv;
		chr_err("%s: CV = %duV, enable_dynamic_cv = %d\n",
			__func__, _cv, info->enable_dynamic_cv);
	} else
		ret = -ENOTSUPP;

	return ret;
}

int charger_manager_notifier(struct charger_manager *info, int event)
{
	return srcu_notifier_call_chain(&info->evt_nh, event, NULL);
}
#ifdef VENDOR_EDIT
/* Qiao.Hu@EXP.BSP.BaseDrv.CHG.Basic, 2017/08/15, Add for charger full status */
int notify_battery_full(void)
{
	printk("notify_battery_full_is_ok\n");

	if (charger_manager_notifier(pinfo, CHARGER_NOTIFY_EOC)) {
		printk("notifier fail\n");
		return 1;
	} else {
		return 0;
	}
}
#endif /* VENDOR_EDIT */

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
int charger_psy_event(struct notifier_block *nb, unsigned long event, void *v)
{
	struct charger_manager *info = container_of(nb, struct charger_manager, psy_nb);
	struct power_supply *psy = v;
	union power_supply_propval val;
	int ret;
	int tmp = 0;

	if (strcmp(psy->desc->name, "battery") == 0) {
		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_TEMP, &val);
		if (!ret) {
			tmp = val.intval / 10;
			if (info->battery_temperature != tmp && mt_get_charger_type() != CHARGER_UNKNOWN) {
				_wake_up_charger(info);
				chr_err("charger_psy_event %ld %s tmp:%d %d chr:%d\n", event, psy->desc->name, tmp,
					info->battery_temperature, mt_get_charger_type());
			}
		}
	}


	return NOTIFY_DONE;
}
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

#if defined(VENDOR_EDIT) && defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/07/25, sjc Modify for charging */
void oppo_wake_up_usbtemp_thread(void);
#endif
void mtk_charger_int_handler(void)
{
#if defined(VENDOR_EDIT) && defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	chr_err("mtk_charger_int_handler\n");
	if (mt_get_charger_type() != CHARGER_UNKNOWN)
		oppo_wake_up_usbtemp_thread();
#else
	if (pinfo == NULL) {
		chr_err("charger is not rdy ,skip1\n");
		return;
	}

	if (pinfo->init_done != true) {
		chr_err("charger is not rdy ,skip2\n");
		return;
	}
	_wake_up_charger(pinfo);
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */
}

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
static int mtk_charger_plug_in(struct charger_manager *info, enum charger_type chr_type)
{
	info->chr_type = chr_type;
	info->charger_thread_polling = true;

	info->can_charging = true;
	info->enable_dynamic_cv = true;
	info->safety_timeout = false;
	info->vbusov_stat = false;

	chr_err("mtk_is_charger_on plug in, tyupe:%d\n", chr_type);
	if (info->plug_in != NULL)
		info->plug_in(info);

	charger_dev_plug_in(info->chg1_dev);
	return 0;
}

static int mtk_charger_plug_out(struct charger_manager *info)
{
	struct charger_data *pdata1 = &info->chg1_data;
	struct charger_data *pdata2 = &info->chg2_data;

	chr_err("mtk_charger_plug_out\n");
	info->chr_type = CHARGER_UNKNOWN;
	info->charger_thread_polling = false;

	pdata1->disable_charging_count = 0;
	pdata1->input_current_limit_by_aicl = -1;
	pdata2->disable_charging_count = 0;

	if (info->plug_out != NULL)
		info->plug_out(info);

	charger_dev_set_input_current(info->chg1_dev, 500000);
	charger_dev_plug_out(info->chg1_dev);
	return 0;
}

static bool mtk_is_charger_on(struct charger_manager *info)
{
	enum charger_type chr_type;

	chr_type = mt_get_charger_type();
	if (chr_type == CHARGER_UNKNOWN) {
		if (info->chr_type != CHARGER_UNKNOWN)
			mtk_charger_plug_out(info);
	} else {
		if (info->chr_type == CHARGER_UNKNOWN)
			mtk_charger_plug_in(info, chr_type);
	}

	if (chr_type == CHARGER_UNKNOWN)
		return false;

	return true;
}

static void charger_update_data(struct charger_manager *info)
{
	info->battery_temperature = battery_meter_get_battery_temperature();
}

/* return false if vbus is over max_charger_voltage */
static bool mtk_chg_check_vbus(struct charger_manager *info)
{
	int vchr = 0;

	vchr = pmic_get_vbus() * 1000; /* uV */
	if (vchr > info->data.max_charger_voltage) {
		chr_err("%s: vbus(%d mV) > %d mV\n", __func__, vchr / 1000,
			info->data.max_charger_voltage / 1000);
		return false;
	}

	return true;
}

static void mtk_battery_notify_VCharger_check(struct charger_manager *info)
{
#if defined(BATTERY_NOTIFY_CASE_0001_VCHARGER)
	int vchr = 0;

	vchr = pmic_get_vbus() * 1000; /* uV */
	if (vchr < info->data.max_charger_voltage)
		info->notify_code &= ~(0x0001);
	else {
		info->notify_code |= 0x0001;
		chr_err("[BATTERY] charger_vol(%d mV) > %d mV\n",
			vchr / 1000, info->data.max_charger_voltage / 1000);
	}
#endif
}

static void mtk_battery_notify_VBatTemp_check(struct charger_manager *info)
{
#if defined(BATTERY_NOTIFY_CASE_0002_VBATTEMP)
	if (info->battery_temperature >= info->thermal.max_charge_temperature) {
		info->notify_code |= 0x0002;
		chr_err("[BATTERY] bat_temp(%d) out of range(too high)\n",
			info->battery_temperature);
	}

	if (info->enable_sw_jeita == true) {
		if (info->battery_temperature < info->data.temp_neg_10_threshold) {
			info->notify_code |= 0x0020;
			chr_err("[BATTERY] bat_temp(%d) out of range(too low)\n",
				info->battery_temperature);
		}
	} else {
#ifdef BAT_LOW_TEMP_PROTECT_ENABLE
		if (info->battery_temperature < info->thermal.min_charge_temperature) {
			info->notify_code |= 0x0020;
			chr_err("[BATTERY] bat_temp(%d) out of range(too low)\n",
				info->battery_temperature);
		}
#endif
	}
#endif
}

static void mtk_battery_notify_UI_test(struct charger_manager *info)
{
	switch (info->notify_test_mode) {
	case 1:
		info->notify_code = 0x0001;
		pr_debug("[%s] CASE_0001_VCHARGER\n", __func__);
		break;
	case 2:
		info->notify_code = 0x0002;
		pr_debug("[%s] CASE_0002_VBATTEMP\n", __func__);
		break;
	case 3:
		info->notify_code = 0x0004;
		pr_debug("[%s] CASE_0003_ICHARGING\n", __func__);
		break;
	case 4:
		info->notify_code = 0x0008;
		pr_debug("[%s] CASE_0004_VBAT\n", __func__);
		break;
	case 5:
		info->notify_code = 0x0010;
		pr_debug("[%s] CASE_0005_TOTAL_CHARGINGTIME\n", __func__);
		break;
	default:
		pr_debug("[%s] Unknown BN_TestMode Code: %x\n",
			__func__, info->notify_test_mode);
	}
}

static void mtk_battery_notify_check(struct charger_manager *info)
{
	info->notify_code = 0x0000;

	if (info->notify_test_mode == 0x0000) {
		mtk_battery_notify_VCharger_check(info);
		mtk_battery_notify_VBatTemp_check(info);
	} else {
		mtk_battery_notify_UI_test(info);
	}
}

static void mtk_chg_get_tchg(struct charger_manager *info)
{
	int ret;
	int tchg_min, tchg_max;
	struct charger_data *pdata;

	pdata = &info->chg1_data;
	ret = charger_dev_get_temperature(info->chg1_dev, &tchg_min, &tchg_max);

	if (ret < 0) {
		pdata->junction_temp_min = -127;
		pdata->junction_temp_max = -127;
	} else {
		pdata->junction_temp_min = tchg_min;
		pdata->junction_temp_max = tchg_max;
	}

	if (is_slave_charger_exist()) {
		pdata = &info->chg2_data;
		ret = charger_dev_get_temperature(info->chg2_dev,
			&tchg_min, &tchg_max);

		if (ret < 0) {
			pdata->junction_temp_min = -127;
			pdata->junction_temp_max = -127;
		} else {
			pdata->junction_temp_min = tchg_min;
			pdata->junction_temp_max = tchg_max;
		}
	}
}

static void charger_check_status(struct charger_manager *info)
{
	bool charging = true;
	int temperature;
	struct battery_thermal_protection_data *thermal;

	temperature = info->battery_temperature;
	thermal = &info->thermal;

	if (info->enable_sw_jeita == true) {
		do_sw_jeita_state_machine(info);
		if (info->sw_jeita.charging == false) {
			charging = false;
			goto stop_charging;
		}
	} else {

		if (thermal->enable_min_charge_temperature) {
			if (temperature < thermal->min_charge_temperature) {
				chr_err("[BATTERY] Battery Under Temperature or NTC fail %d %d!!\n\r", temperature,
					thermal->min_charge_temperature);
				thermal->sm = BAT_TEMP_LOW;
				charging = false;
				goto stop_charging;
			} else if (thermal->sm == BAT_TEMP_LOW) {
				if (temperature >= thermal->min_charge_temperature_plus_x_degree) {
					chr_err("[BATTERY] Battery Temperature raise from %d to %d(%d), allow charging!!\n\r",
							thermal->min_charge_temperature,
							temperature,
							thermal->min_charge_temperature_plus_x_degree);
					thermal->sm = BAT_TEMP_NORMAL;
				} else {
					charging = false;
					goto stop_charging;
				}
			}
		}

		if (temperature >= thermal->max_charge_temperature) {
			chr_err("[BATTERY] Battery over Temperature or NTC fail %d %d!!\n\r", temperature,
				thermal->max_charge_temperature);
			thermal->sm = BAT_TEMP_HIGH;
			charging = false;
			goto stop_charging;
		} else if (thermal->sm == BAT_TEMP_HIGH) {
			if (temperature < thermal->max_charge_temperature_minus_x_degree) {
				chr_err("[BATTERY] Battery Temperature raise from %d to %d(%d), allow charging!!\n\r",
						thermal->max_charge_temperature,
						temperature,
						thermal->max_charge_temperature_minus_x_degree);
				thermal->sm = BAT_TEMP_NORMAL;
			} else {
				charging = false;
				goto stop_charging;
			}
		}
	}

	mtk_chg_get_tchg(info);

	if (!mtk_chg_check_vbus(info)) {
		charging = false;
		goto stop_charging;
	}

	if (info->cmd_discharging)
		charging = false;
	if (info->safety_timeout)
		charging = false;
	if (info->vbusov_stat)
		charging = false;

stop_charging:
	mtk_battery_notify_check(info);

	chr_err("tmp:%d (jeita:%d sm:%d cv:%d en:%d) (sm:%d) en:%d c:%d s:%d ov:%d %d %d\n",
		temperature, info->enable_sw_jeita, info->sw_jeita.sm,
		info->sw_jeita.cv, info->sw_jeita.charging, thermal->sm,
		charging, info->cmd_discharging, info->safety_timeout,
		info->vbusov_stat,
		info->can_charging, charging);

	if (charging != info->can_charging)
		_charger_manager_enable_charging(info->chg1_consumer, 0, charging);

	info->can_charging = charging;

}

static void kpoc_power_off_check(struct charger_manager *info)
{
	unsigned int boot_mode = get_boot_mode();
	int vbus = battery_get_vbus();

	if (boot_mode == KERNEL_POWER_OFF_CHARGING_BOOT
	    || boot_mode == LOW_POWER_OFF_CHARGING_BOOT) {
		pr_debug("[%s] vchr=%d, boot_mode=%d\n",
			__func__, vbus, boot_mode);
		if (!mtk_is_pe30_running(info) && vbus < 2500
			&& atomic_read(&info->enable_kpoc_shdn)) {
			pr_err("Unplug Charger/USB in KPOC mode, shutdown\n");
			kernel_power_off();
		}
	}
}

enum hrtimer_restart charger_kthread_hrtimer_func(struct hrtimer *timer)
{
	struct charger_manager *info = container_of(timer, struct charger_manager, charger_kthread_timer);

	_wake_up_charger(info);
	return HRTIMER_NORESTART;
}

int charger_kthread_fgtimer_func(struct gtimer *data)
{
	struct charger_manager *info = container_of(data, struct charger_manager, charger_kthread_fgtimer);

	_wake_up_charger(info);
	return 0;
}

static void mtk_charger_init_timer(struct charger_manager *info)
{
	if (IS_ENABLED(USE_FG_TIMER)) {
		gtimer_init(&info->charger_kthread_fgtimer, &info->pdev->dev, "charger_thread");
		info->charger_kthread_fgtimer.callback = charger_kthread_fgtimer_func;
		gtimer_start(&info->charger_kthread_fgtimer, info->polling_interval);
	} else {
		ktime_t ktime = ktime_set(info->polling_interval, 0);

		hrtimer_init(&info->charger_kthread_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		info->charger_kthread_timer.function = charger_kthread_hrtimer_func;
		hrtimer_start(&info->charger_kthread_timer, ktime, HRTIMER_MODE_REL);
	}
}

static void mtk_charger_start_timer(struct charger_manager *info)
{
	if (IS_ENABLED(USE_FG_TIMER)) {
		chr_debug("fg start timer");
		gtimer_start(&info->charger_kthread_fgtimer, info->polling_interval);
	} else {
		ktime_t ktime = ktime_set(info->polling_interval, 0);

		chr_debug("hrtimer start timer");
		hrtimer_start(&info->charger_kthread_timer, ktime, HRTIMER_MODE_REL);
	}
}

void mtk_charger_stop_timer(struct charger_manager *info)
{
	if (IS_ENABLED(USE_FG_TIMER))
		gtimer_stop(&info->charger_kthread_fgtimer);
}

static int charger_routine_thread(void *arg)
{
	struct charger_manager *info = arg;
	unsigned long flags;
	bool is_charger_on;
	int bat_current, chg_current;

	while (1) {
		wait_event(info->wait_que, (info->charger_thread_timeout == true));

		mutex_lock(&info->charger_lock);
		spin_lock_irqsave(&info->slock, flags);
		if (wake_lock_active(&info->charger_wakelock) == 0)
			wake_lock(&info->charger_wakelock);
		spin_unlock_irqrestore(&info->slock, flags);

		info->charger_thread_timeout = false;
		bat_current = battery_get_bat_current();
		chg_current = pmic_get_charging_current();
		chr_err("Vbat=%d,Ibat=%d,I=%d,VChr=%d,T=%d,Soc=%d:%d,CT:%d:%d hv:%d pd:%d:%d\n",
			battery_get_bat_voltage(), bat_current, chg_current,
			battery_get_vbus(), battery_get_bat_temperature(),
			battery_get_bat_soc(), battery_get_bat_uisoc(),
			mt_get_charger_type(), info->chr_type, info->enable_hv_charging,
			info->pd_type, info->pd_reset);

		if (info->pd_reset == true) {
			mtk_pe40_plugout_reset(info);
			info->pd_reset = false;
		}

		is_charger_on = mtk_is_charger_on(info);

		if (info->charger_thread_polling == true)
			mtk_charger_start_timer(info);

		charger_update_data(info);
		charger_check_status(info);
		kpoc_power_off_check(info);

		if (is_disable_charger() == false) {
			if (is_charger_on == true) {
				if (info->do_algorithm)
					info->do_algorithm(info);
			}
		} else
			chr_debug("disable charging\n");

		spin_lock_irqsave(&info->slock, flags);
		wake_unlock(&info->charger_wakelock);
		spin_unlock_irqrestore(&info->slock, flags);
		chr_debug("charger_routine_thread end , %d\n", info->charger_thread_timeout);
		mutex_unlock(&info->charger_lock);
	}

	return 0;
}
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

static int mtk_charger_parse_dt(struct charger_manager *info, struct device *dev)
{
	struct device_node *np = dev->of_node;
#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	u32 val;
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

	chr_err("%s: starts\n", __func__);

	if (!np) {
		chr_err("%s: no device node\n", __func__);
		return -EINVAL;
	}

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	if (of_property_read_string(np, "algorithm_name",
		&info->algorithm_name) < 0) {
		chr_err("%s: no algorithm_name name\n", __func__);
		info->algorithm_name = "SwitchCharging";
	}

	if (strcmp(info->algorithm_name, "SwitchCharging") == 0) {
		chr_err("found SwitchCharging\n");
		mtk_switch_charging_init(info);
	}

#ifdef CONFIG_MTK_DUAL_CHARGER_SUPPORT
	if (strcmp(info->algorithm_name, "DualSwitchCharging") == 0) {
		pr_debug("found DualSwitchCharging\n");
		mtk_dual_switch_charging_init(info);
	}
#endif

	if (strcmp(info->algorithm_name, "LinearCharging") == 0) {
		pr_info("%s: LinearCharging\n", __func__);
		mtk_linear_charging_init(info);
	}

	info->disable_charger = of_property_read_bool(np, "disable_charger");
	info->enable_sw_safety_timer = of_property_read_bool(np, "enable_sw_safety_timer");
	info->enable_sw_jeita = of_property_read_bool(np, "enable_sw_jeita");
	info->enable_pe_plus = of_property_read_bool(np, "enable_pe_plus");
	info->enable_pe_2 = of_property_read_bool(np, "enable_pe_2");
	info->enable_pe_3 = of_property_read_bool(np, "enable_pe_3");
	info->enable_pe_4 = of_property_read_bool(np, "enable_pe_4");
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

	info->enable_type_c = of_property_read_bool(np, "enable_type_c");

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	info->enable_hv_charging = true;

	/* common */
	if (of_property_read_u32(np, "battery_cv", &val) >= 0) {
		info->data.battery_cv = val;
	} else {
		chr_err(
			"use default BATTERY_CV:%d\n",
			BATTERY_CV);
		info->data.battery_cv = BATTERY_CV;
	}

	if (of_property_read_u32(np, "max_charger_voltage", &val) >= 0) {
		info->data.max_charger_voltage = val;
	} else {
		chr_err(
			"use default V_CHARGER_MAX:%d\n",
			V_CHARGER_MAX);
		info->data.max_charger_voltage = V_CHARGER_MAX;
	}
	info->data.max_charger_voltage_setting = info->data.max_charger_voltage;

	if (of_property_read_u32(np, "min_charger_voltage", &val) >= 0) {
		info->data.min_charger_voltage = val;
	} else {
		chr_err(
			"use default V_CHARGER_MIN:%d\n",
			V_CHARGER_MIN);
		info->data.min_charger_voltage = V_CHARGER_MIN;
	}

	/* charging current */
	if (of_property_read_u32(np, "usb_charger_current_suspend", &val) >= 0) {
		info->data.usb_charger_current_suspend = val;
	} else {
		chr_err(
			"use default USB_CHARGER_CURRENT_SUSPEND:%d\n",
			USB_CHARGER_CURRENT_SUSPEND);
		info->data.usb_charger_current_suspend = USB_CHARGER_CURRENT_SUSPEND;
	}

	if (of_property_read_u32(np, "usb_charger_current_unconfigured", &val) >= 0) {
		info->data.usb_charger_current_unconfigured = val;
	} else {
		chr_err(
			"use default USB_CHARGER_CURRENT_UNCONFIGURED:%d\n",
			USB_CHARGER_CURRENT_UNCONFIGURED);
		info->data.usb_charger_current_unconfigured = USB_CHARGER_CURRENT_UNCONFIGURED;
	}

	if (of_property_read_u32(np, "usb_charger_current_configured", &val) >= 0) {
		info->data.usb_charger_current_configured = val;
	} else {
		chr_err(
			"use default USB_CHARGER_CURRENT_CONFIGURED:%d\n",
			USB_CHARGER_CURRENT_CONFIGURED);
		info->data.usb_charger_current_configured = USB_CHARGER_CURRENT_CONFIGURED;
	}

	if (of_property_read_u32(np, "usb_charger_current", &val) >= 0) {
		info->data.usb_charger_current = val;
	} else {
		chr_err(
			"use default USB_CHARGER_CURRENT:%d\n",
			USB_CHARGER_CURRENT);
		info->data.usb_charger_current = USB_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ac_charger_current", &val) >= 0) {
		info->data.ac_charger_current = val;
	} else {
		chr_err(
			"use default AC_CHARGER_CURRENT:%d\n",
			AC_CHARGER_CURRENT);
		info->data.ac_charger_current = AC_CHARGER_CURRENT;
	}

	info->data.pd_charger_current = 3000000;

	if (of_property_read_u32(np, "ac_charger_input_current", &val) >= 0) {
		info->data.ac_charger_input_current = val;
	} else {
		chr_err(
			"use default AC_CHARGER_INPUT_CURRENT:%d\n",
			AC_CHARGER_INPUT_CURRENT);
		info->data.ac_charger_input_current = AC_CHARGER_INPUT_CURRENT;
	}

	if (of_property_read_u32(np, "non_std_ac_charger_current", &val) >= 0) {
		info->data.non_std_ac_charger_current = val;
	} else {
		chr_err(
			"use default NON_STD_AC_CHARGER_CURRENT:%d\n",
			NON_STD_AC_CHARGER_CURRENT);
		info->data.non_std_ac_charger_current = NON_STD_AC_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "charging_host_charger_current", &val) >= 0) {
		info->data.charging_host_charger_current = val;
	} else {
		chr_err(
			"use default CHARGING_HOST_CHARGER_CURRENT:%d\n",
			CHARGING_HOST_CHARGER_CURRENT);
		info->data.charging_host_charger_current = CHARGING_HOST_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "apple_1_0a_charger_current", &val) >= 0) {
		info->data.apple_1_0a_charger_current = val;
	} else {
		chr_err("use default APPLE_1_0A_CHARGER_CURRENT:%d\n",
			APPLE_1_0A_CHARGER_CURRENT);
		info->data.apple_1_0a_charger_current = APPLE_1_0A_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "apple_2_1a_charger_current", &val) >= 0) {
		info->data.apple_2_1a_charger_current = val;
	} else {
		chr_err("use default APPLE_2_1A_CHARGER_CURRENT:%d\n",
			APPLE_2_1A_CHARGER_CURRENT);
		info->data.apple_2_1a_charger_current = APPLE_2_1A_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ta_ac_charger_current", &val) >= 0) {
		info->data.ta_ac_charger_current = val;
	} else {
		chr_err(
			"use default TA_AC_CHARGING_CURRENT:%d\n",
			TA_AC_CHARGING_CURRENT);
		info->data.ta_ac_charger_current = TA_AC_CHARGING_CURRENT;
	}

	/* sw jeita */
	if (of_property_read_u32(np, "jeita_temp_above_t4_cv_voltage", &val) >= 0) {
		info->data.jeita_temp_above_t4_cv_voltage = val;
	} else {
		chr_err(
			"use default JEITA_TEMP_ABOVE_T4_CV_VOLTAGE:%d\n", JEITA_TEMP_ABOVE_T4_CV_VOLTAGE);
		info->data.jeita_temp_above_t4_cv_voltage = JEITA_TEMP_ABOVE_T4_CV_VOLTAGE;
	}

	if (of_property_read_u32(np, "jeita_temp_t3_to_t4_cv_voltage", &val) >= 0) {
		info->data.jeita_temp_t3_to_t4_cv_voltage = val;
	} else {
		chr_err(
			"use default JEITA_TEMP_T3_TO_T4_CV_VOLTAGE:%d\n", JEITA_TEMP_T3_TO_T4_CV_VOLTAGE);
		info->data.jeita_temp_t3_to_t4_cv_voltage = JEITA_TEMP_T3_TO_T4_CV_VOLTAGE;
	}

	if (of_property_read_u32(np, "jeita_temp_t2_to_t3_cv_voltage", &val) >= 0) {
		info->data.jeita_temp_t2_to_t3_cv_voltage = val;
	} else {
		chr_err(
			"use default JEITA_TEMP_T2_TO_T3_CV_VOLTAGE:%d\n", JEITA_TEMP_T2_TO_T3_CV_VOLTAGE);
		info->data.jeita_temp_t2_to_t3_cv_voltage = JEITA_TEMP_T2_TO_T3_CV_VOLTAGE;
	}

	if (of_property_read_u32(np, "jeita_temp_t1_to_t2_cv_voltage", &val) >= 0) {
		info->data.jeita_temp_t1_to_t2_cv_voltage = val;
	} else {
		chr_err(
			"use default JEITA_TEMP_T1_TO_T2_CV_VOLTAGE:%d\n", JEITA_TEMP_T1_TO_T2_CV_VOLTAGE);
		info->data.jeita_temp_t1_to_t2_cv_voltage = JEITA_TEMP_T1_TO_T2_CV_VOLTAGE;
	}

	if (of_property_read_u32(np, "jeita_temp_t0_to_t1_cv_voltage", &val) >= 0) {
		info->data.jeita_temp_t0_to_t1_cv_voltage = val;
	} else {
		chr_err(
			"use default JEITA_TEMP_T0_TO_T1_CV_VOLTAGE:%d\n", JEITA_TEMP_T0_TO_T1_CV_VOLTAGE);
		info->data.jeita_temp_t0_to_t1_cv_voltage = JEITA_TEMP_T0_TO_T1_CV_VOLTAGE;
	}

	if (of_property_read_u32(np, "jeita_temp_below_t0_cv_voltage", &val) >= 0) {
		info->data.jeita_temp_below_t0_cv_voltage = val;
	} else {
		chr_err(
			"use default JEITA_TEMP_BELOW_T0_CV_VOLTAGE:%d\n", JEITA_TEMP_BELOW_T0_CV_VOLTAGE);
		info->data.jeita_temp_below_t0_cv_voltage = JEITA_TEMP_BELOW_T0_CV_VOLTAGE;
	}

	if (of_property_read_u32(np, "temp_t4_threshold", &val) >= 0) {
		info->data.temp_t4_threshold = val;
	} else {
		chr_err(
			"use default TEMP_T4_THRESHOLD:%d\n", TEMP_T4_THRESHOLD);
		info->data.temp_t4_threshold = TEMP_T4_THRESHOLD;
	}

	if (of_property_read_u32(np, "temp_t4_thres_minus_x_degree", &val) >= 0) {
		info->data.temp_t4_thres_minus_x_degree = val;
	} else {
		chr_err(
			"use default TEMP_T4_THRES_MINUS_X_DEGREE:%d\n", TEMP_T4_THRES_MINUS_X_DEGREE);
		info->data.temp_t4_thres_minus_x_degree = TEMP_T4_THRES_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t3_threshold", &val) >= 0) {
		info->data.temp_t3_threshold = val;
	} else {
		chr_err(
			"use default TEMP_T3_THRESHOLD:%d\n", TEMP_T3_THRESHOLD);
		info->data.temp_t3_threshold = TEMP_T3_THRESHOLD;
	}

	if (of_property_read_u32(np, "temp_t3_thres_minus_x_degree", &val) >= 0) {
		info->data.temp_t3_thres_minus_x_degree = val;
	} else {
		chr_err(
			"use default TEMP_T3_THRES_MINUS_X_DEGREE:%d\n", TEMP_T3_THRES_MINUS_X_DEGREE);
		info->data.temp_t3_thres_minus_x_degree = TEMP_T3_THRES_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t2_threshold", &val) >= 0) {
		info->data.temp_t2_threshold = val;
	} else {
		chr_err(
			"use default TEMP_T2_THRESHOLD:%d\n", TEMP_T2_THRESHOLD);
		info->data.temp_t2_threshold = TEMP_T2_THRESHOLD;
	}

	if (of_property_read_u32(np, "temp_t2_thres_plus_x_degree", &val) >= 0) {
		info->data.temp_t2_thres_plus_x_degree = val;
	} else {
		chr_err(
			"use default TEMP_T2_THRES_PLUS_X_DEGREE:%d\n", TEMP_T2_THRES_PLUS_X_DEGREE);
		info->data.temp_t2_thres_plus_x_degree = TEMP_T2_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t1_threshold", &val) >= 0) {
		info->data.temp_t1_threshold = val;
	} else {
		chr_err(
			"use default TEMP_T1_THRESHOLD:%d\n", TEMP_T1_THRESHOLD);
		info->data.temp_t1_threshold = TEMP_T1_THRESHOLD;
	}

	if (of_property_read_u32(np, "temp_t1_thres_plus_x_degree", &val) >= 0) {
		info->data.temp_t1_thres_plus_x_degree = val;
	} else {
		chr_err(
			"use default TEMP_T1_THRES_PLUS_X_DEGREE:%d\n", TEMP_T1_THRES_PLUS_X_DEGREE);
		info->data.temp_t1_thres_plus_x_degree = TEMP_T1_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t0_threshold", &val) >= 0) {
		info->data.temp_t0_threshold = val;
	} else {
		chr_err(
			"use default TEMP_T0_THRESHOLD:%d\n", TEMP_T0_THRESHOLD);
		info->data.temp_t0_threshold = TEMP_T0_THRESHOLD;
	}

	if (of_property_read_u32(np, "temp_t0_thres_plus_x_degree", &val) >= 0) {
		info->data.temp_t0_thres_plus_x_degree = val;
	} else {
		chr_err(
			"use default TEMP_T0_THRES_PLUS_X_DEGREE:%d\n", TEMP_T0_THRES_PLUS_X_DEGREE);
		info->data.temp_t0_thres_plus_x_degree = TEMP_T0_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_neg_10_threshold", &val) >= 0) {
		info->data.temp_neg_10_threshold = val;
	} else {
		chr_err(
			"use default TEMP_NEG_10_THRESHOLD:%d\n", TEMP_NEG_10_THRESHOLD);
		info->data.temp_neg_10_threshold = TEMP_NEG_10_THRESHOLD;
	}

	/* battery temperature protection */
	info->thermal.sm = BAT_TEMP_NORMAL;
	info->thermal.enable_min_charge_temperature = of_property_read_bool(np,
		"enable_min_charge_temperature");

	if (of_property_read_u32(np, "min_charge_temperature", &val) >= 0) {
		info->thermal.min_charge_temperature = val;
	} else {
		chr_err(
			"use default MIN_CHARGE_TEMPERATURE:%d\n", MIN_CHARGE_TEMPERATURE);
		info->thermal.min_charge_temperature = MIN_CHARGE_TEMPERATURE;
	}

	if (of_property_read_u32(np, "min_charge_temperature_plus_x_degree", &val) >= 0) {
		info->thermal.min_charge_temperature_plus_x_degree = val;
	} else {
		chr_err(
			"use default MIN_CHARGE_TEMPERATURE_PLUS_X_DEGREE:%d\n",
			MIN_CHARGE_TEMPERATURE_PLUS_X_DEGREE);
		info->thermal.min_charge_temperature_plus_x_degree = MIN_CHARGE_TEMPERATURE_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "max_charge_temperature", &val) >= 0) {
		info->thermal.max_charge_temperature = val;
	} else {
		chr_err(
			"use default MAX_CHARGE_TEMPERATURE:%d\n", MAX_CHARGE_TEMPERATURE);
		info->thermal.max_charge_temperature = MAX_CHARGE_TEMPERATURE;
	}

	if (of_property_read_u32(np, "max_charge_temperature_minus_x_degree", &val) >= 0) {
		info->thermal.max_charge_temperature_minus_x_degree = val;
	} else {
		chr_err(
			"use default MAX_CHARGE_TEMPERATURE_MINUS_X_DEGREE:%d\n",
			MAX_CHARGE_TEMPERATURE_MINUS_X_DEGREE);
		info->thermal.max_charge_temperature_minus_x_degree = MAX_CHARGE_TEMPERATURE_MINUS_X_DEGREE;
	}

	/* PE */
	if (of_property_read_u32(np, "pe_ichg_level_threshold", &val) >= 0) {
		info->data.pe_ichg_level_threshold = val;
	} else {
		chr_err("use default PE_ICHG_LEAVE_THRESHOLD:%d\n",
			PE_ICHG_LEAVE_THRESHOLD);
		info->data.pe_ichg_level_threshold = PE_ICHG_LEAVE_THRESHOLD;
	}
	info->data.ta_ac_12v_input_current = TA_AC_12V_INPUT_CURRENT;
	info->data.ta_ac_9v_input_current = TA_AC_9V_INPUT_CURRENT;
	info->data.ta_ac_7v_input_current = TA_AC_7V_INPUT_CURRENT;
#ifdef TA_12V_SUPPORT
	info->data.ta_12v_support = true;
#else
	info->data.ta_12v_support = false;
#endif

#ifdef TA_9V_SUPPORT
	info->data.ta_9v_support = true;
#else
	info->data.ta_9v_support = false;
#endif

	/* PE 2.0 */
	if (of_property_read_u32(np, "pe20_ichg_level_threshold", &val) >= 0) {
		info->data.pe20_ichg_level_threshold = val;
	} else {
		chr_err(
			"use default PE20_ICHG_LEAVE_THRESHOLD:%d\n",
			PE20_ICHG_LEAVE_THRESHOLD);
		info->data.pe20_ichg_level_threshold = PE20_ICHG_LEAVE_THRESHOLD;
	}

	if (of_property_read_u32(np, "ta_start_battery_soc", &val) >= 0) {
		info->data.ta_start_battery_soc = val;
	} else {
		chr_err(
			"use default TA_START_BATTERY_SOC:%d\n",
			TA_START_BATTERY_SOC);
		info->data.ta_start_battery_soc = TA_START_BATTERY_SOC;
	}

	if (of_property_read_u32(np, "ta_stop_battery_soc", &val) >= 0) {
		info->data.ta_stop_battery_soc = val;
	} else {
		chr_err(
			"use default TA_STOP_BATTERY_SOC:%d\n",
			TA_STOP_BATTERY_SOC);
		info->data.ta_stop_battery_soc = TA_STOP_BATTERY_SOC;
	}

	/* PE 4.0 single */
	if (of_property_read_u32(np, "pe40_single_charger_input_current", &val) >= 0) {
		info->data.pe40_single_charger_input_current = val;
	} else {
		chr_err(
			"use default pe40_single_charger_input_current:%d\n",
			3000);
		info->data.pe40_single_charger_input_current = 3000;
	}

	if (of_property_read_u32(np, "pe40_single_charger_current", &val) >= 0) {
		info->data.pe40_single_charger_current = val;
	} else {
		chr_err(
			"use default pe40_single_charger_current:%d\n",
			3000);
		info->data.pe40_single_charger_current = 3000;
	}

	/* PE 4.0 dual */
	if (of_property_read_u32(np, "pe40_dual_charger_input_current", &val) >= 0) {
		info->data.pe40_dual_charger_input_current = val;
	} else {
		chr_err(
			"use default pe40_dual_charger_input_current:%d\n",
			3000);
		info->data.pe40_dual_charger_input_current = 3000;
	}

	if (of_property_read_u32(np, "pe40_dual_charger_chg1_current", &val) >= 0) {
		info->data.pe40_dual_charger_chg1_current = val;
	} else {
		chr_err(
			"use default pe40_dual_charger_chg1_current:%d\n",
			2000);
		info->data.pe40_dual_charger_chg1_current = 2000;
	}

	if (of_property_read_u32(np, "pe40_dual_charger_chg2_current", &val) >= 0) {
		info->data.pe40_dual_charger_chg2_current = val;
	} else {
		chr_err(
			"use default pe40_dual_charger_chg2_current:%d\n",
			2000);
		info->data.pe40_dual_charger_chg2_current = 2000;
	}

	if (of_property_read_u32(np, "pe40_stop_battery_soc", &val) >= 0) {
		info->data.pe40_stop_battery_soc = val;
	} else {
		chr_err(
			"use default pe40_stop_battery_soc:%d\n",
			2000);
		info->data.pe40_stop_battery_soc = 80;
	}

#ifdef CONFIG_MTK_DUAL_CHARGER_SUPPORT
	/* dual charger */
	if (of_property_read_u32(np, "chg1_ta_ac_charger_current", &val) >= 0) {
		info->data.chg1_ta_ac_charger_current = val;
	} else {
		chr_err(
			"use default TA_AC_MASTER_CHARGING_CURRENT:%d\n",
			TA_AC_MASTER_CHARGING_CURRENT);
		info->data.chg1_ta_ac_charger_current = TA_AC_MASTER_CHARGING_CURRENT;
	}

	if (of_property_read_u32(np, "chg2_ta_ac_charger_current", &val) >= 0) {
		info->data.chg2_ta_ac_charger_current = val;
	} else {
		chr_err(
			"use default TA_AC_SLAVE_CHARGING_CURRENT:%d\n",
			TA_AC_SLAVE_CHARGING_CURRENT);
		info->data.chg2_ta_ac_charger_current = TA_AC_SLAVE_CHARGING_CURRENT;
	}
#endif

	/* cable measurement impedance */
	if (of_property_read_u32(np, "cable_imp_threshold", &val) >= 0) {
		info->data.cable_imp_threshold = val;
	} else {
		chr_err(
			"use default CABLE_IMP_THRESHOLD:%d\n",
			CABLE_IMP_THRESHOLD);
		info->data.cable_imp_threshold = CABLE_IMP_THRESHOLD;
	}

	if (of_property_read_u32(np, "vbat_cable_imp_threshold", &val) >= 0) {
		info->data.vbat_cable_imp_threshold = val;
	} else {
		chr_err(
			"use default VBAT_CABLE_IMP_THRESHOLD:%d\n",
			VBAT_CABLE_IMP_THRESHOLD);
		info->data.vbat_cable_imp_threshold = VBAT_CABLE_IMP_THRESHOLD;
	}

	/* bif */
	if (of_property_read_u32(np, "bif_threshold1", &val) >= 0) {
		info->data.bif_threshold1 = val;
	} else {
		chr_err(
			"use default BIF_THRESHOLD1:%d\n",
			BIF_THRESHOLD1);
		info->data.bif_threshold1 = BIF_THRESHOLD1;
	}

	if (of_property_read_u32(np, "bif_threshold2", &val) >= 0) {
		info->data.bif_threshold2 = val;
	} else {
		chr_err(
			"use default BIF_THRESHOLD2:%d\n",
			BIF_THRESHOLD2);
		info->data.bif_threshold2 = BIF_THRESHOLD2;
	}

	if (of_property_read_u32(np, "bif_cv_under_threshold2", &val) >= 0) {
		info->data.bif_cv_under_threshold2 = val;
	} else {
		chr_err(
			"use default BIF_CV_UNDER_THRESHOLD2:%d\n",
			BIF_CV_UNDER_THRESHOLD2);
		info->data.bif_cv_under_threshold2 = BIF_CV_UNDER_THRESHOLD2;
	}
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

	info->data.power_path_support = of_property_read_bool(np, "power_path_support");
	chr_debug("%s: power_path_support: %d\n", __func__, info->data.power_path_support);

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	if (of_property_read_u32(np, "max_charging_time", &val) >= 0) {
		info->data.max_charging_time = val;
		chr_debug("%s: max_charging_time: %d\n", __func__, info->data.max_charging_time);
	} else {
		chr_err("use default MAX_CHARGING_TIME:%d\n", MAX_CHARGING_TIME);
		info->data.max_charging_time = MAX_CHARGING_TIME;
	}

	chr_err("algorithm name:%s\n", info->algorithm_name);
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

	return 0;
}

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
static ssize_t show_Pump_Express(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;
	int is_ta_detected = 0;

	pr_debug("[%s] chr_type:%d UISOC:%d startsoc:%d stopsoc:%d\n", __func__,
		mt_get_charger_type(), get_ui_soc(),
		pinfo->data.ta_start_battery_soc,
		pinfo->data.ta_stop_battery_soc);

	if (IS_ENABLED(CONFIG_MTK_PUMP_EXPRESS_PLUS_20_SUPPORT)) {
		/* Is PE+20 connect */
		if (mtk_pe20_get_is_connect(pinfo))
			is_ta_detected = 1;
	}

	if (IS_ENABLED(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)) {
		/* Is PE+ connect */
		if (mtk_pe_get_is_connect(pinfo))
			is_ta_detected = 1;
	}

	if (mtk_is_TA_support_pe30(pinfo) == true)
		is_ta_detected = 1;

	if (mtk_is_TA_support_pd_pps(pinfo) == true)
		is_ta_detected = 1;

	pr_debug("%s: detected = %d, pe20_is_connect = %d, pe_is_connect = %d\n",
		__func__, is_ta_detected,
		mtk_pe20_get_is_connect(pinfo),
		mtk_pe_get_is_connect(pinfo));

	return sprintf(buf, "%u\n", is_ta_detected);
}

static DEVICE_ATTR(Pump_Express, 0444, show_Pump_Express, NULL);
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

static ssize_t show_BatteryNotify(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	pr_debug("[Battery] show_BatteryNotify: %x\n", pinfo->notify_code);

	return sprintf(buf, "%u\n", pinfo->notify_code);
}

static ssize_t store_BatteryNotify(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] store_BatteryNotify\n");
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %Zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->notify_code = reg;
		pr_debug("[Battery] store code: %x\n", pinfo->notify_code);
	}
	return size;
}

static DEVICE_ATTR(BatteryNotify, 0664, show_BatteryNotify, store_BatteryNotify);

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
static ssize_t show_input_current(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	pr_debug("[Battery] show_input_current : %x\n", pinfo->chg1_data.thermal_input_current_limit);
	return sprintf(buf, "%u\n", pinfo->chg1_data.thermal_input_current_limit);
}

static ssize_t store_input_current(struct device *dev, struct device_attribute *attr, const char *buf,
				 size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] store_input_current\n");
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %Zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->chg1_data.thermal_input_current_limit = reg;
		pr_debug("[Battery] store_input_current: %x\n", pinfo->chg1_data.thermal_input_current_limit);
	}
	return size;
}
static DEVICE_ATTR(input_current, 0664, show_input_current, store_input_current);

static ssize_t show_chg1_current(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	pr_debug("[Battery] show_chg1_current : %x\n", pinfo->chg1_data.thermal_charging_current_limit);
	return sprintf(buf, "%u\n", pinfo->chg1_data.thermal_charging_current_limit);
}

static ssize_t store_chg1_current(struct device *dev, struct device_attribute *attr, const char *buf,
				 size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] store_chg1_current\n");
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %Zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->chg1_data.thermal_charging_current_limit = reg;
		pr_debug("[Battery] store_chg1_current: %x\n", pinfo->chg1_data.thermal_charging_current_limit);
	}
	return size;
}
static DEVICE_ATTR(chg1_current, 0664, show_chg1_current, store_chg1_current);

static ssize_t show_chg2_current(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	pr_debug("[Battery] show_chg2_current : %x\n", pinfo->chg2_data.thermal_charging_current_limit);
	return sprintf(buf, "%u\n", pinfo->chg2_data.thermal_charging_current_limit);
}

static ssize_t store_chg2_current(struct device *dev, struct device_attribute *attr, const char *buf,
				 size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] store_chg2_current\n");
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %Zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->chg2_data.thermal_charging_current_limit = reg;
		pr_debug("[Battery] store_chg2_current: %x\n", pinfo->chg2_data.thermal_charging_current_limit);
	}
	return size;
}
static DEVICE_ATTR(chg2_current, 0664, show_chg2_current, store_chg2_current);


static ssize_t show_BN_TestMode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct charger_manager *pinfo = dev->driver_data;

	pr_debug("[Battery] show_BN_TestMode : %x\n", pinfo->notify_test_mode);
	return sprintf(buf, "%u\n", pinfo->notify_test_mode);
}

static ssize_t store_BN_TestMode(struct device *dev, struct device_attribute *attr, const char *buf,
				 size_t size)
{
	struct charger_manager *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] store_BN_TestMode\n");
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %Zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->notify_test_mode = reg;
		pr_debug("[Battery] store mode: %x\n", pinfo->notify_test_mode);
	}
	return size;
}
static DEVICE_ATTR(BN_TestMode, 0664, show_BN_TestMode, store_BN_TestMode);

static ssize_t show_ADC_Charger_Voltage(struct device *dev, struct device_attribute *attr,
					char *buf)
{
	int vbus = battery_get_vbus();

	pr_debug("[%s]: %d\n", __func__, vbus);
	return sprintf(buf, "%d\n", vbus);
}

static DEVICE_ATTR(ADC_Charger_Voltage, 0444, show_ADC_Charger_Voltage, NULL);

/* procfs */
static int mtk_charger_current_cmd_show(struct seq_file *m, void *data)
{
	struct charger_manager *pinfo = m->private;

	seq_printf(m, "%d %d\n", pinfo->usb_unlimited, pinfo->cmd_discharging);
	return 0;
}

static ssize_t mtk_charger_current_cmd_write(struct file *file, const char *buffer,
						size_t count, loff_t *data)
{
	int len = 0;
	char desc[32];
	int cmd_current_unlimited = 0;
	int cmd_discharging = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return 0;

	desc[len] = '\0';

	if (sscanf(desc, "%d %d", &cmd_current_unlimited, &cmd_discharging) == 2) {
		info->usb_unlimited = cmd_current_unlimited;
		if (cmd_discharging == 1) {
			info->cmd_discharging = true;
			charger_dev_enable(info->chg1_dev, false);
			charger_manager_notifier(info, CHARGER_NOTIFY_STOP_CHARGING);
		} else if (cmd_discharging == 0) {
			info->cmd_discharging = false;
			charger_dev_enable(info->chg1_dev, true);
			charger_manager_notifier(info, CHARGER_NOTIFY_START_CHARGING);
		}

		pr_debug("%s cmd_current_unlimited=%d, cmd_discharging=%d\n",
			__func__, cmd_current_unlimited, cmd_discharging);
		return count;
	}

	chr_err("bad argument, echo [usb_unlimited] [disable] > current_cmd\n");
	return count;
}

static int mtk_charger_en_power_path_show(struct seq_file *m, void *data)
{
	struct charger_manager *pinfo = m->private;
	bool power_path_en = true;

	charger_dev_is_powerpath_enabled(pinfo->chg1_dev, &power_path_en);
	seq_printf(m, "%d\n", power_path_en);

	return 0;
}

static ssize_t mtk_charger_en_power_path_write(struct file *file, const char *buffer,
	size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32];
	unsigned int enable = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));


	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return 0;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_powerpath(info->chg1_dev, enable);
		pr_debug("%s: enable power path = %d\n", __func__, enable);
		return count;
	}

	chr_err("bad argument, echo [enable] > en_power_path\n");
	return count;
}

static int mtk_charger_en_safety_timer_show(struct seq_file *m, void *data)
{
	struct charger_manager *pinfo = m->private;
	bool safety_timer_en = false;

	charger_dev_is_safety_timer_enabled(pinfo->chg1_dev, &safety_timer_en);
	seq_printf(m, "%d\n", safety_timer_en);

	return 0;
}

static ssize_t mtk_charger_en_safety_timer_write(struct file *file, const char *buffer,
	size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32];
	unsigned int enable = 0;
	struct charger_manager *info = PDE_DATA(file_inode(file));

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return 0;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_safety_timer(info->chg1_dev, enable);
		pr_debug("%s: enable safety timer = %d\n", __func__, enable);

		/* SW safety timer */
		if (enable)
			info->enable_sw_safety_timer = true;
		else
			info->enable_sw_safety_timer = false;

		return count;
	}

	chr_err("bad argument, echo [enable] > en_safety_timer\n");
	return count;
}

/* PROC_FOPS_RW(battery_cmd); */
/* PROC_FOPS_RW(discharging_cmd); */
PROC_FOPS_RW(current_cmd);
PROC_FOPS_RW(en_power_path);
PROC_FOPS_RW(en_safety_timer);
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */


/* Create sysfs and procfs attributes */
static int mtk_charger_setup_files(struct platform_device *pdev)
{
	int ret = 0;
#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	struct proc_dir_entry *battery_dir = NULL;
	struct charger_manager *info = platform_get_drvdata(pdev);
	/* struct charger_device *chg_dev; */

	ret = device_create_file(&(pdev->dev), &dev_attr_sw_jeita);
	if (ret)
		goto _out;
	ret = device_create_file(&(pdev->dev), &dev_attr_pe20);
	if (ret)
		goto _out;
	ret = device_create_file(&(pdev->dev), &dev_attr_pe30);
	if (ret)
		goto _out;
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

	/* Battery warning */
	ret = device_create_file(&(pdev->dev), &dev_attr_BatteryNotify);
	if (ret)
		goto _out;

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	ret = device_create_file(&(pdev->dev), &dev_attr_BN_TestMode);
	if (ret)
		goto _out;
	/* Pump express */
	ret = device_create_file(&(pdev->dev), &dev_attr_Pump_Express);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_charger_log_level);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_pdc_max_watt);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_ADC_Charger_Voltage);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_input_current);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_chg1_current);
	if (ret)
		goto _out;

	ret = device_create_file(&(pdev->dev), &dev_attr_chg2_current);
	if (ret)
		goto _out;

	battery_dir = proc_mkdir("mtk_battery_cmd", NULL);
	if (!battery_dir) {
		chr_err("[%s]: mkdir /proc/mtk_battery_cmd failed\n", __func__);
		return -ENOMEM;
	}

#if 0
	proc_create_data("battery_cmd", S_IRUGO | S_IWUSR, battery_dir,
			&battery_cmd_proc_fops, info);
	proc_create_data("discharging_cmd", S_IRUGO | S_IWUSR, battery_dir,
			&discharging_cmd_proc_fops, info);
#endif
	proc_create_data("current_cmd", S_IRUGO | S_IWUSR, battery_dir,
			&mtk_charger_current_cmd_fops, info);
	proc_create_data("en_power_path", S_IRUGO | S_IWUSR, battery_dir,
			&mtk_charger_en_power_path_fops, info);
	proc_create_data("en_safety_timer", S_IRUGO | S_IWUSR, battery_dir,
			&mtk_charger_en_safety_timer_fops, info);
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

_out:
	return ret;
}

static int pd_tcp_notifier_call(struct notifier_block *pnb, unsigned long event, void *data)
{
	struct tcp_notify *noti = data;
	struct charger_manager *pinfo;

	pinfo = container_of(pnb, struct charger_manager, pd_nb);

	chr_err("PD charger event:%d %d\r\n", (int)event,
		(int)noti->pd_state.connected);
	switch (event) {
	case TCP_NOTIFY_PD_STATE:
		switch (noti->pd_state.connected) {
		case  PD_CONNECT_NONE:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify Detach\r\n");
			pinfo->pd_type = PD_CONNECT_NONE;
			mutex_unlock(&pinfo->charger_pd_lock);
			/* reset PE40 */
		break;

		case PD_CONNECT_HARD_RESET:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify HardReset\r\n");
			pinfo->pd_type = PD_CONNECT_NONE;
			pinfo->pd_reset = true;
			mutex_unlock(&pinfo->charger_pd_lock);
			_wake_up_charger(pinfo);
		/* reset PE40 */
		break;

		case PD_CONNECT_PE_READY_SNK:
#ifdef VENDOR_EDIT
/* Jianchao.Shi@BSP.CHG.Basic, 2019/08/08, sjc Modify for PISEN PD3.0 adapter */
		case PD_CONNECT_PE_READY_SNK_PD30:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify fixe voltage ready\r\n");
			pinfo->pd_type = noti->pd_state.connected;;
			mutex_unlock(&pinfo->charger_pd_lock);
#else
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify fixe voltage ready\r\n");
			pinfo->pd_type = PD_CONNECT_PE_READY_SNK;
			mutex_unlock(&pinfo->charger_pd_lock);
#endif /*VENDOR_EDIT*/
		/* PD is ready */
		break;

		case PD_CONNECT_PE_READY_SNK_APDO:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify APDO Ready\r\n");
			pinfo->pd_type = PD_CONNECT_PE_READY_SNK_APDO;
			mutex_unlock(&pinfo->charger_pd_lock);
		/* PE40 is ready */
			_wake_up_charger(pinfo);
		break;

		case PD_CONNECT_TYPEC_ONLY_SNK:
			mutex_lock(&pinfo->charger_pd_lock);
			chr_err("PD Notify Type-C Ready\r\n");
			pinfo->pd_type = PD_CONNECT_TYPEC_ONLY_SNK;
			mutex_unlock(&pinfo->charger_pd_lock);
		/* type C is ready */
			_wake_up_charger(pinfo);
		break;

		};
	}
	return NOTIFY_OK;
}


#if defined(VENDOR_EDIT) && defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Add for charging */
//====================================================================//
static void oppo_mt6370_dump_registers(void)
{
	struct charger_device *chg = NULL;

	if (!pinfo->chg1_dev) {
		printk(KERN_ERR "[OPPO_CHG][%s]: oppo_chip not ready!\n", __func__);
		return;
	}
	/*This function runs for more than 400ms, so return when no charger for saving power */
	if (!pmic_chrdet_status() || oppo_get_chg_powersave() == true) {
		return;
	}
	chg = pinfo->chg1_dev;
	charger_dev_dump_registers(chg);
	return;
}

static int oppo_mt6370_kick_wdt(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!pinfo->chg1_dev) {
		printk(KERN_ERR "[OPPO_CHG][%s]: oppo_chip not ready!\n", __func__);
		return 0;
	}
	chg = pinfo->chg1_dev;
	rc = charger_dev_kick_wdt(chg);
	if (rc < 0) {
		chg_debug("charger_dev_kick_wdt fail\n");
	}
	return 0;
}

static int oppo_mt6370_hardware_init(void)
{
	int hw_aicl_point = 4400;
	//int sw_aicl_point = 4500;
	struct charger_device *chg = NULL;

	if (!pinfo->chg1_dev) {
		printk(KERN_ERR "[OPPO_CHG][%s]: oppo_chip not ready!\n", __func__);
		return 0;
	}
	chg = pinfo->chg1_dev;

	oppo_mt6370_reset_charger();

	if (get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT) {
		oppo_mt6370_disable_charging();
		oppo_mt6370_float_voltage_write(4400);
		msleep(100);
	}

	oppo_mt6370_float_voltage_write(4385);

	//set_complete_charge_timeout(OVERTIME_DISABLED);
	mt6370_set_register(0x1C, 0xFF, 0xF9);

	//set_prechg_current(300);
	mt6370_set_register(0x18, 0xF, 0x4);

	oppo_mt6370_charging_current_write_fast(512);

	oppo_mt6370_set_termchg_current(150);

	oppo_mt6370_set_rechg_voltage(100);

	charger_dev_set_mivr(chg, hw_aicl_point * 1000);

#ifdef CONFIG_OPPO_CHARGER_MTK
	if (get_boot_mode() == META_BOOT || get_boot_mode() == FACTORY_BOOT
			|| get_boot_mode() == ADVMETA_BOOT || get_boot_mode() == ATE_FACTORY_BOOT) {
		oppo_mt6370_suspend_charger();
		oppo_mt6370_disable_charging();
	} else {
		oppo_mt6370_unsuspend_charger();
	}
#else /* CONFIG_OPPO_CHARGER_MTK */
	oppo_mt6370_unsuspend_charger();
#endif /* CONFIG_OPPO_CHARGER_MTK */

	oppo_mt6370_enable_charging();

	//set_wdt_timer(REG05_BQ25601D_WATCHDOG_TIMER_40S);
	//mt6370_set_register(0x1D, 0x80, 0x80);
	mt6370_set_register(0x1D, 0x30, 0x10);

	return 0;
}

static int oppo_mt6370_charging_current_write_fast(int chg_curr)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!pinfo->chg1_dev) {
		printk(KERN_ERR "[OPPO_CHG][%s]: oppo_chip not ready!\n", __func__);
		return 0;
	}
	chg = pinfo->chg1_dev;
	rc = charger_dev_set_charging_current(chg, chg_curr * 1000);
	if (rc < 0) {
		chg_debug("set fast charge current:%d fail\n", chg_curr);
	} else {
		//chg_debug("set fast charge current:%d\n", chg_curr);
	}

	return 0;
}

static void oppo_mt6370_set_aicl_point(int vbatt)
{
	int rc = 0;
	static int hw_aicl_point = 4400;
	struct charger_device *chg = NULL;

	if (!pinfo->chg1_dev) {
		printk(KERN_ERR "[OPPO_CHG][%s]: oppo_chip not ready!\n", __func__);
		return;
	}
	chg = pinfo->chg1_dev;

	if (hw_aicl_point == 4400 && vbatt > 4100) {
		hw_aicl_point = 4500;
		//sw_aicl_point = 4550;
	} else if (hw_aicl_point == 4500 && vbatt <= 4100) {
		hw_aicl_point = 4400;
		//sw_aicl_point = 4500;
	}
	rc = charger_dev_set_mivr(chg, hw_aicl_point * 1000);
	if (rc < 0) {
		chg_debug("set aicl point:%d fail\n", hw_aicl_point);
	}
}

static int usb_icl[] = {
	300, 500, 900, 1200, 1350, 1500, 2000, 2500, 3000,
};
struct oppo_chg_operations mt6370_chg_ops;
static int oppo_mt6370_input_current_limit_write(int value)
{
	int rc = 0;
	int i = 0;
	int chg_vol = 0;
	int aicl_point = 0;
	int vbus_mv = 0;
	int ibus_ma = 0;
	struct charger_device *chg = NULL;
	union power_supply_propval val = {0, };
	static struct power_supply *batt_psy = NULL;

	if (!pinfo->chg1_dev) {
		printk(KERN_ERR "[OPPO_CHG][%s]: oppo_chip not ready!\n", __func__);
		return 0;
	}
	chg_debug("usb input max current limit=%d setting %02x\n", value, i);

	chg = pinfo->chg1_dev;

	//aicl_point_temp = g_oppo_chip->sw_aicl_point;
	if (mt6370_chg_ops.oppo_chg_get_pd_type) {
		if (mt6370_chg_ops.oppo_chg_get_pd_type() == true) {
			rc = oppo_pdc_get(&vbus_mv, &ibus_ma);
			if (rc >= 0 && ibus_ma >= 500 && ibus_ma < 3000 && value > ibus_ma) {
				value = ibus_ma;
				chg_debug("usb input max current limit=%d(pd)\n", value);
			}
		}
	}

	if (batt_psy)
		batt_psy = power_supply_get_by_name("battery");
	if (batt_psy && batt_psy->desc->get_property)
		batt_psy->desc->get_property(batt_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	if (val.intval > 4100 )
		aicl_point = 4550;
	else
		aicl_point = 4500;

	if (value < 500) {
		i = 0;
		goto aicl_end;
	}

	mt6370_aicl_enable(false);

	i = 1; /* 500 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		chg_debug( "use 500 here\n");
		goto aicl_end;
	} else if (value < 900)
		goto aicl_end;

	i = 2; /* 900 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (value < 1200)
		goto aicl_end;

	i = 3; /* 1200 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	}

	i = 4; /* 1350 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 2;
		goto aicl_pre_step;
	}

	i = 5; /* 1500 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 3; //We DO NOT use 1.2A here
		goto aicl_pre_step;
	} else if (value < 1500) {
		i = i - 2; //We use 1.2A here
		goto aicl_end;
	} else if (value < 2000)
		goto aicl_end;

	i = 6; /* 2000 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (value < 2500)
		goto aicl_end;

	i = 7; /* 2500 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (value < 3000)
		goto aicl_end;

	i = 8; /* 3000 */
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	msleep(90);
	chg_vol = battery_meter_get_charger_voltage();
	if (chg_vol < aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (value >= 3000)
		goto aicl_end;

aicl_pre_step:
	chg_debug("usb input max current limit aicl chg_vol=%d i[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, usb_icl[i], aicl_point);
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	goto aicl_rerun;
aicl_end:
	chg_debug("usb input max current limit aicl chg_vol=%d i[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, usb_icl[i], aicl_point);
	rc = charger_dev_set_input_current(chg, usb_icl[i] * 1000);
	goto aicl_rerun;
aicl_rerun:
	mt6370_aicl_enable(true);
	return rc;
}

static int oppo_mt6370_float_voltage_write(int vfloat_mv)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!pinfo->chg1_dev) {
		printk(KERN_ERR "[OPPO_CHG][%s]: oppo_chip not ready!\n", __func__);
		return 0;
	}
	chg = pinfo->chg1_dev;
	rc = charger_dev_set_constant_voltage(chg, vfloat_mv * 1000);
	if (rc < 0) {
		chg_debug("set float voltage:%d fail\n", vfloat_mv);
	} else {
		//chg_debug("set float voltage:%d\n", vfloat_mv);
	}

	return 0;
}

static int oppo_mt6370_set_termchg_current(int term_curr)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!pinfo->chg1_dev) {
		printk(KERN_ERR "[OPPO_CHG][%s]: oppo_chip not ready!\n", __func__);
		return 0;
	}
	chg = pinfo->chg1_dev;
	rc = charger_dev_set_eoc_current(chg, term_curr * 1000);
	if (rc < 0) {
		//chg_debug("set termchg_current fail\n");
	}
	return 0;
}

static int oppo_mt6370_enable_charging(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!pinfo->chg1_dev) {
		printk(KERN_ERR "[OPPO_CHG][%s]: oppo_chip not ready!\n", __func__);
		return 0;
	}
	chg = pinfo->chg1_dev;
	rc = charger_dev_enable(chg, true);
	if (rc < 0) {
		chg_debug("enable charging fail\n");
	} else {
		//chg_debug("enable charging\n");
	}

	return 0;
}

static int oppo_mt6370_disable_charging(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!pinfo->chg1_dev) {
		printk(KERN_ERR "[OPPO_CHG][%s]: oppo_chip not ready!\n", __func__);
		return 0;
	}
	chg = pinfo->chg1_dev;
	rc = charger_dev_enable(chg, false);
	if (rc < 0) {
		chg_debug("disable charging fail\n");
	} else {
		//chg_debug("disable charging\n");
	}

	return 0;
}

static int oppo_mt6370_check_charging_enable(void)
{
	return mt6370_check_charging_enable();
}

static int oppo_mt6370_suspend_charger(void)
{
	int rc = 0;

	rc = mt6370_suspend_charger(true);
	if (rc < 0) {
		chg_debug("suspend charger fail\n");
	} else {
		chg_debug("suspend charger\n");
	}
	return 0;
}

static int oppo_mt6370_unsuspend_charger(void)
{
	int rc = 0;

	rc = mt6370_suspend_charger(false);
	if (rc < 0) {
		chg_debug("unsuspend charger fail\n");
	} else {
		chg_debug("unsuspend charger\n");
	}
	return 0;
}

static int oppo_mt6370_set_rechg_voltage(int rechg_mv)
{
	int rc = 0;

	rc = mt6370_set_rechg_voltage(rechg_mv);
	if (rc < 0) {
		chg_debug("set rechg voltage fail:%d\n", rechg_mv);
	}
	return 0;
}

static int oppo_mt6370_reset_charger(void)
{
	//int rc = 0;

	//rc = mt6370_reset_charger();
	//if (rc < 0) {
	//	chg_debug("reset charger fail\n");
	//}
	return 0;
}

static int oppo_mt6370_registers_read_full(void)
{
	bool full = false;
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!pinfo->chg1_dev) {
		printk(KERN_ERR "[OPPO_CHG][%s]: oppo_chip not ready!\n", __func__);
		return 0;
	}
	chg = pinfo->chg1_dev;
	rc = charger_dev_is_charging_done(chg, &full);
	if (rc < 0) {
		chg_debug("registers read full  fail\n");
		full = false;
	} else {
		//chg_debug("registers read full\n");
	}

	return full;
}

static int oppo_mt6370_otg_enable(void)
{
	int rc = 0;
	struct charger_device *chg = NULL;

	if (!pinfo->chg1_dev) {
		printk(KERN_ERR "[OPPO_CHG][%s]: oppo_chip not ready!\n", __func__);
		return 0;
	}
	chg = pinfo->chg1_dev;
	rc =  charger_dev_enable_otg(chg, true);
	if (rc < 0) {
		chg_debug("set enable_otg fail\n");
	}
	return 0;
}

static int oppo_mt6370_otg_disable(void)
{
	int rc = 0;
		struct charger_device *chg = NULL;
	
		if (!pinfo->chg1_dev) {
			printk(KERN_ERR "[OPPO_CHG][%s]: oppo_chip not ready!\n", __func__);
			return 0;
		}
		chg = pinfo->chg1_dev;
		rc =  charger_dev_enable_otg(chg, false);
		if (rc < 0) {
			chg_debug("set enable_otg fail\n");
		}
	return 0;

}

static int oppo_mt6370_set_chging_term_disable(void)
{
	int rc = 0;

	rc = mt6370_set_chging_term_disable(true);
	if (rc < 0) {
		chg_debug("disable chging_term fail\n");
	}
	return 0;
}

static bool oppo_mt6370_check_charger_resume(void)
{
	return true;
}

static int oppo_mt6370_get_chg_current_step(void)
{
	return 100;
}

static int mt_power_supply_type_check(void)
{
	int charger_type = POWER_SUPPLY_TYPE_UNKNOWN;

	switch(mt_get_charger_type()) {
	case CHARGER_UNKNOWN:
		break;
	case STANDARD_HOST:
		charger_type = POWER_SUPPLY_TYPE_USB;
		break;
	case CHARGING_HOST:
		charger_type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case NONSTANDARD_CHARGER:
	case STANDARD_CHARGER:
	case APPLE_2_1A_CHARGER:
	case APPLE_1_0A_CHARGER:
	case APPLE_0_5A_CHARGER:
		charger_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	default:
		break;
	}
	chg_debug("charger_type[%d]\n", charger_type);
	return charger_type;
}

/*static int battery_meter_get_charger_voltage(void)
{
	return battery_get_vbus();
}*/

/*static bool oppo_mt6370_get_vbus_status(void)
{
	bool vbus_status = false;
	static bool pre_vbus_status = false;
	int ret = 0;

	ret = mt6370_get_vbus_rising();
	if (ret < 0) {
		if (g_oppo_chip && g_oppo_chip->unwakelock_chg == 1) {
			printk(KERN_ERR "[OPPO_CHG][%s]: unwakelock_chg=1, use pre status\n", __func__);
			return pre_vbus_status;
		} else {
			return false;
		}
	}
	if (ret == 0)
		vbus_status = false;
	else
		vbus_status = true;
	pre_vbus_status = vbus_status;
	return vbus_status;
}*/

static int oppo_battery_meter_get_battery_voltage(void)
{
	return 4000;
}

static int get_rtc_spare_oppo_fg_value(void)
{
	return 0;
}

static int set_rtc_spare_oppo_fg_value(int soc)
{
	return 0;
}

static void oppo_mt_power_off(void)
{
	union power_supply_propval val = {0, };
	static struct power_supply *ac_psy = NULL;
	struct tcpc_device *tcpc_dev = NULL;

	if (ac_psy == NULL)
		ac_psy = power_supply_get_by_name("ac");
	if (ac_psy && ac_psy->desc->get_property)
		ac_psy->desc->get_property(ac_psy, POWER_SUPPLY_PROP_ONLINE, &val);

	if (val.intval != 1) {
		if (tcpc_dev == NULL)
			tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
		if (tcpc_dev) {
			if (!(tcpc_dev->pd_wait_hard_reset_complete)
					&& !pmic_chrdet_status()) {
				mt_power_off();
			}
		}
	} else {
		printk(KERN_ERR "[OPPO_CHG][%s]: ac_online is true, return!\n", __func__);
	}
}

#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
/* This function is getting the dynamic aicl result/input limited in mA.
 * If charger was suspended, it must return 0(mA).
 * It meets the requirements in SDM660 platform.
 */
static int oppo_mt6370_chg_get_dyna_aicl_result(void)
{
	int rc = 0;
	int aicl_ma = 0;
	struct charger_device *chg = NULL;

	if (!pinfo->chg1_dev) {
		printk(KERN_ERR "[OPPO_CHG][%s]: oppo_chip not ready!\n", __func__);
		return 0;
	}
	chg = pinfo->chg1_dev;
	rc = charger_dev_get_input_current(chg, &aicl_ma);
	if (rc < 0) {
		chg_debug("get dyna aicl fail\n");
		return 500;
	}
	return aicl_ma / 1000;
}
#endif

#ifdef CONFIG_OPPO_RTC_DET_SUPPORT
static int rtc_reset_check(void)
{
	int rc = 0;
	struct rtc_time tm;
	struct rtc_device *rtc;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		return 0;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	if ((tm.tm_year == 70) && (tm.tm_mon == 0) && (tm.tm_mday <= 1)) {
		chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  @@@ wday: %d, yday: %d, isdst: %d\n",
			tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
			tm.tm_wday, tm.tm_yday, tm.tm_isdst);
		rtc_class_close(rtc);
		return 1;
	}

	chg_debug(": Sec: %d, Min: %d, Hour: %d, Day: %d, Mon: %d, Year: %d  ###  wday: %d, yday: %d, isdst: %d\n",
		tm.tm_sec, tm.tm_min, tm.tm_hour, tm.tm_mday, tm.tm_mon, tm.tm_year,
		tm.tm_wday, tm.tm_yday, tm.tm_isdst);

close_time:
	rtc_class_close(rtc);
	return 0;
}
#endif /* CONFIG_OPPO_RTC_DET_SUPPORT */
//====================================================================//
#endif /* VENDOR_EDIT && CONFIG_OPPO_CHARGER_MT6370_TYPEC */


#if defined(VENDOR_EDIT) && defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/15, sjc Add for OTG switch */
//====================================================================//
void oppo_set_otg_switch_status(bool value)
{
	if (pinfo != NULL && pinfo->tcpc != NULL) {
		printk(KERN_ERR "[OPPO_CHG][%s]: otg switch[%d]\n", __func__, value);
		tcpm_typec_change_role(pinfo->tcpc, value ? TYPEC_ROLE_DRP : TYPEC_ROLE_SNK);
	}
}
EXPORT_SYMBOL(oppo_set_otg_switch_status);

int oppo_get_typec_cc_orientation(void)
{
	int typec_cc_orientation = 0;

	if (pinfo != NULL && pinfo->tcpc != NULL) {
		if (tcpm_inquire_typec_attach_state(pinfo->tcpc) != TYPEC_UNATTACHED) {
			typec_cc_orientation = (int)tcpm_inquire_cc_polarity(pinfo->tcpc) + 1;
		} else {
			typec_cc_orientation = 0;
		}
		if (typec_cc_orientation != 0)
			printk(KERN_ERR "[OPPO_CHG][%s]: cc[%d]\n", __func__, typec_cc_orientation);
	} else {
		typec_cc_orientation = 0;
	}
	return typec_cc_orientation;
}
EXPORT_SYMBOL(oppo_get_typec_cc_orientation);
//====================================================================//
#endif /* VENDOR_EDIT && CONFIG_OPPO_CHARGER_MT6370_TYPEC */


//====================================================================//
#if defined(VENDOR_EDIT) && defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/07/31, sjc Add for PD */
static bool oppo_mt6370_get_pd_type(void)
{
	if (pinfo != NULL) {
		return mtk_pdc_check_charger(pinfo);
	}
	return false;
}

static int oppo_mt6370_pd_setup(void)
{
	int vbus_mv = 0;
	int ibus_ma = 0;
	int ret = 0;

	ret = oppo_pdc_setup(&vbus_mv, &ibus_ma);
	//printk(KERN_ERR "%s: vbus[%d], ibus[%d]\n", __func__, vbus_mv, ibus_ma);
	return ret;
}
#endif /* VENDOR_EDIT && CONFIG_OPPO_CHARGER_MT6370_TYPEC */
//====================================================================//


#if defined(VENDOR_EDIT) && defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Add for charging */
//====================================================================//
struct oppo_chg_operations  mt6370_chg_ops = {
	.dump_registers = oppo_mt6370_dump_registers,
	.kick_wdt = oppo_mt6370_kick_wdt,
	.hardware_init = oppo_mt6370_hardware_init,
	.charging_current_write_fast = oppo_mt6370_charging_current_write_fast,
	.set_aicl_point = oppo_mt6370_set_aicl_point,
	.input_current_write = oppo_mt6370_input_current_limit_write,
	.float_voltage_write = oppo_mt6370_float_voltage_write,
	.term_current_set = oppo_mt6370_set_termchg_current,
	.charging_enable = oppo_mt6370_enable_charging,
	.charging_disable = oppo_mt6370_disable_charging,
	.get_charging_enable = oppo_mt6370_check_charging_enable,
	.charger_suspend = oppo_mt6370_suspend_charger,
	.charger_unsuspend = oppo_mt6370_unsuspend_charger,
	.set_rechg_vol = oppo_mt6370_set_rechg_voltage,
	.reset_charger = oppo_mt6370_reset_charger,
	.read_full = oppo_mt6370_registers_read_full,
	.otg_enable = oppo_mt6370_otg_enable,
	.otg_disable = oppo_mt6370_otg_disable,
	.set_charging_term_disable = oppo_mt6370_set_chging_term_disable,
	.check_charger_resume = oppo_mt6370_check_charger_resume,
	.get_chg_current_step = oppo_mt6370_get_chg_current_step,
#ifdef CONFIG_OPPO_CHARGER_MTK
	.get_charger_type = mt_power_supply_type_check,
	.get_charger_volt = battery_meter_get_charger_voltage,
	.check_chrdet_status = (bool (*) (void)) pmic_chrdet_status,
	.get_instant_vbatt = oppo_battery_meter_get_battery_voltage,
	.get_boot_mode = (int (*)(void))get_boot_mode,
	.get_boot_reason = (int (*)(void))get_boot_reason,
	.get_chargerid_volt = mt_get_chargerid_volt,
	.set_chargerid_switch_val = mt_set_chargerid_switch_val ,
	.get_chargerid_switch_val  = mt_get_chargerid_switch_val,
	.get_rtc_soc = get_rtc_spare_oppo_fg_value,
	.set_rtc_soc = set_rtc_spare_oppo_fg_value,
	.set_power_off = oppo_mt_power_off,
	//.usb_connect = mt_usb_connect,
	//.usb_disconnect = mt_usb_disconnect,
#else /* CONFIG_OPPO_CHARGER_MTK */
	.get_charger_type = qpnp_charger_type_get,
	.get_charger_volt = qpnp_get_prop_charger_voltage_now,
	.check_chrdet_status = qpnp_lbc_is_usb_chg_plugged_in,
	.get_instant_vbatt = qpnp_get_prop_battery_voltage_now,
	.get_boot_mode = get_boot_mode,
	.get_rtc_soc = qpnp_get_pmic_soc_memory,
	.set_rtc_soc = qpnp_set_pmic_soc_memory,
#endif /* CONFIG_OPPO_CHARGER_MTK */

#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
	.get_dyna_aicl_result = oppo_mt6370_chg_get_dyna_aicl_result,
#endif
	.get_shortc_hw_gpio_status = oppo_chg_get_shortc_hw_gpio_status,
#ifdef CONFIG_OPPO_RTC_DET_SUPPORT
	.check_rtc_reset = rtc_reset_check,
#endif
	.oppo_chg_get_pd_type = oppo_mt6370_get_pd_type,
	.oppo_chg_pd_setup = oppo_mt6370_pd_setup,
};
//====================================================================//
#endif /* VENDOR_EDIT && CONFIG_OPPO_CHARGER_MT6370_TYPEC */


static int mtk_charger_probe(struct platform_device *pdev)
{
	struct charger_manager *info = NULL;
#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	struct list_head *pos;
	struct list_head *phead = &consumer_head;
	struct charger_consumer *ptr;
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */
	int ret;

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
#ifdef VENDOR_EDIT
/* Qiao.Hu@BSP.CHG.Basic, 2018/8/8,  Add for charging*/
    return 0;
#endif
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

	chr_err("%s: starts\n", __func__);

	info = devm_kzalloc(&pdev->dev, sizeof(struct charger_manager), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	pinfo = info;

	platform_set_drvdata(pdev, info);
	info->pdev = pdev;

#if defined(VENDOR_EDIT) && defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	info->chg1_dev = get_charger_by_name("primary_chg");
	if (info->chg1_dev) {
		chr_err("found primary charger [%s]\n",
			info->chg1_dev->props.alias_name);
	} else {
		chr_err("can't find primary charger!\n");
	}
	charger_ic_flag = 4;
#endif /* VENDOR_EDIT && CONFIG_OPPO_CHARGER_MT6370_TYPEC */

	mtk_charger_parse_dt(info, &pdev->dev);

	mutex_init(&info->charger_lock);
	mutex_init(&info->charger_pd_lock);

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	atomic_set(&info->enable_kpoc_shdn, 1);
	wake_lock_init(&info->charger_wakelock, WAKE_LOCK_SUSPEND, "charger suspend wakelock");
	spin_lock_init(&info->slock);

	/* init thread */
	init_waitqueue_head(&info->wait_que);
	info->polling_interval = CHARGING_INTERVAL;
	info->enable_dynamic_cv = true;

	info->chg1_data.thermal_charging_current_limit = -1;
	info->chg1_data.thermal_input_current_limit = -1;
	info->chg1_data.input_current_limit_by_aicl = -1;
	info->chg2_data.thermal_charging_current_limit = -1;
	info->chg2_data.thermal_input_current_limit = -1;

	info->sw_jeita.error_recovery_flag = true;

	mtk_charger_init_timer(info);

	kthread_run(charger_routine_thread, info, "charger_thread");

	if (info->chg1_dev != NULL && info->do_event != NULL) {
		info->chg1_nb.notifier_call = info->do_event;
		register_charger_device_notifier(info->chg1_dev, &info->chg1_nb);
		charger_dev_set_drvdata(info->chg1_dev, info);
	}
#ifdef VENDOR_EDIT
//Qiao.Hu@BSP.BaseDrv.CHG.Basic, 2018/01/12, add for fast chargering.
	//info->psy_nb.notifier_call = charger_psy_event;
	//power_supply_reg_notifier(&info->psy_nb);
#endif
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

	srcu_init_notifier_head(&info->evt_nh);
	ret = mtk_charger_setup_files(pdev);
	if (ret)
		chr_err("Error creating sysfs interface\n");

	pinfo->tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (pinfo->tcpc != NULL) {
		pinfo->pd_nb.notifier_call = pd_tcp_notifier_call;
		ret = register_tcp_dev_notifier(pinfo->tcpc, &pinfo->pd_nb, TCP_NOTIFY_TYPE_USB);
	} else {
		chr_err("get PD dev fail\n");
	}

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	if (mtk_pe_init(info) < 0)
		info->enable_pe_plus = false;

	if (mtk_pe20_init(info) < 0)
		info->enable_pe_2 = false;

	if (mtk_pe30_init(info) == false)
		info->enable_pe_3 = false;

	if (mtk_pe40_init(info) == false)
		info->enable_pe_4 = false;
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

	mtk_pdc_init(info);

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
	charger_ftm_init();

	mutex_lock(&consumer_mutex);
	list_for_each(pos, phead) {
		ptr = container_of(pos, struct charger_consumer, list);
		ptr->cm = info;
		if (ptr->pnb != NULL) {
			srcu_notifier_chain_register(&info->evt_nh, ptr->pnb);
			ptr->pnb = NULL;
		}
	}
	mutex_unlock(&consumer_mutex);
	info->chg1_consumer = charger_manager_get_by_name(&pdev->dev, "charger_port1");
	info->init_done = true;
	_wake_up_charger(info);
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

	return 0;
}

static int mtk_charger_remove(struct platform_device *dev)
{
	return 0;
}

static void mtk_charger_shutdown(struct platform_device *dev)
{
	struct charger_manager *info = platform_get_drvdata(dev);

#if defined(VENDOR_EDIT) && !defined(CONFIG_OPPO_CHARGER_MT6370_TYPEC)
/* Jianchao.Shi@BSP.CHG.Basic, 2019/06/10, sjc Modify for charging */
#ifdef VENDOR_EDIT
/* Qiao.Hu@BSP.CHG.Basic, 2018/8/9,  Add for charging*/
    return;
#endif
#endif /* VENDOR_EDIT && !CONFIG_OPPO_CHARGER_MT6370_TYPEC */

	if (mtk_pe20_get_is_connect(info) || mtk_pe_get_is_connect(info)) {
		if (info->chg2_dev)
			charger_dev_enable(info->chg2_dev, false);
		mtk_pe20_reset_ta_vchr(info);
		pr_debug("%s: reset TA before shutdown\n", __func__);
	}
}

static const struct of_device_id mtk_charger_of_match[] = {
	{.compatible = "mediatek,charger",},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_charger_of_match);

struct platform_device charger_device = {
	.name = "charger",
	.id = -1,
};

static struct platform_driver charger_driver = {
	.probe = mtk_charger_probe,
	.remove = mtk_charger_remove,
	.shutdown = mtk_charger_shutdown,
	.driver = {
		   .name = "charger",
		   .of_match_table = mtk_charger_of_match,
		   },
};

static int __init mtk_charger_init(void)
{
	return platform_driver_register(&charger_driver);
}
late_initcall(mtk_charger_init);

static void __exit mtk_charger_exit(void)
{
	platform_driver_unregister(&charger_driver);
}
module_exit(mtk_charger_exit);


MODULE_AUTHOR("wy.chuang <wy.chuang@mediatek.com>");
MODULE_DESCRIPTION("MTK Charger Driver");
MODULE_LICENSE("GPL");
