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

#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/ktime.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include "ion_drv.h"
#include "mtk_ion.h"
/* #include "mtk_idle.h" */
#include "mtk_spm_reg.h"
/* #include "pcm_def.h" */
/* #include "mtk_spm_idle.h" */
#include "mtk_smi.h"

#include "disp_drv_platform.h"
#include "debug.h"
#include "ddp_debug.h"
#include "disp_drv_log.h"
#include "disp_lcm.h"
#include "disp_utils.h"
#include "disp_session.h"
#include "primary_display.h"
#include "disp_helper.h"
#include "cmdq_def.h"
#include "cmdq_record.h"
#include "cmdq_reg.h"
#include "cmdq_core.h"
#include "ddp_manager.h"
#include "disp_lcm.h"
#include "ddp_clkmgr.h"
#include "mtk_smi.h"
/* #include "mmdvfs_mgr.h" */
#include "disp_drv_log.h"
#include "ddp_log.h"
#include "disp_lowpower.h"
/* device tree */
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/io.h>
/* #include "mach/eint.h" */
#if defined(CONFIG_MTK_LEGACY)
#include <mach/mtk_gpio.h>
#include <cust_gpio_usage.h>
#include <cust_eint.h>
#else
#include "disp_dts_gpio.h"
#include <linux/gpio.h>
#endif

#include "disp_recovery.h"
#include "disp_partial.h"
#include "ddp_dsi.h"
#include "mtk_disp_mgr.h"
#include "mtk_ovl.h"

#ifdef VENDOR_EDIT
/* Yongpeng.Yi@PSW.MM.Display.LCD.Stability, 2018/09/24,add mm critical log */
#include <soc/oppo/mmkey_log.h>
/*
* liping-m@PSW.MM.Display.LCD.Stability, 2018/07/21,
* add power seq api for ulps
*/
#include <soc/oppo/oppo_project.h>

#endif /*VENDOR_EDIT*/

#ifdef VENDOR_EDIT
/*Cong.Dai@PSW.BSP.TP, 2017/11/18, add for stop lcd esd check until firmware update*/
static atomic_t esd_check_fw_updated = ATOMIC_INIT(0);
#endif /*VENDOR_EDIT*/

static struct task_struct *primary_display_check_task; /* For abnormal check */
static struct task_struct *primary_recovery_thread;
static wait_queue_head_t _check_task_wq; /* used for blocking check task  */
static atomic_t _check_task_wakeup = ATOMIC_INIT(0); /* For  Check Task */

static wait_queue_head_t esd_ext_te_wq;	/* For EXT TE EINT Check */
static atomic_t esd_ext_te_event = ATOMIC_INIT(0); /* For EXT TE EINT Check */
static unsigned int esd_check_mode;
static unsigned int esd_check_enable;

atomic_t enable_wdma_recovery = ATOMIC_INIT(0);
atomic_t enable_rdma_recovery = ATOMIC_INIT(0);
atomic_t enable_ovl0_recovery = ATOMIC_INIT(0);
atomic_t enable_ovl0_2l_recovery = ATOMIC_INIT(0);


#if (CONFIG_MTK_DUAL_DISPLAY_SUPPORT == 2)
/***********external display dual LCM ESD check******************/
static struct task_struct *external_display_check_task; /* For abnormal check */
static wait_queue_head_t extd_check_task_wq;	/* used for blocking check task  */
static atomic_t extd_check_task_wakeup = ATOMIC_INIT(0);	/* For  Check Task */

static wait_queue_head_t esd_ext_te_1_wq;	/* For EXT TE EINT Check */
static atomic_t esd_ext_te_1_event = ATOMIC_INIT(0);	/* For EXT TE EINT Check */
static unsigned int extd_esd_check_mode;
static unsigned int extd_esd_check_enable;
#endif

#ifdef VENDOR_EDIT
/*
* ZhongWenjie@PSW.BSP.TP.FUNCTION, 2019/02/26,
* add to trigger tp irq reset for esd recovery
*/
void __attribute__((weak)) lcd_trigger_tp_irq_reset(void) {return;}
#endif

#ifdef VENDOR_EDIT
/*Yongpeng.Yi@PSW.MM.Display.LCD.Stability 2017/12/29, Add for lcm ic esd recovery backlight */
unsigned int esd_recovery_backlight_level = 2;
/*Yongpeng.Yi@PSW.MM.Display.LCD.Stability, 2018/07/04, add for hx83112a lcd esd read reg*/
static atomic_t enable_lcm_recovery = ATOMIC_INIT(0);
#endif /* VENDOR_EDIT */
unsigned int get_esd_check_mode(void)
{
	return esd_check_mode;
}

void set_esd_check_mode(unsigned int mode)
{
	esd_check_mode = mode;
}

unsigned int _can_switch_check_mode(void)
{
	int ret = 0;

	if (primary_get_lcm()->params->dsi.customization_esd_check_enable == 0 &&
	    primary_get_lcm()->params->dsi.lcm_esd_check_table[0].cmd != 0)
		ret = 1;
	return ret;
}

static unsigned int _need_do_esd_check(void)
{
	int ret = 0;

#ifdef CONFIG_OF
	if ((primary_get_lcm()->params->dsi.esd_check_enable == 1) && (islcmconnected == 1))
		ret = 1;
#else
	if (primary_get_lcm()->params->dsi.esd_check_enable == 1)
		ret = 1;
#endif
	return ret;
}

/**
 * For Cmd Mode Read LCM Check
 * Config cmdq_handle_config_esd
 */
int _esd_check_config_handle_cmd(struct cmdqRecStruct *qhandle)
{
	int ret = 0; /* 0:success */
	disp_path_handle phandle = primary_get_dpmgr_handle();

	/* 1.reset */
	cmdqRecReset(qhandle);

	primary_display_manual_lock();

	/* 2.write first instruction */
	/* cmd mode: wait CMDQ_SYNC_TOKEN_STREAM_EOF(wait trigger thread done) */
	cmdqRecWaitNoClear(qhandle, CMDQ_SYNC_TOKEN_STREAM_EOF);

	/* 3.clear CMDQ_SYNC_TOKEN_ESD_EOF(trigger thread need wait this sync token) */
	cmdqRecClearEventToken(qhandle, CMDQ_SYNC_TOKEN_ESD_EOF);

	/* 4.write instruction(read from lcm) */
	dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_ESD_CHECK_READ, 0);

	/* 5.set CMDQ_SYNC_TOKE_ESD_EOF(trigger thread can work now) */
	cmdqRecSetEventToken(qhandle, CMDQ_SYNC_TOKEN_ESD_EOF);

	primary_display_manual_unlock();

	/* 6.flush instruction */
	dprec_logger_start(DPREC_LOGGER_ESD_CMDQ, 0, 0);
	ret = cmdqRecFlush(qhandle);
	dprec_logger_done(DPREC_LOGGER_ESD_CMDQ, 0, 0);

	DISPINFO("[ESD]_esd_check_config_handle_cmd ret=%d\n", ret);

	if (ret)
		ret = 1;
	return ret;
}

/**
 * For Vdo Mode Read LCM Check
 * Config cmdq_handle_config_esd
 * return value: 0:success, 1:fail
 */
int _esd_check_config_handle_vdo(struct cmdqRecStruct *qhandle)
{
	int ret = 0;
	disp_path_handle phandle = primary_get_dpmgr_handle();

	/* 1.reset */
	cmdqRecReset(qhandle);

	/* wait stream eof first */
	/* cmdqRecWait(qhandle, CMDQ_EVENT_DISP_RDMA0_EOF); */
	cmdqRecWait(qhandle, CMDQ_EVENT_MUTEX0_STREAM_EOF);

	primary_display_manual_lock();
	/* 2.stop dsi vdo mode */
	dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_STOP_VDO_MODE, 0);

	/* 3.write instruction(read from lcm) */
	dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_ESD_CHECK_READ, 0);

	/* 4.start dsi vdo mode */
	dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_START_VDO_MODE, 0);
	cmdqRecClearEventToken(qhandle, CMDQ_EVENT_MUTEX0_STREAM_EOF);
	/* cmdqRecClearEventToken(qhandle, CMDQ_EVENT_DISP_RDMA0_EOF); */
	/* 5.trigger path */
	dpmgr_path_trigger(phandle, qhandle, CMDQ_ENABLE);

	/* mutex sof wait*/
	ddp_mutex_set_sof_wait(dpmgr_path_get_mutex(phandle), qhandle, 0);

	primary_display_manual_unlock();

	/* 6.flush instruction */
	dprec_logger_start(DPREC_LOGGER_ESD_CMDQ, 0, 0);
	ret = cmdqRecFlush(qhandle);
	dprec_logger_done(DPREC_LOGGER_ESD_CMDQ, 0, 0);

	DISPINFO("[ESD]_esd_check_config_handle_vdo ret=%d\n", ret);

	if (ret)
		ret = 1;
	return ret;
}

/* For EXT TE EINT Check */
static irqreturn_t _esd_check_ext_te_irq_handler(int irq, void *data)
{
	mmprofile_log_ex(ddp_mmp_get_events()->esd_vdo_eint, MMPROFILE_FLAG_PULSE, 0, 0);
	atomic_set(&esd_ext_te_event, 1);
	wake_up_interruptible(&esd_ext_te_wq);
	return IRQ_HANDLED;
}

#if defined(CONFIG_MTK_LEGACY)
int primary_display_switch_esd_mode(int mode)
{
	int ret = 0;
	int gpio_mode = 0;
	struct device_node *node;
	int irq;
	u32 ints[2] = { 0, 0 };

#ifndef GPIO_DSI_TE_PIN
	return ret;
#endif

	gpio_mode = mt_get_gpio_mode(GPIO_DSI_TE_PIN);

	/* switch to eint mode */
	if (mode == GPIO_EINT_MODE) {
		/* 1. set mode0 */
		/* keep dsi te mode */
		/* mt_set_gpio_mode(GPIO_DSI_TE_PIN, GPIO_DSI_TE_PIN_M_GPIO); */

		/* 2.register irq handler */
		node = of_find_compatible_node(NULL, NULL, "mediatek, DSI_TE-eint");
		if (node) {
			of_property_read_u32_array(node, "debounce",
						   ints, ARRAY_SIZE(ints));
			mt_gpio_set_debounce(ints[0], ints[1]);
			irq = irq_of_parse_and_map(node, 0);
			if (primary_get_lcm()->params->dsi.ext_te_edge == LCM_POLARITY_FALLING) {
				if (request_irq(irq, _esd_check_ext_te_irq_handler,
						IRQF_TRIGGER_FALLING, "DSI_TE-eint", NULL))
					/*IRQF_TRIGGER_NONE*/
					DDPPR_ERR("[ESD]EINT IRQ LINE NOT AVAILABLE!!\n");
			} else {
				if (request_irq(irq, _esd_check_ext_te_irq_handler,
						IRQF_TRIGGER_RISING, "DSI_TE-eint", NULL))
					/*IRQF_TRIGGER_NONE*/
					DDPPR_ERR("[ESD]EINT IRQ LINE NOT AVAILABLE!!\n");
			}
		} else {
			DDPPR_ERR("[ESD][%s] can't find DSI_TE eint compatible node\n",  __func__);
		}
	}

	if (mode == GPIO_DSI_MODE) {
		/* 1. unregister irq handler */
		node = of_find_compatible_node(NULL, NULL, "mediatek, DSI_TE-eint");
		if (node) {
			irq = irq_of_parse_and_map(node, 0);
			free_irq(irq, NULL);
		} else {
			DDPPR_ERR("[ESD][%s] can't find DSI_TE eint compatible node\n",  __func__);
		}

		/* 2. set dsi te mode */
		/* mt_set_gpio_mode(GPIO_DSI_TE_PIN, GPIO_DSI_TE_PIN_M_DSI_TE); */
	}

	return ret;
}
#else /* CONFIG_MTK_LEGACY */
int primary_display_switch_esd_mode(int mode)
{
	int ret = 0;
	/* int gpio_mode = 0; */
	struct device_node *node;
	int irq;
	u32 ints[2] = { 0, 0 };

	if (mode == GPIO_EINT_MODE) {
		/* 1. set mode0 */
		/* disp_dts_gpio_select_state(DTS_GPIO_STATE_TE_MODE_GPIO); */

		/* 2.register irq handler */
		node = of_find_compatible_node(NULL, NULL, "mediatek, DSI_TE-eint");
		if (node) {
			of_property_read_u32_array(node, "debounce",
						   ints, ARRAY_SIZE(ints));
			/* mt_gpio_set_debounce(ints[0], ints[1]); */
			irq = irq_of_parse_and_map(node, 0);
			if (request_irq(irq, _esd_check_ext_te_irq_handler,
					IRQF_TRIGGER_RISING, "DSI_TE-eint", NULL))
				/* IRQF_TRIGGER_NONE */
				DDPPR_ERR("[ESD]EINT IRQ LINE NOT AVAILABLE!!\n");
		} else {
			DDPPR_ERR("[ESD][%s] can't find DSI_TE eint compatible node\n", __func__);
		}
	} else if (mode == GPIO_DSI_MODE) {
		/* 1. unregister irq handler */
		node = of_find_compatible_node(NULL, NULL, "mediatek, DSI_TE-eint");
		if (node) {
			irq = irq_of_parse_and_map(node, 0);
			free_irq(irq, NULL);
		}

		/* disp_dts_gpio_select_state(DTS_GPIO_STATE_TE_MODE_TE); */
	}

	return ret;
}
#endif /* CONFIG_MTK_LEGACY */

int do_esd_check_eint(void)
{
	int ret = 0;

	if (wait_event_interruptible_timeout(esd_ext_te_wq, atomic_read(&esd_ext_te_event), HZ / 2) > 0)
		ret = 0; /* esd check pass */
	else
		ret = 1; /* esd check fail */

	#ifdef VENDOR_EDIT
	/* Yongpeng.Yi@PSW.MM.Display.LCD.Stability, 2018/09/24,add mm critical log */
	if (ret) {
		mm_keylog_write("Dispaly Panel do_esd_check_eint fail\n", "ESD\n", TYPE_ESD_EXCEPTION);
	}
	#endif /* VENDOR_EDIT */
	atomic_set(&esd_ext_te_event, 0);

	return ret;
}

int do_esd_check_dsi_te(void)
{
	int ret = 0;

	if (dpmgr_wait_event_timeout(primary_get_dpmgr_handle(), DISP_PATH_EVENT_IF_VSYNC, HZ / 2) > 0)
		ret = 0; /* esd check pass */
	else
		ret = 1; /* esd check fail */

	return ret;
}

int do_esd_check_read(void)
{
	int ret = 0;
	struct cmdqRecStruct *qhandle;
	disp_path_handle phandle = primary_get_dpmgr_handle();

	/* 0.create esd check cmdq */
	ret = cmdqRecCreate(CMDQ_SCENARIO_DISP_ESD_CHECK, &qhandle);
	if (ret) {
		DDPPR_ERR("%s:%d, create cmdq handle fail!ret=%d\n", __func__, __LINE__, ret);
		return -1;
	}
	cmdqRecReset(qhandle);

	primary_display_manual_lock();
	dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_ESD_ALLC_SLOT, 0);
	primary_display_manual_unlock();

	/* 1.use cmdq to read from lcm */
	if (primary_display_is_video_mode())
		ret = _esd_check_config_handle_vdo(qhandle);
	else
		ret = _esd_check_config_handle_cmd(qhandle);

	primary_display_manual_lock();

	if (ret == 1) {	/* cmdq fail */
		if (need_wait_esd_eof()) {
			/* Need set esd check eof synctoken to let trigger loop go. */
			cmdqCoreSetEvent(CMDQ_SYNC_TOKEN_ESD_EOF);
		}
		/* do dsi reset */
		dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_DSI_RESET, 0);
		#ifdef VENDOR_EDIT
		/* Yongpeng.Yi@PSW.MM.Display.LCD.Stability, 2017/12/13,add mm critical log */
		mm_keylog_write("Dispaly Panel do_esd_check_read fail\n", "ESD\n", TYPE_ESD_EXCEPTION);
		#endif /* VENDOR_EDIT */
		goto destroy_cmdq;
	}

	/* 2.check data(*cpu check now) */
	ret = dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_ESD_CHECK_CMP, 0);
	if (ret)
		ret = 1; /* esd check fail */

destroy_cmdq:
	dpmgr_path_build_cmdq(phandle, qhandle, CMDQ_ESD_FREE_SLOT, 0);

	primary_display_manual_unlock();

	/* 3.destroy esd config thread */
	cmdqRecDestroy(qhandle);

	return ret;
}

/**
 * ESD CHECK FUNCTION
 * return 1: esd check fail
 * return 0: esd check pass
 */
int primary_display_esd_check(void)
{
	int ret = 0;
	unsigned int mode;
	mmp_event mmp_te = ddp_mmp_get_events()->esd_extte;
	mmp_event mmp_rd = ddp_mmp_get_events()->esd_rdlcm;
	mmp_event mmp_chk = ddp_mmp_get_events()->esd_check_t;

	dprec_logger_start(DPREC_LOGGER_ESD_CHECK, 0, 0);
	mmprofile_log_ex(mmp_chk, MMPROFILE_FLAG_START, 0, 0);
	DISPINFO("[ESD]ESD check begin\n");

	primary_display_manual_lock();
	if (primary_get_state() == DISP_SLEPT) {
		mmprofile_log_ex(mmp_chk, MMPROFILE_FLAG_PULSE, 1, 0);
		DISPINFO("[ESD]primary display path is slept?? -- skip esd check\n");
		primary_display_manual_unlock();
		goto done;
	}
	primary_display_manual_unlock();

	/*  Esd Check : EXT TE */
	if (primary_get_lcm()->params->dsi.customization_esd_check_enable == 0) {
		/* use te for esd check */
		mmprofile_log_ex(mmp_te, MMPROFILE_FLAG_START, 0, 0);

		mode = get_esd_check_mode();
		if (mode == GPIO_EINT_MODE) {
			DISPINFO("[ESD]ESD check eint\n");
			mmprofile_log_ex(mmp_te, MMPROFILE_FLAG_PULSE,
				primary_display_is_video_mode(), mode);
			primary_display_switch_esd_mode(mode);
			ret = do_esd_check_eint();
			mode = GPIO_DSI_MODE; /* used for mode switch */
			primary_display_switch_esd_mode(mode);
		} else if (mode == GPIO_DSI_MODE) {
			mmprofile_log_ex(mmp_te, MMPROFILE_FLAG_PULSE,
				primary_display_is_video_mode(), mode);
#if 0
			/* use eint do esd check instead of dsi te irq for lowpower */
			ret = do_esd_check_dsi_te();
#else
			DISPCHECK("[ESD]ESD check read\n");
			ret = do_esd_check_read();
#endif
			mode = GPIO_EINT_MODE; /* used for mode switch */
		}

		if (disp_helper_get_option(DISP_OPT_ESD_CHECK_SWITCH)) {
			if (primary_display_is_video_mode()) {
				/* try eint & read switch on vdo mode */
				if (_can_switch_check_mode())
					set_esd_check_mode(mode);
			}
		}

		mmprofile_log_ex(mmp_te, MMPROFILE_FLAG_END, 0, ret);

		goto done;
	}

	/*  Esd Check : Read from lcm */
	mmprofile_log_ex(mmp_rd, MMPROFILE_FLAG_START,
			 0, primary_display_cmdq_enabled());

	if (primary_display_cmdq_enabled() == 0) {
		DDPPR_ERR("[ESD]not support cpu read do esd check\n");
		mmprofile_log_ex(mmp_rd, MMPROFILE_FLAG_END, 0, ret);
		goto done;
	}

	mmprofile_log_ex(mmp_rd, MMPROFILE_FLAG_PULSE,
			 0, primary_display_is_video_mode());

	/* only cmd mode read & with disable mmsys clk will kick */
	if (disp_helper_get_option(DISP_OPT_IDLEMGR_ENTER_ULPS) &&
	    !primary_display_is_video_mode())
		primary_display_idlemgr_kick((char *)__func__, 1);

	ret = do_esd_check_read();

	mmprofile_log_ex(mmp_rd, MMPROFILE_FLAG_END, 0, ret);

done:
	DISPINFO("[ESD]ESD check end, ret = %d\n", ret);
	mmprofile_log_ex(mmp_chk, MMPROFILE_FLAG_END, 0, ret);
	dprec_logger_done(DPREC_LOGGER_ESD_CHECK, 0, 0);
	return ret;
}

#ifdef VENDOR_EDIT
void display_esd_check_enable_bytouchpanel(bool enable)
{
	if (enable) {
		atomic_set(&esd_check_fw_updated, 1);
	} else {
		atomic_set(&esd_check_fw_updated, 0);
	}
}
EXPORT_SYMBOL(display_esd_check_enable_bytouchpanel);
/*Yongpeng.Yi@PSW.MM.Display.LCD.Stability, 2018/07/04, add for hx83112a lcd esd read reg*/
int display_esd_recovery_lcm(void)
{
	if (atomic_read(&enable_lcm_recovery)) {
		return 1;
	} else {
		return 0;
	}
}
EXPORT_SYMBOL(display_esd_recovery_lcm);
#endif /*VENDOR_EDIT*/
static int primary_display_check_recovery_worker_kthread(void *data)
{
	struct sched_param param = {.sched_priority = 87 };
	int ret = 0;
	int i = 0;
	int esd_try_cnt = 5; /* 20; */
	int recovery_done = 0;

	DISPFUNC();
	sched_setscheduler(current, SCHED_RR, &param);

#ifdef VENDOR_EDIT
/* YongPeng.Yi@PSW.MM.Display.LCD.Stability, 2019/01/26, add for esd test */
	if (is_project(18531) || is_project(18561) || is_project(18161) ) {
		esd_try_cnt = 9;
	}
#endif /*VENDOR_EDIT*/

	while (1) {
#ifndef VENDOR_EDIT
		/*
		* Yongpeng.Yi@PSW.MM.Display.LCD.Stability, 2018/01/25,
		* add for esd recovery
		*/
		msleep(2000); /* 2s */
		#else
		msleep(5000);
		#endif /*VENDOR_EDIT*/
#ifndef VENDOR_EDIT
		/*Cong.Dai@PSW.BSP.TP, 2017/11/18, add for stop lcd esd check until firmware update*/
		ret = wait_event_interruptible(_check_task_wq, atomic_read(&_check_task_wakeup));
#else
		ret = wait_event_interruptible(_check_task_wq, atomic_read(&_check_task_wakeup) && atomic_read(&esd_check_fw_updated));
#endif /*VENDOR_EDIT*/

		if (ret < 0) {
			DISPINFO("[disp_check]check thread waked up accidently\n");
			continue;
		}

		_primary_path_switch_dst_lock();

		/* 1. esd check & recovery */
		if (esd_check_enable) {
			i = 0; /* repeat */
			do {
				ret = primary_display_esd_check();
				if (!ret) /* success */
					break;
				#ifdef VENDOR_EDIT
				/* Yongpeng.Yi@PSW.MM.Display.LCD.Stability, 2018/07/04, add for hx83112a lcd esd read reg*/
				if (is_project(18311) || is_project(18011)
					|| is_project(18531) || is_project(18561)
					|| is_project(18161)) {
					DDPPR_ERR("[ESD] name:%s\n", primary_get_lcm()->drv->name);
					if(!strcmp(primary_get_lcm()->drv->name,"oppo18311_dsjm_himax83112a_1080p_dsi_vdo")
						|| !strcmp(primary_get_lcm()->drv->name,"oppo18311_truly_td4320_1080p_dsi_vdo")
						|| !strcmp(primary_get_lcm()->drv->name,"oppo18561_dsjm_jdi_himax83112a_1080p_dsi_vdo")
						|| !strcmp(primary_get_lcm()->drv->name,"oppo18561_tianma_himax83112a_1080p_dsi_vdo"))
					atomic_set(&enable_lcm_recovery, 1);
				}
				#endif /*VENDOR_EDIT*/

				DDPPR_ERR("[ESD]esd check fail, will do esd recovery. try=%d\n", i);
				primary_display_esd_recovery();
				#ifdef VENDOR_EDIT
				/*
				* Yongpeng.Yi@PSW.MM.Display.LCD.Stability, 2018/01/12,
				* add for esd recovery
				*/
				msleep(2000);
				#endif /*VENDOR_EDIT*/
				recovery_done = 1;
			} while (++i < esd_try_cnt);

			if (ret == 1) {
				DDPPR_ERR("[ESD]after esd recovery %d times, still fail, disable esd check\n",
					esd_try_cnt);
				primary_display_esd_check_enable(0);
				#ifdef VENDOR_EDIT
				/* Yongpeng.Yi@PSW.MM.Display.LCD.Stability, 2018/07/04, add for hx83112a lcd esd read reg*/
				atomic_set(&enable_lcm_recovery, 0);
				#endif /*VENDOR_EDIT*/
			} else if (recovery_done == 1) {
				DISPCHECK("[ESD]esd recovery success\n");
				#ifdef VENDOR_EDIT
				/* Yongpeng.Yi@PSW.MM.Display.LCD.Stability, 2018/07/04, add for hx83112a lcd esd read reg*/
				atomic_set(&enable_lcm_recovery, 0);
				#endif /*VENDOR_EDIT*/
				recovery_done = 0;
			}
		}

		_primary_path_switch_dst_unlock();

		/* 2. other check & recovery */

		if (kthread_should_stop())
			break;
	}
	return 0;
}

#ifdef VENDOR_EDIT
/* zhongwenjie@PSW.BSP.TP.Function, 2018/07/05, Add for tp fw download after esd recovery */
extern void lcd_queue_load_tp_fw(void);
extern bool flag_lcd_off;
extern int _set_backlight_by_cmdq(unsigned int level);

#endif /*VENDOR_EDIT*/

/* ESD RECOVERY */
int primary_display_esd_recovery(void)
{
	enum DISP_STATUS ret = DISP_STATUS_OK;
	LCM_PARAMS *lcm_param = NULL;
	mmp_event mmp_r = ddp_mmp_get_events()->esd_recovery_t;

	DISPFUNC();
	dprec_logger_start(DPREC_LOGGER_ESD_RECOVERY, 0, 0);
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_START, 0, 0);
	DISPCHECK("[ESD]ESD recovery begin\n");

	primary_display_manual_lock();
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE,
		       primary_display_is_video_mode(), 1);

	lcm_param = disp_lcm_get_params(primary_get_lcm());
	if (primary_get_state() == DISP_SLEPT) {
		DISPCHECK("[ESD]esd recovery but primary display path is slept??\n");
		goto done;
	}

	primary_display_idlemgr_kick((char *)__func__, 0);
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 2);

	/* blocking flush before stop trigger loop */
	_blocking_flush();

	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 3);

	DISPINFO("[ESD]display cmdq trigger loop stop[begin]\n");
	_cmdq_stop_trigger_loop();
	DISPINFO("[ESD]display cmdq trigger loop stop[end]\n");

	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 4);

	DISPDBG("[ESD]stop dpmgr path[begin]\n");
	dpmgr_path_stop(primary_get_dpmgr_handle(), CMDQ_DISABLE);
	DISPCHECK("[ESD]stop dpmgr path[end]\n");
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 0xff);

	if (dpmgr_path_is_busy(primary_get_dpmgr_handle())) {
		DISPCHECK("[ESD]primary display path is busy after stop\n");
		dpmgr_wait_event_timeout(primary_get_dpmgr_handle(), DISP_PATH_EVENT_FRAME_DONE, HZ * 1);
		DISPCHECK("[ESD]wait frame done ret:%d\n", ret);
	}
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 5);

	DISPDBG("[ESD]reset display path[begin]\n");
	dpmgr_path_reset(primary_get_dpmgr_handle(), CMDQ_DISABLE);
	DISPCHECK("[ESD]reset display path[end]\n");

	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 6);

	DISPDBG("[POWER]lcm suspend[begin]\n");
	/*after dsi_stop, we should enable the dsi basic irq.*/
	dsi_basic_irq_enable(DISP_MODULE_DSI0, NULL);
	disp_lcm_suspend(primary_get_lcm());
	#ifdef VENDOR_EDIT
	/*
	* Yongpeng.Yi@PSW.MM.Display.LCD.Stability, 2018/01/05,
	* add power seq api for ulps
	*/
	disp_lcm_poweroff_after_ulps(primary_get_lcm());
	#endif /*VENDOR_EDIT*/
	DISPCHECK("[POWER]lcm suspend[end]\n");

	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 7);

	DISPDBG("[ESD]dsi power reset[begin]\n");
	dpmgr_path_dsi_power_off(primary_get_dpmgr_handle(), NULL);
	dpmgr_path_dsi_power_on(primary_get_dpmgr_handle(), NULL);
	if (!primary_display_is_video_mode())
		dpmgr_path_ioctl(primary_get_dpmgr_handle(), NULL,
				DDP_DSI_ENABLE_TE, NULL);
	DISPCHECK("[ESD]dsi power reset[end]\n");



	DISPDBG("[ESD]lcm force init[begin]\n");
	#ifdef VENDOR_EDIT
	/*
	* Yongpeng.Yi@PSW.MM.Display.LCD.Stability, 2018/01/05,
	* add power seq api for ulps
	*/
	msleep(80);
	disp_lcm_poweron_before_ulps(primary_get_lcm());
	#endif /*VENDOR_EDIT*/
	disp_lcm_init(primary_get_lcm(), 1);
	DISPCHECK("[ESD]lcm force init[end]\n");
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 8);

	DISPDBG("[ESD]start dpmgr path[begin]\n");
	if (disp_partial_is_support()) {
		struct disp_ddp_path_config *data_config =
			dpmgr_path_get_last_config(primary_get_dpmgr_handle());

		primary_display_config_full_roi(data_config, primary_get_dpmgr_handle(), NULL);
	}
	dpmgr_path_start(primary_get_dpmgr_handle(), CMDQ_DISABLE);
	DISPCHECK("[ESD]start dpmgr path[end]\n");

	if (dpmgr_path_is_busy(primary_get_dpmgr_handle())) {
		DDPPR_ERR("[ESD]Fatal error, we didn't trigger display path but it's already busy\n");
		ret = -1;
		/* goto done; */
	}

	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 9);
	DISPDBG("[ESD]start cmdq trigger loop[begin]\n");
	_cmdq_start_trigger_loop();
	DISPCHECK("[ESD]start cmdq trigger loop[end]\n");
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 10);
	if (primary_display_is_video_mode()) {
		/*
		 * for video mode, we need to force trigger here
		 * for cmd mode, just set DPREC_EVENT_CMDQ_SET_EVENT_ALLOW
		 * when trigger loop start
		 */
		dpmgr_path_trigger(primary_get_dpmgr_handle(), NULL, CMDQ_DISABLE);

	}
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_PULSE, 0, 11);

	/*
	 * (in suspend) when we stop trigger loop
	 * if no other thread is running, cmdq may disable its clock
	 * all cmdq event will be cleared after suspend
	 */
	cmdqCoreSetEvent(CMDQ_EVENT_DISP_WDMA0_EOF);

	/* set dirty to trigger one frame -- cmd mode */
	if (!primary_display_is_video_mode()) {
		cmdqCoreSetEvent(CMDQ_SYNC_TOKEN_CONFIG_DIRTY);
		mdelay(40);
	}

done:
	#ifdef VENDOR_EDIT
	/*
	* ZhongWenjie@PSW.BSP.TP.FUNCTION, 2019/02/26,
	* add to trigger tp irq reset for esd recovery
	*/
	lcd_trigger_tp_irq_reset();
	#endif
	primary_display_manual_unlock();
	#ifdef VENDOR_EDIT
	/*
	* Yongpeng.Yi@PSW.MM.Display.LCD.Stability, 2018/07/21,
	* add for 17197 esd recovery
	*/
	if (is_project(18311) || is_project(18011) || is_project(18611)) {
		/* zhongwenjie@PSW.BSP.TP.Function, 2018/07/05, Add for tp fw download after esd recovery */
		lcd_queue_load_tp_fw();
	}

		/* zhongwenjie@PSW.BSP.TP.Function, 2018/07/05, Add for tp fw download after esd recovery */
	if ((!strcmp(primary_get_lcm()->drv->name,"oppo18561_dsjm_jdi_himax83112a_1080p_dsi_vdo"))
	|| (!strcmp(primary_get_lcm()->drv->name,"oppo18561_tianma_himax83112a_1080p_dsi_vdo"))) {
		lcd_queue_load_tp_fw();
	}

	if (is_project(17197) || is_project(17199) \
		|| is_project(19391) || is_project(19531)) {
		disp_lcm_set_backlight(primary_get_lcm(),NULL,esd_recovery_backlight_level);
	} else if (is_project(18531) || is_project(18561)
		|| is_project(18161) || is_project(18311)
		|| is_project(18011) || is_project(17331)
		|| is_project(17175)) {
		disp_lcm_set_backlight(primary_get_lcm(),primary_get_dpmgr_handle(),esd_recovery_backlight_level);
	}

	if (is_project(19151) || is_project(19350)) {
		if (primary_get_state() != DISP_SLEPT) {
			_set_backlight_by_cmdq(esd_recovery_backlight_level);
		} else {
			DISPCHECK("[ESD] lcd is off!\n");
		}
	}

	flag_lcd_off = false;
	#endif /* VENDOR_EDIT */
	DISPCHECK("[ESD]ESD recovery end\n");
	mmprofile_log_ex(mmp_r, MMPROFILE_FLAG_END, 0, 0);
	dprec_logger_done(DPREC_LOGGER_ESD_RECOVERY, 0, 0);
	return ret;
}

int primary_display_recovery(enum DISP_MODULE_ENUM module)
{
	enum DISP_STATUS ret = DISP_STATUS_OK;
	struct disp_ddp_path_config *pconfig;

	DISPFUNC();
	DISPCHECK("[disp path recovery]begin\n");

	primary_display_manual_lock();

	if (primary_get_state() == DISP_SLEPT) {
		DISPCHECK("disp path recovery but primary display path is slept??\n");
		goto done;
	}

	primary_display_idlemgr_kick((char *)__func__, 0);

	DISPDBG("[disp path recovery]cmdq trigger loop stop[begin]\n");
	_cmdq_stop_trigger_loop();
	DISPCHECK("[disp path recovery]cmdq trigger loop stop[end]\n");

	DISPDBG("[disp path recovery]stop dpmgr path[begin]\n");
	dpmgr_path_stop(primary_get_dpmgr_handle(), CMDQ_DISABLE);
	DISPCHECK("[disp path recovery]stop dpmgr path[end]\n");

	if (dpmgr_path_is_busy(primary_get_dpmgr_handle()))
		DISPERR("[disp path recovery]display path is busy after stop\n");

	DISPDBG("[disp path recovery]reset display path[begin]\n");
	ddp_path_mmsys_sw_reset(module);
	dpmgr_path_reset(primary_get_dpmgr_handle(), CMDQ_DISABLE);
	DISPCHECK("[disp path recovery]reset display path[end]\n");
	dsi_basic_irq_enable(DISP_MODULE_DSI0, NULL);

	pconfig = dpmgr_path_get_last_config(pgc->dpmgr_handle);
	pconfig->rdma_dirty = 1;
	if (module == DISP_MODULE_OVL0 || module == DISP_MODULE_OVL0_2L) {
		pconfig->ovl_dirty = 1;
		pconfig->dst_dirty = 1;
	}
	ret = dpmgr_path_config(pgc->dpmgr_handle, pconfig, NULL);

	DISPDBG("[disp path recovery]start dpmgr path[begin]\n");
	if (disp_partial_is_support()) {
		struct disp_ddp_path_config *data_config =
			dpmgr_path_get_last_config(primary_get_dpmgr_handle());

		primary_display_config_full_roi(data_config, primary_get_dpmgr_handle(), NULL);
	}
	dpmgr_path_start(primary_get_dpmgr_handle(), CMDQ_DISABLE);
	DISPCHECK("[disp path recovery]start dpmgr path[end]\n");

	if (dpmgr_path_is_busy(primary_get_dpmgr_handle())) {
		DISPERR("[ESD]Fatal error, we didn't trigger display path but it's already busy\n");
		ret = -1;
		/* goto done; */
	}

	DISPDBG("[disp path recovery]start cmdq trigger loop[begin]\n");
	_cmdq_start_trigger_loop();
	DISPCHECK("[disp path recovery]start cmdq trigger loop[end]\n");
	if (primary_display_is_video_mode()) {
		/*
		 * for video mode, we need to force trigger here
		 * for cmd mode, just set DPREC_EVENT_CMDQ_SET_EVENT_ALLOW
		 * when trigger loop start
		 */
		dpmgr_path_trigger(primary_get_dpmgr_handle(), NULL, CMDQ_DISABLE);

	}

	/*
	 * (in suspend) when we stop trigger loop
	 * if no other thread is running, cmdq may disable its clock
	 * all cmdq event will be cleared after suspend
	 */
	cmdqCoreSetEvent(CMDQ_EVENT_DISP_WDMA0_EOF);

	/* set dirty to trigger one frame -- cmd mode */
	if (!primary_display_is_video_mode()) {
		cmdqCoreSetEvent(CMDQ_SYNC_TOKEN_CONFIG_DIRTY);
		mdelay(40);
	}

done:
	primary_display_manual_unlock();
	DISPCHECK("[disp path recovery] end\n");
	return ret;
}

int primary_display_wdma_recovery(void)
{
	enum DISP_STATUS ret = DISP_STATUS_OK;
	struct disp_ddp_path_config *pconfig;
	bool wdma_in_main_disp = true;

	DISPFUNC();
	DISPCHECK("[disp wdma recovery]begin\n");

	/* Lock main display frame update and DISP PATH */
	primary_display_manual_lock();

	if (!primary_get_ovl2mem_handle() && disp_mgr_has_mem_session())
		wdma_in_main_disp = false;

	if (wdma_in_main_disp) {
		DISPCHECK("[disp wdma recovery]wdma in main display\n");
		if (primary_get_state() == DISP_SLEPT) {
			DISPCHECK("disp wdma recovery but primary display path is slept??\n");
			goto done;
		}

		if (!primary_get_ovl2mem_handle()) {
			DISPCHECK("ovl2mem_handle is NULL, cancel wdma recovery\n");
			goto done;
		}

		pconfig = dpmgr_path_get_last_config(primary_get_ovl2mem_handle());
	} else {
		DISPCHECK("[disp wdma recovery]wdma in virtual display\n");
		/* Lock memory out path frame update. */
		ovl2mem_path_lock(__func__);
		if (!mtk_ovl_get_dpmgr_handle()) {
			DISPCHECK("mtk_ovl_handle is NULL, cancel wdma recovery\n");
			goto done;
		}
		pconfig = dpmgr_path_get_last_config(mtk_ovl_get_dpmgr_handle());
	}
	/* Since we expect the ovl->wdma is hang if run to this path,
	 * so do the reset directly without wait idle.
	 */
	pconfig->wdma_dirty = 1;
	DISPDBG("[disp wdma recovery]reset wdma engine[begin]\n");
	ddp_get_module_driver(DISP_MODULE_WDMA0)->reset(DISP_MODULE_WDMA0, NULL);
	ddp_path_mmsys_sw_reset(DISP_MODULE_WDMA0);
	DISPCHECK("[disp wdma recovery]reset wdma engine[end]\n");

	DISPDBG("[disp wdma recovery]config wdma engine[begin]\n");
	ddp_get_module_driver(DISP_MODULE_WDMA0)->config(DISP_MODULE_WDMA0,
		pconfig, NULL);
	DISPCHECK("[disp wdma recovery]config wdma engine[end]\n");

	DISPDBG("[disp wdma recovery]start wdma engine[begin]\n");
	ddp_get_module_driver(DISP_MODULE_WDMA0)->start(DISP_MODULE_WDMA0, NULL);
	DISPCHECK("[disp wdma recovery]start dpmgr engine[end]\n");

done:
	if (!wdma_in_main_disp)
		ovl2mem_path_unlock(__func__);
	primary_display_manual_unlock();
	DISPCHECK("[disp wdma recovery] end\n");
	return ret;
}

void primary_display_set_recovery_module(enum DISP_MODULE_ENUM module)
{
	switch (module) {
	case DISP_MODULE_WDMA0:
		atomic_set(&enable_wdma_recovery, 1);
		break;
	case DISP_MODULE_RDMA0:
		atomic_set(&enable_rdma_recovery, 1);
		break;
	case DISP_MODULE_OVL0:
		atomic_set(&enable_ovl0_recovery, 1);
		break;
	case DISP_MODULE_OVL0_2L:
		atomic_set(&enable_ovl0_2l_recovery, 1);
		break;
	default:
		break;
	}
}

static int primary_display_recovery_kthread(void *data)
{
	dpmgr_enable_event(primary_get_dpmgr_handle(), DISP_PATH_EVENT_DISP_RECOVERY);
	while (1) {
		dpmgr_wait_event(primary_get_dpmgr_handle(), DISP_PATH_EVENT_DISP_RECOVERY);

		if (ddp_path_check_smi_busy())
			continue;
		else
			DISPMSG("SMI idle, check display engine status\n");

		if (atomic_read(&enable_wdma_recovery)) {
			if (ddp_path_need_mmsys_sw_reset(DISP_MODULE_WDMA0)) {
				DISPERR("Detect wdma0 malfunction, do recovery.\n");
				primary_display_wdma_recovery();
				atomic_set(&enable_wdma_recovery, 0);
			}
		}

		if (atomic_read(&enable_rdma_recovery)) {
			if (ddp_path_need_mmsys_sw_reset(DISP_MODULE_RDMA0)) {
				DISPERR("Detect rdma0 malfunction, do recovery.\n");
				primary_display_recovery(DISP_MODULE_RDMA0);
				atomic_set(&enable_rdma_recovery, 0);
			}
		}

		if (atomic_read(&enable_ovl0_recovery)) {
			if (ddp_path_need_mmsys_sw_reset(DISP_MODULE_OVL0)) {
				DISPERR("Detect ovl0 malfunction, do recovery.\n");
				primary_display_recovery(DISP_MODULE_OVL0);
				atomic_set(&enable_ovl0_recovery, 0);
			}
		}

		if (atomic_read(&enable_ovl0_2l_recovery)) {
			if (ddp_path_need_mmsys_sw_reset(DISP_MODULE_OVL0_2L)) {
				DISPERR("Detect ovl0_2l malfunction, do recovery.\n");
				primary_display_recovery(DISP_MODULE_OVL0_2L);
				atomic_set(&enable_ovl0_2l_recovery, 0);
			}
		}

		if (kthread_should_stop())
			break;
	}

	return 0;
}

void primary_display_check_recovery_init(void)
{
	/* primary display check thread init */
	primary_display_check_task =
		kthread_create(primary_display_check_recovery_worker_kthread,
			       NULL, "disp_check");
	init_waitqueue_head(&_check_task_wq);

	if (disp_helper_get_option(DISP_OPT_ESD_CHECK_RECOVERY)) {
		wake_up_process(primary_display_check_task);
		if (_need_do_esd_check()) {
			/* esd check init */
			init_waitqueue_head(&esd_ext_te_wq);
			set_esd_check_mode(GPIO_EINT_MODE);
			primary_display_esd_check_enable(1);
		} else {
			atomic_set(&_check_task_wakeup, 1);
			wake_up_interruptible(&_check_task_wq);
		}
	}

	if (disp_helper_get_stage() == DISP_HELPER_STAGE_NORMAL) {
		primary_recovery_thread =
			kthread_create(primary_display_recovery_kthread, NULL, "disp_path_recovery");
		wake_up_process(primary_recovery_thread);
	}

}

void primary_display_esd_check_enable(int enable)
{
	if (_need_do_esd_check()) {
		if (enable) {
			esd_check_enable = 1;
			DISPCHECK("[ESD]enable esd check\n");
			atomic_set(&_check_task_wakeup, 1);
			wake_up_interruptible(&_check_task_wq);
		} else {
			esd_check_enable = 0;
			atomic_set(&_check_task_wakeup, 0);
			DISPCHECK("[ESD]disable esd check\n");
		}
	} else {
		DISPCHECK("[ESD]do not support esd check\n");
	}
}

unsigned int need_wait_esd_eof(void)
{
	int ret = 1;

	/*
	 * 1.esd check disable
	 * 2.vdo mode
	 * 3.cmd mode te
	 */
	if (_need_do_esd_check() == 0)
		ret = 0;

	if (primary_display_is_video_mode())
		ret = 0;

	if (primary_get_lcm()->params->dsi.customization_esd_check_enable == 0)
		ret = 0;

	return ret;
}

#if (CONFIG_MTK_DUAL_DISPLAY_SUPPORT == 2)
/*********external display dual LCM feature************
**********esd check*******************************
*/
static unsigned int extd_need_do_esd_check(void)
{
	int ret = 0;
	struct disp_lcm_handle *plcm = NULL;

	extd_disp_get_interface((struct disp_lcm_handle **)&plcm);

	if (plcm && plcm->params->dsi.esd_check_enable == 1)
		ret = 1;

	return ret;
}

/* For external display EXT TE EINT Check */
static irqreturn_t extd_esd_check_ext_te_irq_handler(int irq, void *data)
{
	mmprofile_log_ex(ddp_mmp_get_events()->esd_vdo_eint, MMPROFILE_FLAG_PULSE, 0, 0);
	atomic_set(&esd_ext_te_1_event, 1);
	wake_up_interruptible(&esd_ext_te_1_wq);
	return IRQ_HANDLED;
}

static int extd_esd_check_eint(void)
{
	int ret = 0;

	if (wait_event_interruptible_timeout(esd_ext_te_1_wq, atomic_read(&esd_ext_te_1_event), HZ / 2) > 0)
		ret = 0;	/* esd check pass */
	else
		ret = 1;	/* esd check fail */

	atomic_set(&esd_ext_te_1_event, 0);

	return ret;
}

static unsigned int get_extd_esd_check_mode(void)
{
	return extd_esd_check_mode;
}

static void set_extd_esd_check_mode(unsigned int mode)
{
	extd_esd_check_mode = mode;
}

void external_display_esd_check_enable(int enable)
{
	if (extd_need_do_esd_check()) {

		if (enable) {
			extd_esd_check_enable = 1;
			DISPCHECK("[EXTD-ESD]enable esd check\n");
			atomic_set(&extd_check_task_wakeup, 1);
			wake_up_interruptible(&extd_check_task_wq);
		} else {
			extd_esd_check_enable = 0;
			atomic_set(&extd_check_task_wakeup, 0);
			DISPCHECK("[EXTD-ESD]disable esd check\n");

		}
	} else
		DISPCHECK("[EXTD-ESD]do not support esd check\n");

}

int external_display_switch_esd_mode(int mode)
{
	int ret = 0;
	struct device_node *node;
	int irq;
	u32 ints[2] = { 0, 0 };

	node = of_find_compatible_node(NULL, NULL, "mediatek, dsi_te_1-eint");
	if (node == NULL) {
		DDPPR_ERR("[EXTD-ESD][%s] can't find DSI_TE eint compatible node\n", __func__);
		return ret;
	}

	if (mode == GPIO_EINT_MODE) {
		/* register irq handler */
		of_property_read_u32_array(node, "debounce", ints, ARRAY_SIZE(ints));
		/* mt_gpio_set_debounce(ints[0], ints[1]); */
		irq = irq_of_parse_and_map(node, 0);
		if (request_irq(irq, extd_esd_check_ext_te_irq_handler, IRQF_TRIGGER_RISING, "dsi_te_1-eint", NULL))
			DDPPR_ERR("[EXTD-ESD]EINT IRQ LINE NOT AVAILABLE!!\n");
	} else if (mode == GPIO_DSI_MODE) {
		/* 1. unregister irq handler */
		irq = irq_of_parse_and_map(node, 0);
		free_irq(irq, NULL);
		/*disp_dts_gpio_select_state(DTS_GPIO_STATE_TE_MODE_TE);*/
	}

	return ret;
}

/* ESD CHECK FUNCTION */
/* return 1: esd check fail */
/* return 0: esd check pass */
int external_display_esd_check(void)
{
	int ret = 0;
	unsigned int mode;
	struct disp_lcm_handle *plcm = NULL;

	dprec_logger_start(DPREC_LOGGER_ESD_CHECK, 0, 0);
	mmprofile_log_ex(ddp_mmp_get_events()->esd_check_t, MMPROFILE_FLAG_START, 0, 0);
	DISPCHECK("[EXTD-ESD]ESD check begin\n");

	if (ext_disp_is_alive() != EXTD_RESUME) {
		mmprofile_log_ex(ddp_mmp_get_events()->esd_check_t, MMPROFILE_FLAG_PULSE, 1, 0);
		DISPCHECK("[EXTD-ESD]exernal display path is slept?? -- skip esd check\n");
		goto done;
	}

	/*  Esd Check : EXT TE */
	extd_disp_get_interface((struct disp_lcm_handle **)&plcm);
	if (plcm && plcm->params->dsi.customization_esd_check_enable == 0) {
		/* use te for esd check */
		mmprofile_log_ex(ddp_mmp_get_events()->esd_extte, MMPROFILE_FLAG_START, 0, 0);

		mode = get_extd_esd_check_mode();
		if (mode == GPIO_EINT_MODE) {
			DISPCHECK("[EXTD-ESD]ESD check eint\n");
			mmprofile_log_ex(ddp_mmp_get_events()->esd_extte, MMPROFILE_FLAG_PULSE,
				ext_disp_is_video_mode(), mode);
			external_display_switch_esd_mode(mode);
			DISPCHECK("[EXTD-ESD]ESD check begin ~\n");
			ret = extd_esd_check_eint();
			DISPCHECK("[EXTD-ESD]ESD check end, ret:%d\n", ret);
			mode = GPIO_DSI_MODE; /* used for mode switch */
			external_display_switch_esd_mode(mode);
		} else if (mode == GPIO_DSI_MODE) {
			mmprofile_log_ex(ddp_mmp_get_events()->esd_extte, MMPROFILE_FLAG_PULSE,
				ext_disp_is_video_mode(), mode);

			DISPCHECK("[EXTD-ESD]ESD check read\n");
			/*ret = do_esd_check_read();*/
			mode = GPIO_EINT_MODE; /* used for mode switch */
		}

		mmprofile_log_ex(ddp_mmp_get_events()->esd_extte, MMPROFILE_FLAG_END, 0, ret);
	}

done:
	DISPCHECK("[EXTD-ESD]ESD check end, ret = %d\n", ret);
	mmprofile_log_ex(ddp_mmp_get_events()->esd_check_t, MMPROFILE_FLAG_END, 0, ret);
	dprec_logger_done(DPREC_LOGGER_ESD_CHECK, 0, 0);
	return ret;

}

static int external_display_check_recovery_worker_kthread(void *data)
{
	struct sched_param param = {.sched_priority = 87 };
	int ret = 0;
	int i = 0;
	int esd_try_cnt = 5;	/* 20; */
	int recovery_done = 0;

	DISPFUNC();
	sched_setscheduler(current, SCHED_RR, &param);

#ifdef VENDOR_EDIT
/* YongPeng.Yi@PSW.MM.Display.LCD.Stability, 2019/01/26, add for esd test */
	if (is_project(18531) || is_project(18561) || is_project(18161) ) {
		esd_try_cnt = 9;
	}
#endif /*VENDOR_EDIT*/

	while (1) {
		#ifndef VENDOR_EDIT
		/*
		* Yongpeng.Yi@PSW.MM.Display.LCD.Stability, 2017/12/13,
		* modify for esd check 5s
		*/
		msleep(2000);/*2s*/
		#else
		msleep(5000);/*5s*/
		#endif
		ret = wait_event_interruptible(extd_check_task_wq, atomic_read(&extd_check_task_wakeup));
		if (ret < 0) {
			DISPCHECK("[ext_disp_check]check thread waked up accidently\n");
			continue;
		}
		pr_debug("[EXTD ext_disp_check]check thread waked up!\n");
		ext_disp_esd_check_lock();
		/* esd check and  recovery */
		if (extd_esd_check_enable) {
			i = 0;/*repeat*/
			do {
				ret = external_display_esd_check();
				if (ret == 1) {
					DDPPR_ERR("[EXTD-ESD]esd check fail, will do esd recovery. try=%d\n", i);
					ext_disp_esd_recovery();
					recovery_done = 1;
				} else
					break;

			} while (++i < esd_try_cnt);

			if (ret == 1) {
				DDPPR_ERR("[EXTD-ESD]after esd recovery %d times, still fail, disable esd check\n",
					esd_try_cnt);
				external_display_esd_check_enable(0);
			} else if (recovery_done == 1) {
				DISPCHECK("[EXTD-ESD]esd recovery success\n");
				recovery_done = 0;
			}
		}
		ext_disp_esd_check_unlock();

		if (kthread_should_stop())
			break;
	}

	return 0;
}

void external_display_check_recovery_init(void)
{
	/* primary display check thread init */
	if (external_display_check_task == NULL) {
		external_display_check_task =
			kthread_create(external_display_check_recovery_worker_kthread, NULL,
			"extd_esd_check");
		init_waitqueue_head(&extd_check_task_wq);
		wake_up_process(external_display_check_task);
	}

	if (disp_helper_get_option(DISP_OPT_ESD_CHECK_RECOVERY)
		&& extd_need_do_esd_check()) {
		/* esd check init */
		init_waitqueue_head(&esd_ext_te_1_wq);
		set_extd_esd_check_mode(GPIO_EINT_MODE);
		/*external_display_esd_check_enable(1);*/
	}

}
#endif
