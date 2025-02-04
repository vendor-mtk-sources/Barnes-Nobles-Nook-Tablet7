/*****************************************************************************/
/*****************************************************************************/
#if defined(CONFIG_MTK_HDMI_SUPPORT)
#define TMFL_TDA19989

#define _tx_c_
/* /#include <linux/autoconf.h> */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
/*#include <linux/earlysuspend.h> */
#include <linux/kthread.h>
#include <linux/rtpm_prio.h>
#include <linux/vmalloc.h>
#include "disp_assert_layer.h"
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/switch.h>
#include "mmprofile.h"

#include <asm/uaccess.h>
#include <asm/atomic.h>
/* #include <asm/mach-types.h> */
#include <asm/cacheflush.h>
#include <asm/io.h>
/*#include <mach/dma.h> */
/*#include "mach/irqs.h" */
#include <asm/tlbflush.h>
#include <asm/page.h>

#include "m4u.h"
#include <linux/types.h>
/*#include <mach/mt_reg_base.h>*/
#ifdef CONFIG_MTK_CLKMGR
#include <mach/mt_clkmgr.h>
#endif
#include <mt-plat/mt_boot.h>
/*#include "mach/eint.h" */
/*#include "mach/irqs.h" */
#include "mtkfb_info.h"
#include "mtkfb.h"

#include "ddp_info.h"
/* #include "ddp_rdma.h" */
#include "ddp_irq.h"
#include "ddp_mmp.h"
#include "ddp_dpi.h"

#include "external_display.h"
#include "disp_session.h"

#include "extd_platform.h"
#include "extd_hdmi_drv.h"
#include "extd_drv_log.h"
#include "extd_utils.h"
#include "extd_hdmi_types.h"

#ifdef CONFIG_MTK_SMARTBOOK_SUPPORT
#include <linux/sbsuspend.h>
#include "smartbook.h"
#endif


#ifdef MTK_EXT_DISP_SYNC_SUPPORT
#include "display_recorder.h"
#include "mtkfb_fence.h"
#endif

#ifdef I2C_DBG
#include "tmbslHdmiTx_types.h"
#include "tmbslTDA9989_local.h"
#endif

#ifdef CONFIG_MTK_INTERNAL_HDMI_SUPPORT
#include "internal_hdmi_drv.h"
#endif
/* ~~~~~~~~~~~~~~~~~~~~~~~the static variable~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#define HDMI_OVERLAY_CNT 4

/*the following declare is for debug*/
static int hdmi_log_on = 1;
static int hdmi_force_to_on;
static int hdmi_hwc_on = 1;
static int hdmi_bufferdump_on = 1;
static atomic_t hdmi_fake_in = ATOMIC_INIT(false);

/*the following declare is for factory mode*/
static bool factory_mode;


static bool otg_enable_status;
static bool hdmi_vsync_flag;
static wait_queue_head_t hdmi_vsync_wq;
static unsigned long hdmi_reschange = HDMI_VIDEO_RESOLUTION_NUM;

static bool HDMI_DEBUG_OUTPUT = FALSE;



static struct switch_dev hdmi_switch_data;
static struct switch_dev hdmires_switch_data;

static struct HDMI_DRIVER *hdmi_drv;
static _t_hdmi_context hdmi_context;
static _t_hdmi_context *p = &hdmi_context;

static unsigned int hdmi_layer_num;
static unsigned long ovl_config_address[HDMI_OVERLAY_CNT];
static unsigned int hdmi_resolution_param_table[][3] = {
	{720, 480, 60},
	{1280, 720, 60},
	{1920, 1080, 30},
	{1920, 1080, 60},
};

static DEFINE_SEMAPHORE(hdmi_update_mutex);
/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* ~~~~~~~~~~~~~~~~~~~~~~~the gloable variable~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#ifdef EXTD_DBG_USE_INNER_BUF
unsigned long hdmi_va, hdmi_mva_r;
#endif

struct HDMI_PARAMS _s_hdmi_params = { 0 };

struct HDMI_PARAMS *hdmi_params = &_s_hdmi_params;
disp_ddp_path_config extd_dpi_params;

struct task_struct *hdmi_rdma_config_task = NULL;
wait_queue_head_t hdmi_rdma_config_wq;
atomic_t hdmi_rdma_config_event = ATOMIC_INIT(0);

unsigned int g_hdmi_is_interlace = 0;
/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */

/* ~~~~~~~~~~~~~~~~~~~~~~~the definition~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */
#define IS_HDMI_ON()            (HDMI_POWER_STATE_ON == atomic_read(&p->state))
#define IS_HDMI_OFF()           (HDMI_POWER_STATE_OFF == atomic_read(&p->state))
#define IS_HDMI_STANDBY()       (HDMI_POWER_STATE_STANDBY == atomic_read(&p->state))

#define IS_HDMI_NOT_ON()        (HDMI_POWER_STATE_ON != atomic_read(&p->state))
#define IS_HDMI_NOT_OFF()       (HDMI_POWER_STATE_OFF != atomic_read(&p->state))
#define IS_HDMI_NOT_STANDBY()   (HDMI_POWER_STATE_STANDBY != atomic_read(&p->state))

#define SET_HDMI_ON()           atomic_set(&p->state, HDMI_POWER_STATE_ON)
#define SET_HDMI_OFF()          atomic_set(&p->state, HDMI_POWER_STATE_OFF)
#define SET_HDMI_STANDBY()      atomic_set(&p->state, HDMI_POWER_STATE_STANDBY)


#define IS_HDMI_FAKE_PLUG_IN()  (true == atomic_read(&hdmi_fake_in))
#define SET_HDMI_FAKE_PLUG_IN() (atomic_set(&hdmi_fake_in, true))
#define SET_HDMI_NOT_FAKE()     (atomic_set(&hdmi_fake_in, false))
/* ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ */


static void hdmi_state_reset(void);

/* --------------------------------------------------------------------------- */
/* Information Dump Routines */
/* --------------------------------------------------------------------------- */
void hdmi_log_enable(int enable)
{
	pr_debug("hdmi log %s\n", enable ? "enabled" : "disabled");
	hdmi_log_on = enable;
	hdmi_drv->log_enable(enable);
}

static int mhl_forcr_on_cnt;
void hdmi_force_on(int from_uart_drv)
{
	if ((hdmi_force_to_on > 0) && (from_uart_drv > 0))
		return;

	pr_debug("hdmi_force_on %d force on %d, cnt %d\n", from_uart_drv, hdmi_force_to_on,
	       mhl_forcr_on_cnt);
	mhl_forcr_on_cnt++;
	hdmi_force_to_on = 1;
#if 0
	/*No definiton of force_on in hdmi_drv */
	if (hdmi_drv && hdmi_drv->force_on)
		hdmi_drv->force_on(from_uart_drv);
#endif
}


void hdmi_mmp_enable(int enable)
{
	pr_debug("hdmi log %s\n", enable ? "enabled" : "disabled");
	hdmi_bufferdump_on = enable;
}

void hdmi_hwc_enable(int enable)
{
	pr_debug("hdmi log %s\n", enable ? "enabled" : "disabled");
	hdmi_hwc_on = enable;
}

/* <--for debug */
void hdmi_cable_fake_plug_in(void)
{
	SET_HDMI_FAKE_PLUG_IN();
	HDMI_LOG("[HDMIFake]Cable Plug In\n");

	if (p->is_force_disable == false) {
		if (IS_HDMI_STANDBY()) {
			hdmi_resume();
			/* /msleep(1000); */
			hdmi_reschange = HDMI_VIDEO_RESOLUTION_NUM;
			switch_set_state(&hdmi_switch_data, HDMI_STATE_ACTIVE);
		}
	}
}

void hdmi_cable_fake_plug_out(void)
{
	SET_HDMI_NOT_FAKE();
	HDMI_LOG("[HDMIFake]Disable\n");

	if (p->is_force_disable == false) {
		if (IS_HDMI_ON()) {
			if (hdmi_drv->get_state() != HDMI_STATE_ACTIVE) {
				hdmi_suspend();
				switch_set_state(&hdmi_switch_data, HDMI_STATE_NO_DEVICE);
				switch_set_state(&hdmires_switch_data, 0);
			}
		}
	}
}

int hdmi_allocate_hdmi_buffer(void)
{

	HDMI_FUNC();
#ifdef EXTD_DBG_USE_INNER_BUF

	/*RETIF(hdmi_va, 0);*/
	if ((hdmi_va)) {
		HDMI_LOG("return in %d\n", __LINE__);
		return 0;
	}


	hdmi_va = (unsigned long)vmalloc(hdmiBufferSize);
	if (((void *)hdmi_va) == NULL) {
		HDMI_LOG("vmalloc %d bytes fail!!!\n", hdmiBufferSize);
		return -1;
	}

	m4u_client_t *client = NULL;

	client = m4u_create_client();
	if (IS_ERR_OR_NULL(client))
		HDMI_LOG("create client fail!\n");

	ret =
	    m4u_alloc_mva(client, M4U_PORT_DISP_OVL1, hdmi_va, 0, hdmiBufferSize,
			  M4U_PROT_READ | M4U_PROT_WRITE, 0, &hdmi_mva_r);

	memcpy(hdmi_va, kara_1280x720, 2764800);
	HDMI_LOG("hdmi_va=%p, hdmi_mva_r=%p, size %d\n", hdmi_va, hdmi_mva_r, hdmiBufferSize);
#endif
	return 0;

}


int hdmi_free_hdmi_buffer(void)
{
	return 0;
}

/* -->for debug */

static void _hdmi_rdma_irq_handler(DISP_MODULE_ENUM module, unsigned int param)
{
	/*RET_VOID_IF_NOLOG(!is_hdmi_active());*/
	if (!is_hdmi_active())
		return;

	if (param & 0x2) {
		/* start */
		/* /HDMI_LOG("_hdmi_rdma_irq_handler(%d) irq %x\n",module,  param); */
		/* MMProfileLogEx(ddp_mmp_get_events()->Extd_IrqStatus, MMProfileFlagPulse, module, param); */

		atomic_set(&hdmi_rdma_config_event, 1);
		wake_up_interruptible(&hdmi_rdma_config_wq);


		if (hdmi_params->cabletype == MHL_SMB_CABLE) {
			/* rdma1 register updated */
			hdmi_vsync_flag = 1;
			wake_up_interruptible(&hdmi_vsync_wq);
		}
	}

}

/* extern int ddp_dpi_dump(DISP_MODULE_ENUM module, int level); */
static int hdmi_rdma_config_kthread(void *data)
{
	int layid = 0;
	unsigned int session_id = 0;
	int fence_idx = 0;
	bool ovl_reg_updated = false;
	unsigned long input_curr_addr[HDMI_OVERLAY_CNT];
	struct sched_param param = {.sched_priority = RTPM_PRIO_SCRN_UPDATE };

	sched_setscheduler(current, SCHED_RR, &param);

	for (;;) {
		wait_event_interruptible(hdmi_rdma_config_wq, atomic_read(&hdmi_rdma_config_event));
		atomic_set(&hdmi_rdma_config_event, 0);

		ovl_reg_updated = false;
		session_id = ext_disp_get_sess_id();
		fence_idx = -1;

		/* HDMI_LOG("hdmi_rdma_config_kthread wake up\n"); */
		if (session_id == 0)
			continue;

		if ((ext_disp_path_get_mode() == EXTD_RDMA_DPI_MODE) && (ovl1_remove != 1)
		    && (ovl1_remove != 2)) {
			extd_get_curr_addr(input_curr_addr, 0);
			fence_idx =
			    disp_sync_find_fence_idx_by_addr(session_id, 0, input_curr_addr[0]);
			mtkfb_release_fence(session_id, 0, fence_idx);
			/* ddp_dpi_dump(0,0); */
		} else {
			extd_get_curr_addr(input_curr_addr, 1);
			for (layid = 0; layid < HDMI_OVERLAY_CNT; layid++) {
				if (ovl_config_address[layid] != input_curr_addr[layid]) {
					ovl_config_address[layid] = input_curr_addr[layid];
					ovl_reg_updated = true;
				}

				if (input_curr_addr[layid] == get_dim_layer_mva_addr()) {
					fence_idx =
					    disp_sync_find_fence_idx_by_addr(session_id, layid, 0);
				} else {
					fence_idx =
					    disp_sync_find_fence_idx_by_addr(session_id, layid,
									     input_curr_addr
									     [layid]);
				}

				mtkfb_release_fence(session_id, layid, fence_idx);
			}

			if (ovl_reg_updated == false) {
				MMProfileLogEx(ddp_mmp_get_events()->Extd_trigger,
					       MMProfileFlagPulse, input_curr_addr[0],
					       input_curr_addr[1]);
			}
			MMProfileLogEx(ddp_mmp_get_events()->Extd_UsedBuff, MMProfileFlagPulse,
				       input_curr_addr[0], input_curr_addr[1]);
		}

		hdmi_video_config(p->output_video_resolution, HDMI_VIN_FORMAT_RGB888,
				  HDMI_VOUT_FORMAT_RGB888);

		if (kthread_should_stop())
			break;
	}

	return 0;
}

/* Allocate memory, set M4U, LCD, MDP, DPI */
/* LCD overlay to memory -> MDP resize and rotate to memory -> DPI read to HDMI */
/* Will only be used in ioctl(MTK_HDMI_AUDIO_VIDEO_ENABLE) */
static HDMI_STATUS hdmi_drv_init(void)
{
	HDMI_FUNC();

	p->hdmi_width = hdmi_resolution_param_table[hdmi_params->init_config.vformat][0];
	/* /hdmi_get_width(hdmi_params->init_config.vformat); */
	p->hdmi_height = hdmi_resolution_param_table[hdmi_params->init_config.vformat][1];
	/* /hdmi_get_height(hdmi_params->init_config.vformat); */
	p->bg_width = 0;
	p->bg_height = 0;

	p->output_video_resolution = hdmi_params->init_config.vformat;
	p->output_audio_format = hdmi_params->init_config.aformat;
	p->scaling_factor = hdmi_params->scaling_factor < 10 ? hdmi_params->scaling_factor : 10;

	p->is_clock_on = false;	/* <--Donglei */

	if (!hdmi_rdma_config_task) {
		/* disp_register_module_irq_callback(DISP_MODULE_RDMA2, _hdmi_rdma_irq_handler); K2 no RDMA2 */
		disp_register_module_irq_callback(DISP_MODULE_RDMA1, _hdmi_rdma_irq_handler);
		hdmi_rdma_config_task =
		    kthread_create(hdmi_rdma_config_kthread, NULL, "hdmi_rdma_config_kthread");
		wake_up_process(hdmi_rdma_config_task);
	}

	return HDMI_STATUS_OK;
}

/* Release memory */
/* Will only be used in ioctl(MTK_HDMI_AUDIO_VIDEO_ENABLE) */
static HDMI_STATUS hdmi_drv_deinit(void)
{
	HDMI_FUNC();
	hdmi_free_hdmi_buffer();

	return HDMI_STATUS_OK;
}

bool is_hdmi_active(void)
{
	return (IS_HDMI_ON() && p->is_clock_on);
}

int get_extd_fps_time(void)
{

	if (hdmi_reschange == HDMI_VIDEO_1920x1080p_30Hz)
		return 34000;
	else
		return 16700;
}

unsigned int hdmi_get_width(void)
{
	return p->hdmi_width;
}

unsigned int hdmi_get_height(void)
{
	return p->hdmi_height;
}

void hdmi_waitVsync(void)
{

	unsigned int session_id = ext_disp_get_sess_id();
	disp_session_sync_info *session_info = disp_get_session_sync_info_for_debug(session_id);

	if (session_info)
		dprec_start(&session_info->event_waitvsync, 0, 0);

	if (p->is_clock_on == false) {
		pr_debug("[hdmi]:hdmi has suspend, return directly\n");
		msleep(20);
		return;
	}

	hdmi_vsync_flag = 0;

	if (wait_event_interruptible_timeout(hdmi_vsync_wq, hdmi_vsync_flag, HZ / 10) == 0)
		pr_debug("[hdmi] Wait VSync timeout. early_suspend=%d\n", p->is_clock_on);

	if (session_info)
		dprec_done(&session_info->event_waitvsync, 1, 0);

}

int hdmi_get_support_info(void)
{
	int value = 0, temp = 0;

#ifdef USING_SCALE_ADJUSTMENT
	value |= HDMI_SCALE_ADJUSTMENT_SUPPORT;
#endif

#ifdef MHL_PHONE_GPIO_REUSAGE
	value |= HDMI_PHONE_GPIO_REUSAGE;
#endif

#ifdef MTK_AUDIO_MULTI_CHANNEL_SUPPORT
	temp = hdmi_drv->get_external_device_capablity();
#else
	temp = 0x2 << 3;
#endif

	value |= temp;

	HDMI_LOG("value is 0x%x\n", value);
	return value;
}


/* Configure video attribute */
int hdmi_video_config(HDMI_VIDEO_RESOLUTION vformat, enum HDMI_VIDEO_INPUT_FORMAT vin,
		      enum HDMI_VIDEO_OUTPUT_FORMAT vout)
{
	if (p->is_mhl_video_on == true)
		return 0;

	HDMI_LOG("hdmi_video_config video_on=%d\n", p->is_mhl_video_on);
	/* RETIF(IS_HDMI_NOT_ON(), 0); */

	/* hdmi_allocate_hdmi_buffer(); */
	p->is_mhl_video_on = true;

	if (IS_HDMI_FAKE_PLUG_IN())
		return 0;

	return hdmi_drv->video_config(vformat, vin, vout);
}

#if 0
/* Configure audio attribute, will be called by audio driver */
int hdmi_audio_config(int format)
{
	HDMI_AUDIO_FORMAT audio_format;
	int channel_count = format & 0x0F;
	int sampleRate = (format & 0x70) >> 4;
	int bitWidth = (format & 0x180) >> 7, sampleBit;

	HDMI_LOG("channel_count: %d, sampleRate: %d, bitWidth: %d\n", channel_count, sampleRate,
		 bitWidth);

	if (bitWidth == HDMI_MAX_BITWIDTH_16) {
		sampleBit = 16;
		HDMI_LOG("HDMI_MAX_BITWIDTH_16\n");
	} else if (bitWidth == HDMI_MAX_BITWIDTH_24) {
		sampleBit = 24;
		HDMI_LOG("HDMI_MAX_BITWIDTH_24\n");
	}

	if (channel_count == HDMI_MAX_CHANNEL_2 && sampleRate == HDMI_MAX_SAMPLERATE_44) {
		audio_format = HDMI_AUDIO_44K_2CH;
		HDMI_LOG("AUDIO_44K_2CH\n");
	} else if (channel_count == HDMI_MAX_CHANNEL_8 && sampleRate == HDMI_MAX_SAMPLERATE_32) {
		audio_format = HDMI_AUDIO_32K_8CH;
		HDMI_LOG("AUDIO_32K_8CH\n");
	} else if (channel_count == HDMI_MAX_CHANNEL_8 && sampleRate == HDMI_MAX_SAMPLERATE_44) {
		audio_format = HDMI_AUDIO_44K_8CH;
		HDMI_LOG("AUDIO_44K_8CH\n");
	} else if (channel_count == HDMI_MAX_CHANNEL_8 && sampleRate == HDMI_MAX_SAMPLERATE_48) {
		audio_format = HDMI_AUDIO_48K_8CH;
		HDMI_LOG("AUDIO_48K_8CH\n");
	} else if (channel_count == HDMI_MAX_CHANNEL_8 && sampleRate == HDMI_MAX_SAMPLERATE_96) {
		audio_format = HDMI_AUDIO_96K_8CH;
		HDMI_LOG("AUDIO_96K_8CH\n");
	}

	else if (channel_count == HDMI_MAX_CHANNEL_8 && sampleRate == HDMI_MAX_SAMPLERATE_192) {
		audio_format = HDMI_AUDIO_192K_8CH;
		HDMI_LOG("AUDIO_192K_8CH\n");
	} else {
		HDMI_LOG("audio format is not supported\n");
	}

	RETIF(!p->is_enabled, 0);
	hdmi_drv->audio_config(audio_format, sampleBit);

	return 0;
}
#endif

/*static*/ void hdmi_power_on(void)
{
	HDMI_FUNC();

	/*RET_VOID_IF(IS_HDMI_NOT_OFF());*/
	if (IS_HDMI_NOT_OFF())
		return;

	if (down_interruptible(&hdmi_update_mutex)) {
		pr_debug("[hdmi][HDMI] can't get semaphore in %s()\n", __func__);
		return;
	}

	SET_HDMI_STANDBY();
	hdmi_drv->power_on();
	up(&hdmi_update_mutex);

	if (p->is_force_disable == false) {
		if (IS_HDMI_FAKE_PLUG_IN()) {
			hdmi_resume();
			msleep(1000);
			switch_set_state(&hdmi_switch_data, HDMI_STATE_ACTIVE);
			hdmi_reschange = HDMI_VIDEO_RESOLUTION_NUM;
		} else {
			/* this is just a ugly workaround for some tv sets... */
			if (hdmi_drv->get_state() == HDMI_STATE_ACTIVE) {
				/* && (factory_mode == true)) */
				hdmi_drv->get_params(hdmi_params);
				hdmi_resume();
			}

			hdmi_state_reset();
		}
	}

}

/*static*/ void hdmi_power_off(void)
{
	HDMI_FUNC();
	/*RET_VOID_IF(IS_HDMI_OFF());*/
	if (IS_HDMI_OFF())
		return;

	if (down_interruptible(&hdmi_update_mutex)) {
		pr_debug("[hdmi][HDMI] can't get semaphore in %s()\n", __func__);
		return;
	}

	ext_disp_suspend();
	hdmi_drv->power_off();
	p->is_clock_on = false;
	SET_HDMI_OFF();
	up(&hdmi_update_mutex);

	switch_set_state(&hdmires_switch_data, 0);

}

/*static*/ void hdmi_suspend(void)
{
	int i = 0;
	int session_id = ext_disp_get_sess_id();

	HDMI_FUNC();
	/*RET_VOID_IF(IS_HDMI_NOT_ON());*/
	if (IS_HDMI_NOT_ON())
		return;

	if (hdmi_bufferdump_on > 0)
		MMProfileLogEx(ddp_mmp_get_events()->Extd_State, MMProfileFlagStart, Plugout, 0);

	if (down_interruptible(&hdmi_update_mutex)) {
		pr_debug("[hdmi][HDMI] can't get semaphore in %s()\n", __func__);
		return;
	}

	SET_HDMI_STANDBY();
	hdmi_drv->suspend();
	p->is_mhl_video_on = false;
	p->is_clock_on = false;

	ext_disp_suspend();

	for (i = 0; i < HDMI_OVERLAY_CNT; i++)
		mtkfb_release_layer_fence(session_id, i);
	up(&hdmi_update_mutex);

	if (hdmi_bufferdump_on > 0)
		MMProfileLogEx(ddp_mmp_get_events()->Extd_State, MMProfileFlagEnd, Plugout, 0);
}

/*static*/ void hdmi_resume(void)
{
	HDMI_LOG("p->state is %d,(0:off, 1:on, 2:standby)\n", atomic_read(&p->state));
	/*RET_VOID_IF(IS_HDMI_NOT_ON());*/
	if (IS_HDMI_NOT_STANDBY())
		return;

	if (hdmi_bufferdump_on > 0)
		MMProfileLogEx(ddp_mmp_get_events()->Extd_State, MMProfileFlagStart, Plugin, 0);

	if (down_interruptible(&hdmi_update_mutex)) {
		pr_debug("[hdmi][HDMI] can't get semaphore in %s()\n", __func__);
		return;
	}

	SET_HDMI_ON();
	p->is_clock_on = true;
	hdmi_drv->resume();
	up(&hdmi_update_mutex);

	if (hdmi_bufferdump_on > 0)
		MMProfileLogEx(ddp_mmp_get_events()->Extd_State, MMProfileFlagEnd, Plugin, 0);
}

static void hdmi_resolution_setting(int arg)
{
	HDMI_FUNC();
	switch (arg) {
	case HDMI_VIDEO_720x480p_60Hz:
		{
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
#endif
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_RISING;

			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 62;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 60;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 16;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 6;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 30;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 9;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 27027; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_720x480p_60Hz;

			p->bg_height = ((480 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((720 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 720 - p->bg_width;
			p->hdmi_height = 480 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_720x480p_60Hz;
			break;
		}

	case HDMI_VIDEO_720x576p_50Hz:
		{
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
#endif
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_RISING;

			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 64;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 68;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 12;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 39;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 5;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 27027; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_720x576p_50Hz;

			p->bg_height = ((576 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((720 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 720 - p->bg_width;
			p->hdmi_height = 576 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_720x576p_50Hz;
			break;
		}

	case HDMI_VIDEO_1280x720p_60Hz:
		{
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
#if defined(HDMI_TDA19989)
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
#endif
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;

#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif

			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 40;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 220;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 110;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 20;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 5;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 74250; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1280x720p_60Hz;

			p->bg_height = ((720 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1280 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1280 - p->bg_width;	/* 1280  1366 */
			p->hdmi_height = 720 - p->bg_height;	/* 720;  768 */

#ifdef CONFIG_MTK_SMARTBOOK_SUPPORT
			if (hdmi_params->cabletype == MHL_SMB_CABLE) {
				p->hdmi_width = 1366;
				p->hdmi_height = 768;
				p->bg_height = 0;
				p->bg_width = 0;
			}
#endif
			p->output_video_resolution = HDMI_VIDEO_1280x720p_60Hz;
			break;
		}

	case HDMI_VIDEO_1280x720p_50Hz:
		{
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
#if defined(HDMI_TDA19989)
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
#endif
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;

#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif

			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 40;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 220;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 440;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 20;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 5;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 74250; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1280x720p_50Hz;

			p->bg_height = ((720 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1280 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1280 - p->bg_width;	/* 1280  1366 */
			p->hdmi_height = 720 - p->bg_height;	/* 720;  768 */

#ifdef CONFIG_MTK_SMARTBOOK_SUPPORT
			if (hdmi_params->cabletype == MHL_SMB_CABLE) {
				p->hdmi_width = 1366;
				p->hdmi_height = 768;
				p->bg_height = 0;
				p->bg_width = 0;
			}
#endif
			p->output_video_resolution = HDMI_VIDEO_1280x720p_50Hz;
			break;
		}

	case HDMI_VIDEO_1920x1080p_30Hz:
		{
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif

			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 44;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 148;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 88;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 36;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 4;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 74250; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1920x1080p_30Hz;

			p->bg_height = ((1080 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1920 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1920 - p->bg_width;
			p->hdmi_height = 1080 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1920x1080p_30Hz;
			break;
		}

	case HDMI_VIDEO_1920x1080p_24Hz:
		{
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif

			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 44;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 148;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 638;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 36;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 4;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 74250; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1920x1080p_24Hz;

			p->bg_height = ((1080 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1920 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1920 - p->bg_width;
			p->hdmi_height = 1080 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1920x1080p_24Hz;
			break;
		}


	case HDMI_VIDEO_1920x1080p_25Hz:
		{
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif

			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 44;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 148;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 528;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 36;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 4;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 74250; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1920x1080p_25Hz;

			p->bg_height = ((1080 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1920 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1920 - p->bg_width;
			p->hdmi_height = 1080 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1920x1080p_25Hz;
			break;
		}


	case HDMI_VIDEO_1920x1080p_29Hz:
		{
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif

			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 44;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 148;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 88;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 36;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 4;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 74250; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1920x1080p_29Hz;

			p->bg_height = ((1080 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1920 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1920 - p->bg_width;
			p->hdmi_height = 1080 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1920x1080p_29Hz;
			break;
		}


	case HDMI_VIDEO_1920x1080p_23Hz:
		{
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif

			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 44;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 148;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 638;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 36;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 4;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 74250; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1920x1080p_23Hz;

			p->bg_height = ((1080 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1920 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1920 - p->bg_width;
			p->hdmi_height = 1080 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1920x1080p_23Hz;
			break;
		}

	case HDMI_VIDEO_1920x1080p_60Hz:
		{
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif
			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 44;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 148;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 88;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 36;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 4;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 148500; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1920x1080p_60Hz;

			p->bg_height = ((1080 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1920 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1920 - p->bg_width;
			p->hdmi_height = 1080 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1920x1080p_60Hz;
			break;
		}

	case HDMI_VIDEO_1920x1080p_50Hz:
		{
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif
			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 44;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 148;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 528;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 36;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 4;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 148500; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1920x1080p_50Hz;

			p->bg_height = ((1080 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1920 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1920 - p->bg_width;
			p->hdmi_height = 1080 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1920x1080p_50Hz;
			break;
		}

	case HDMI_VIDEO_1920x1080i_50Hz:
		{
			/* fgInterlace = TRUE; */
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif
			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 44;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 148;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 528;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 15;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 2;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 148500; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1920x1080i_50Hz;

			p->bg_height = ((1080 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1920 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1920 - p->bg_width;
			p->hdmi_height = 1080 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1920x1080i_50Hz;
			break;
		}

	case HDMI_VIDEO_1920x1080i_60Hz:
		{
			/* fgInterlace = TRUE; */
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif
			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 44;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 148;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 88;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 15;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 2;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 148500; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1920x1080i_60Hz;

			p->bg_height = ((1080 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1920 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1920 - p->bg_width;
			p->hdmi_height = 1080 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1920x1080i_60Hz;
			break;
		}

#if 0
	case HDMI_VIDEO_1920x1080i3d_60Hz:
		{
			/* fgInterlace = TRUE; */
			/* fg3DFrame = TRUE; */
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif
			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 44;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 148;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 88;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 15;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 2;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 148500; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1920x1080i3d_60Hz;

			p->bg_height = ((540 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1920 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1920 - p->bg_width;
			p->hdmi_height = 540 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1920x1080i3d_60Hz;
			break;
		}

	case HDMI_VIDEO_1920x1080i3d_50Hz:
		{
			/* fgInterlace = TRUE; */
			/* fg3DFrame = TRUE; */
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif
			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 168;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 184;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 32;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 27;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 23;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 148500; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1920x1080i3d_50Hz;

			p->bg_height = ((540 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1920 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1920 - p->bg_width;
			p->hdmi_height = 540 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1920x1080i3d_50Hz;
			break;
		}

	case HDMI_VIDEO_1280x720p3d_60Hz:
		{
			/* fg3DFrame = TRUE; */
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif
			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 40;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 220;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 110;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 20;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 5;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 148500; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1280x720p3d_60Hz;

			p->bg_height = ((720 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1280 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1280 - p->bg_width;
			p->hdmi_height = 720 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1280x720p3d_60Hz;
			break;
		}

	case HDMI_VIDEO_1280x720p3d_50Hz:
		{
			/* fg3DFrame = TRUE; */
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif
			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 40;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 220;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 440;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 20;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 5;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 148500; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1280x720p3d_50Hz;

			p->bg_height = ((720 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1280 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1280 - p->bg_width;
			p->hdmi_height = 720 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1280x720p3d_50Hz;
			break;
		}

	case HDMI_VIDEO_1920x1080p3d_23Hz:
		{
			/* fg3DFrame = TRUE; */
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif
			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 44;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 148;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 638;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 36;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 4;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 148500; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1920x1080p3d_23Hz;

			p->bg_height = ((1080 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1920 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1920 - p->bg_width;
			p->hdmi_height = 1080 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1920x1080p3d_23Hz;
			break;
		}

	case HDMI_VIDEO_1920x1080p3d_24Hz:
		{
			/* fg3DFrame = TRUE; */
#if defined(MHL_SII8338) || defined(MHL_SII8348)
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#else
			extd_dpi_params.dispif_config.dpi.clk_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.de_pol = HDMI_POLARITY_RISING;
			extd_dpi_params.dispif_config.dpi.hsync_pol = HDMI_POLARITY_FALLING;
			extd_dpi_params.dispif_config.dpi.vsync_pol = HDMI_POLARITY_FALLING;
#endif
			extd_dpi_params.dispif_config.dpi.hsync_pulse_width = 44;
			extd_dpi_params.dispif_config.dpi.hsync_back_porch = 148;
			extd_dpi_params.dispif_config.dpi.hsync_front_porch = 638;

			extd_dpi_params.dispif_config.dpi.vsync_pulse_width = 5;
			extd_dpi_params.dispif_config.dpi.vsync_back_porch = 36;
			extd_dpi_params.dispif_config.dpi.vsync_front_porch = 4;

			/* extd_dpi_params.dispif_config.dpi.dpi_clock = 148500; */
			extd_dpi_params.dispif_config.dpi.dpi_clock = DPI_VIDEO_1920x1080p3d_24Hz;

			p->bg_height = ((1080 * p->scaling_factor) / 100 >> 2) << 2;
			p->bg_width = ((1920 * p->scaling_factor) / 100 >> 2) << 2;
			p->hdmi_width = 1920 - p->bg_width;
			p->hdmi_height = 1080 - p->bg_height;
			p->output_video_resolution = HDMI_VIDEO_1920x1080p3d_24Hz;
			break;
		}
#endif

	default:
		break;
	}

	extd_dpi_params.dispif_config.dpi.width = p->hdmi_width;
	extd_dpi_params.dispif_config.dpi.height = p->hdmi_height;
	extd_dpi_params.dispif_config.dpi.bg_width = p->bg_width;
	extd_dpi_params.dispif_config.dpi.bg_height = p->bg_height;

	extd_dpi_params.dispif_config.dpi.format = LCM_DPI_FORMAT_RGB888;
	extd_dpi_params.dispif_config.dpi.rgb_order = LCM_COLOR_ORDER_RGB;
	extd_dpi_params.dispif_config.dpi.i2x_en = true;
	extd_dpi_params.dispif_config.dpi.i2x_edge = 2;
	extd_dpi_params.dispif_config.dpi.embsync = false;

	HDMI_LOG("hdmi_resolution_setting_res (%d)\n", arg);
}

int hdmi_check_resolution(int src_w, int src_h, int physical_w, int physical_h)
{
	int ret = 0;

	if (physical_w <= 0 || physical_h <= 0 || src_w > physical_w || src_h > physical_h) {
		HDMI_LOG("hdmi_check_resolution fail\n");
		ret = -1;
	}

	return ret;
}

int hdmi_recompute_bg(int src_w, int src_h)
{
	int physical_w = 0;
	int physical_h = 0;
	int ret = 0;
	int extd_path_state = ext_disp_is_alive();

	/* fail -1; 0 ok but not changed; 1 ok and changed */
	HDMI_LOG("hdmi_recompute_bg(%d*%d), resolution: %d\n", src_w, src_h,
		 p->output_video_resolution);

	if ((extd_path_state == EXTD_DEINIT || extd_path_state == EXTD_SUSPEND)
	    || is_hdmi_active() == false) {
		HDMI_LOG("hdmi is not alive\n");
		return -1;
	}

	switch (p->output_video_resolution) {
	case HDMI_VIDEO_720x480p_60Hz:
		{
			physical_w = 720;
			physical_h = 480;
			break;
		}
	case HDMI_VIDEO_1280x720p_60Hz:
		{
			physical_w = 1280;
			physical_h = 720;
			break;
		}
	case HDMI_VIDEO_1920x1080p_30Hz:
	case HDMI_VIDEO_1920x1080p_60Hz:
		{
			physical_w = 1920;
			physical_h = 1080;
			break;
		}
	default:
		{
			HDMI_LOG("hdmi_recompute_bg, resolution fail\n");
			break;
		}
	}

	ret = hdmi_check_resolution(src_w, src_h, physical_w, physical_h);
	if (ret >= 0) {
		extd_dpi_params.dispif_config.dpi.bg_width = physical_w - src_w;
		extd_dpi_params.dispif_config.dpi.bg_height = physical_h - src_h;

		extd_dpi_params.dispif_config.dpi.width = src_w;
		extd_dpi_params.dispif_config.dpi.height = src_h;
		HDMI_LOG("bg_width*bg_height(%d*%d)\n", extd_dpi_params.dispif_config.dpi.bg_width,
			 extd_dpi_params.dispif_config.dpi.bg_height);
	}

	return ret;
}

/* Reset HDMI Driver state */
static void hdmi_state_reset(void)
{
	HDMI_FUNC();

	if (hdmi_drv->get_state() == HDMI_STATE_ACTIVE) {
		switch_set_state(&hdmi_switch_data, HDMI_STATE_ACTIVE);
		hdmi_reschange = HDMI_VIDEO_RESOLUTION_NUM;
	} else {
		switch_set_state(&hdmi_switch_data, HDMI_STATE_NO_DEVICE);
		switch_set_state(&hdmires_switch_data, 0);
	}
}

/* HDMI Driver state callback function */
void hdmi_state_callback(enum HDMI_STATE state)
{
	pr_debug("[hdmi]%s, state = %d\n", __func__, state);
	/*RET_VOID_IF((p->is_force_disable == true));*/
	/*RET_VOID_IF(IS_HDMI_FAKE_PLUG_IN());*/
	if ((p->is_force_disable == true) || (IS_HDMI_FAKE_PLUG_IN()))
		return;

	switch (state) {
	case HDMI_STATE_NO_DEVICE:
		{
			hdmi_suspend();
			switch_set_state(&hdmi_switch_data, HDMI_STATE_NO_DEVICE);
			switch_set_state(&hdmires_switch_data, 0);

#if defined(CONFIG_MTK_SMARTBOOK_SUPPORT) && defined(CONFIG_HAS_SBSUSPEND)
			if (hdmi_params->cabletype == MHL_SMB_CABLE)
				sb_plug_out();
#endif
			pr_debug("[hdmi]%s, state = %d out!\n", __func__, state);
			break;
		}
	case HDMI_STATE_ACTIVE:
		{
			if (IS_HDMI_ON()) {
				pr_debug("[hdmi]%s, already on(%d) !\n", __func__,
				       atomic_read(&p->state));
				break;
			}

			hdmi_drv->get_params(hdmi_params);
			hdmi_resume();

			if (atomic_read(&p->state) > HDMI_POWER_STATE_OFF) {
				hdmi_reschange = HDMI_VIDEO_RESOLUTION_NUM;
				switch_set_state(&hdmi_switch_data, HDMI_STATE_ACTIVE);

#if defined(CONFIG_MTK_SMARTBOOK_SUPPORT) && defined(CONFIG_HAS_SBSUSPEND)
				if (hdmi_params->cabletype == MHL_SMB_CABLE)
					sb_plug_in();
#endif
			}
			pr_debug("[hdmi]%s, state = %d out!\n", __func__, state);
			break;
		}
	default:
		{
			pr_debug("[hdmi]%s, state not support\n", __func__);
			break;
		}
	}

}

void hdmi_set_layer_num(int layer_num)
{
	if (layer_num >= 0)
		hdmi_layer_num = layer_num;
}

int extd_hdmi_get_dev_info(void *info)
{
	disp_session_info *dispif_info = (disp_session_info *) info;

	memset((void *)dispif_info, 0, sizeof(disp_session_info));

	dispif_info->isOVLDisabled = (hdmi_layer_num == 1) ? 1 : 0;
	if (extd_hdmi_path_get_mode() == EXTD_RDMA_DPI_MODE)
		dispif_info->isOVLDisabled = 0;

	dispif_info->maxLayerNum = hdmi_layer_num;
	dispif_info->displayFormat = DISPIF_FORMAT_RGB888;
	dispif_info->displayHeight = p->hdmi_height;
	dispif_info->displayWidth = p->hdmi_width;
	dispif_info->displayMode = DISP_IF_MODE_VIDEO;

	if (hdmi_params->cabletype == MHL_SMB_CABLE) {
		dispif_info->displayType = DISP_IF_HDMI_SMARTBOOK;
		if (IS_HDMI_OFF())
			dispif_info->displayType = DISP_IF_MHL;
	} else if (hdmi_params->cabletype == MHL_CABLE) {
		dispif_info->displayType = DISP_IF_MHL;
	} else {
		dispif_info->displayType = DISP_IF_HDMI;
	}

	dispif_info->isHwVsyncAvailable = 0;
	dispif_info->vsyncFPS = 60;

	if (dispif_info->displayWidth * dispif_info->displayHeight <= 240 * 432)
		dispif_info->physicalHeight = dispif_info->physicalWidth = 0;
	else if (dispif_info->displayWidth * dispif_info->displayHeight <= 320 * 480)
		dispif_info->physicalHeight = dispif_info->physicalWidth = 0;
	else if (dispif_info->displayWidth * dispif_info->displayHeight <= 480 * 854)
		dispif_info->physicalHeight = dispif_info->physicalWidth = 0;
	else
		dispif_info->physicalHeight = dispif_info->physicalWidth = 0;

	dispif_info->isConnected = 1;
	/* dispif_info->isHDCPSupported = (unsigned int)hdmi_params->HDCPSupported; */

	return 0;
}

void hdmi_enable(int enable)
{
	HDMI_FUNC();
	if (enable) {
		if (p->is_enabled) {
			pr_debug("[hdmi] hdmi already enable, %s()\n", __func__);
			return;
		}

		if (hdmi_drv->enter)
			hdmi_drv->enter();

		hdmi_drv_init();
		hdmi_power_on();
		p->is_enabled = true;
	} else {
		if (!p->is_enabled)
			return;

		hdmi_power_off();
		hdmi_drv_deinit();
		/* when disable hdmi, HPD is disabled */
		switch_set_state(&hdmi_switch_data, HDMI_STATE_NO_DEVICE);

		p->is_enabled = false;

		if (hdmi_drv->exit)
			hdmi_drv->exit();
	}

}

void hdmi_power_enable(int enable)
{
	HDMI_FUNC();
	/*RETIF(!p->is_enabled, 0);*/
	if (!p->is_enabled) {
		HDMI_LOG("return in %d\n", __LINE__);
		return;
	}

	if (enable) {
		/*RETIF(otg_enable_status, 0);*/
		if (otg_enable_status) {
			HDMI_LOG("return in %d\n", __LINE__);
			return;
		}
		hdmi_power_on();
	} else {
		hdmi_power_off();
		switch_set_state(&hdmi_switch_data, HDMI_STATE_NO_DEVICE);
	}
}

void hdmi_force_disable(int enable)
{
	HDMI_FUNC();
/*
	RETIF(!p->is_enabled, 0);
	RETIF(IS_HDMI_OFF(), 0);
*/
	if (!p->is_enabled) {
		HDMI_LOG("return in %d\n", __LINE__);
		return;
	}
	if (IS_HDMI_OFF()) {
		HDMI_LOG("return in %d\n", __LINE__);
		return;
	}

	if (enable) {
		if (p->is_force_disable == true)
			return;

		if (IS_HDMI_FAKE_PLUG_IN() || (hdmi_drv->get_state() == HDMI_STATE_ACTIVE)) {
			hdmi_suspend();
			switch_set_state(&hdmi_switch_data, HDMI_STATE_NO_DEVICE);
			switch_set_state(&hdmires_switch_data, 0);
		}

		p->is_force_disable = true;
	} else {
		if (p->is_force_disable == false)
			return;

		if (IS_HDMI_FAKE_PLUG_IN() || (hdmi_drv->get_state() == HDMI_STATE_ACTIVE)) {
			hdmi_resume();
			msleep(1000);
			switch_set_state(&hdmi_switch_data, HDMI_STATE_ACTIVE);
			hdmi_reschange = HDMI_VIDEO_RESOLUTION_NUM;
		}

		p->is_force_disable = false;
	}
}


bool hdmi_is_debug_output(void)
{
	return HDMI_DEBUG_OUTPUT;
}

void hdmi_set_USBOTG_status(int status)
{
	HDMI_LOG("MTK_HDMI_USBOTG_STATUS, arg=%d, enable %d\n", status, p->is_enabled);
/*
	RETIF(!p->is_enabled, 0);
	RETIF((hdmi_params->cabletype != MHL_CABLE), 0);
*/
	if (!p->is_enabled) {
		HDMI_LOG("return in %d\n", __LINE__);
		return;
	}
	if (hdmi_params->cabletype != MHL_CABLE) {
		HDMI_LOG("return in %d\n", __LINE__);
		return;
	}

	if (status)
		otg_enable_status = true;
	else {
		otg_enable_status = false;
		/*RETIF(p->is_force_disable, 0);*/
		if (p->is_force_disable) {
			HDMI_LOG("return in %d\n", __LINE__);
			return;
		}
		hdmi_power_on();
	}
}


void hdmi_set_audio_enable(int enable)
{
	/*RETIF(!p->is_enabled, 0);*/
	if (!p->is_enabled) {
		HDMI_LOG("return in %d\n", __LINE__);
		return;
	}

	hdmi_drv->audio_enable(enable);
}


void hdmi_set_video_enable(int enable)
{
	/*RETIF(!p->is_enabled, 0);*/
	if (!p->is_enabled) {
		HDMI_LOG("return in %d\n", __LINE__);
		return;
	}

	hdmi_drv->video_enable(enable);
}

#if (defined(CONFIG_MTK_IN_HOUSE_TEE_SUPPORT) && defined(CONFIG_MTK_HDMI_HDCP_SUPPORT) \
	&& defined(CONFIG_MTK_DRM_KEY_MNG_SUPPORT))
void hdmi_install_hdcpkey(hdmi_hdcp_drmkey *key)
#else
void hdmi_install_hdcpkey(hdmi_hdcp_key *key)
#endif
{
	hdmi_drv->hdcpkey((uint8_t *) key);
}

static int flag_resolution_interlace(HDMI_VIDEO_RESOLUTION resolution)
{
	if ((resolution == HDMI_VIDEO_1920x1080i_60Hz) ||
	    (resolution == HDMI_VIDEO_1920x1080i_50Hz) ||
#if defined(MTK_INTERNAL_MHL_SUPPORT)
	    (resolution == HDMI_VIDEO_1920x1080i3d_sbs_60Hz) ||
	    (resolution == HDMI_VIDEO_1920x1080i3d_sbs_50Hz) ||
#endif
	    (resolution == HDMI_VIDEO_1920x1080i3d_60Hz) ||
	    (resolution == HDMI_VIDEO_1920x1080i3d_60Hz))
		return true;
	else
		return false;
}


void hdmi_set_resolution(int res)
{
	int extd_path_state;

	HDMI_LOG("video resolution configuration, res:%d, old res:%ld, video_on:%d\n", res,
		 hdmi_reschange, p->is_mhl_video_on);
	/*RETIF(!p->is_enabled, 0);*/
	if (!p->is_enabled) {
		HDMI_LOG("return in %d\n", __LINE__);
		return;
	}


	if (hdmi_reschange == res) {
		HDMI_LOG("hdmi_reschange=%ld\n", hdmi_reschange);
		return;
	}

	p->is_clock_on = false;
	hdmi_reschange = res;

	if (hdmi_bufferdump_on > 0) {
		MMProfileLogEx(ddp_mmp_get_events()->Extd_State, MMProfileFlagStart, ResChange,
			       res);
	}

	if (down_interruptible(&hdmi_update_mutex)) {
		HDMI_LOG("[HDMI] can't get semaphore in\n");
		return;
	}

	/*if ddp path already start, stop ddp path, and release all fence of external session */
	extd_path_state = ext_disp_is_alive();
	if (extd_path_state == EXTD_RESUME) {
		int i = 0;
		int session_id;

		ext_disp_suspend();

		i = 0;
		session_id = ext_disp_get_sess_id();
		for (i = 0; i < HDMI_OVERLAY_CNT; i++)
			mtkfb_release_layer_fence(session_id, i);
	}

	hdmi_resolution_setting(res);
	hdmi_video_config(p->output_video_resolution, HDMI_VIN_FORMAT_RGB888,
			  HDMI_VOUT_FORMAT_RGB888);


	p->is_mhl_video_on = false;

	if (factory_mode == true)
		hdmi_hwc_on = 0;
	up(&hdmi_update_mutex);

	if (flag_resolution_interlace(res))
		g_hdmi_is_interlace = 1;
	else
		g_hdmi_is_interlace = 0;

	if ((factory_mode == false) && hdmi_hwc_on)
		switch_set_state(&hdmires_switch_data, hdmi_reschange + 1);

	p->is_clock_on = true;

	if (hdmi_bufferdump_on > 0) {
		MMProfileLogEx(ddp_mmp_get_events()->Extd_State, MMProfileFlagEnd, ResChange,
			       hdmi_reschange + 1);
	}
}

int hdmi_get_dev_info(void *info)
{
	int ret = 0;
	int displayid = 0;
	mtk_dispif_info_t hdmi_info;

	if (!info) {
		HDMI_LOG("ioctl pointer is NULL\n");
		return -EFAULT;
	}

	if (hdmi_bufferdump_on > 0) {
		MMProfileLogEx(ddp_mmp_get_events()->Extd_DevInfo, MMProfileFlagStart,
			       p->is_enabled, p->is_clock_on);
	}

	HDMI_LOG("DEV_INFO configuration get +\n");

	if (copy_from_user(&displayid, info, sizeof(displayid))) {
		if (hdmi_bufferdump_on > 0) {
			MMProfileLogEx(ddp_mmp_get_events()->Extd_ErrorInfo, MMProfileFlagPulse,
				       Devinfo, 0);
		}
		HDMI_LOG(": copy_from_user failed! line:%d\n", __LINE__);
		return -EAGAIN;
	}

	if (displayid != MTKFB_DISPIF_HDMI)
		HDMI_LOG(": invalid display id:%d\n", displayid);

	memset(&hdmi_info, 0, sizeof(hdmi_info));
	hdmi_info.displayFormat = DISPIF_FORMAT_RGB888;
	hdmi_info.displayHeight = p->hdmi_height;
	hdmi_info.displayWidth = p->hdmi_width;
	hdmi_info.display_id = displayid;
	hdmi_info.isConnected = 1;
	hdmi_info.displayMode = DISPIF_MODE_COMMAND;

	if (hdmi_params->cabletype == MHL_SMB_CABLE)
		hdmi_info.displayType = HDMI_SMARTBOOK;
	else if (hdmi_params->cabletype == MHL_CABLE)
		hdmi_info.displayType = MHL;
	else
		hdmi_info.displayType = HDMI;

	hdmi_info.isHwVsyncAvailable = 1;
	hdmi_info.vsyncFPS = 60;

	if (copy_to_user(info, &hdmi_info, sizeof(hdmi_info))) {
		if (hdmi_bufferdump_on > 0) {
			MMProfileLogEx(ddp_mmp_get_events()->Extd_ErrorInfo, MMProfileFlagPulse,
				       Devinfo, 1);
		}

		HDMI_LOG("copy_to_user failed! line:%d\n", __LINE__);
		ret = -EFAULT;
	}

	if (hdmi_bufferdump_on > 0) {
		MMProfileLogEx(ddp_mmp_get_events()->Extd_DevInfo, MMProfileFlagEnd, p->is_enabled,
			       hdmi_info.displayType);
	}
	HDMI_LOG("DEV_INFO configuration get displayType-%d\n", hdmi_info.displayType);

	return ret;
}

int hdmi_get_capability(void *info)
{
	int ret = 0;
	int query_type = 0;

	query_type = hdmi_get_support_info();

	if (copy_to_user(info, &query_type, sizeof(query_type))) {
		HDMI_LOG(": copy_to_user error! line:%d\n", __LINE__);
		ret = -EFAULT;
	}
	HDMI_LOG("[hdmi][HDMI] query_type  done %x\n", query_type);

	return ret;
}

int hdmi_get_edid(void *edid_info)
{
	int ret = 0;
	HDMI_EDID_T pv_get_info;

	memset(&pv_get_info, 0, sizeof(pv_get_info));

	if (!edid_info) {
		HDMI_LOG("ioctl pointer is NULL\n");
		return -EFAULT;
	}

	if (hdmi_drv->getedid) {
		hdmi_drv->getedid(&pv_get_info);
	}

	if (copy_to_user(edid_info, &pv_get_info, sizeof(pv_get_info))) {
		HDMI_LOG("copy_to_user failed! line:%d\n", __LINE__);
		ret = -EFAULT;
	}

	return ret;
}

int hdmi_screen_capture(void *info)
{
	return 0;
}

int hdmi_is_force_awake(void *argp)
{
	return (copy_to_user
		(argp, &hdmi_params->is_force_awake,
		 sizeof(hdmi_params->is_force_awake)) ? -EFAULT : 0);
}

int hdmi_factory_mode_test(HDMI_FACTORY_MODE_TEST test_step, void *info)
{
	int ret = 0;

	if (factory_mode == false)
		return ret;

	switch (test_step) {
	case STEP1_ENABLE:
		{
			if (hdmi_drv->power_on()) {
				ret = -EAGAIN;
				HDMI_LOG("Error factory mode test fail\n");
			}
			break;
		}
	case STEP2_GET_STATUS:
		{
			int hdmi_status = p->is_clock_on ? true : false;

			HDMI_LOG(" HDMI STATUS is %d\n", p->is_clock_on);
			if (hdmi_is_debug_output()) {
				pr_err("[HDMI] Debug Output\n");
				hdmi_status = true;
			}

			if (copy_to_user(info, &hdmi_status, sizeof(hdmi_status))) {
				HDMI_LOG("copy_to_user failed! line:%d\n", __LINE__);
				ret = -EFAULT;
			}
			break;
		}
	case STEP3_START:
		{
			if (hdmi_is_debug_output()) {
				hdmi_config_pll(p->output_video_resolution);
				dpi_setting_res(p->output_video_resolution);
				hdmi_internal_video_config(p->output_video_resolution, 0, 0);
			} else {
				if (down_interruptible(&hdmi_update_mutex)) {
					HDMI_LOG("[HDMI] can't get semaphore in\n");
					return -EAGAIN;
				}

				ext_disp_factory_test(0, (void *)&extd_dpi_params);
				hdmi_hwc_on = 0;
				up(&hdmi_update_mutex);

				if (IS_HDMI_FAKE_PLUG_IN()) {
					HDMI_LOG("fake cable in to return line:%d\n", __LINE__);
				} else {
					msleep(50);
					hdmi_video_config(p->output_video_resolution,
							  HDMI_VIN_FORMAT_RGB888,
							  HDMI_VOUT_FORMAT_RGB888);
				}
			}
			break;
		}
	case STEP3_SUSPEND:
		{
			hdmi_drv->power_off();
			ddp_dpi_power_off(DISP_MODULE_DPI1, NULL);
			break;
		}
	default:
		break;
	}

	return ret;
}

static void hdmi_udelay(unsigned int us)
{
	udelay(us);
}

static void hdmi_mdelay(unsigned int ms)
{
	msleep(ms);
}


void hdmi_read_reg(READ_REG_VALUE *reg_val)
{
	hdmi_drv->read(reg_val->u1address, &(reg_val->pu1Data));
}

void hdmi_write_reg(hdmi_device_write w_info)
{
	hdmi_drv->write(w_info.u4Addr, w_info.u4Data);
}


void hdmi_config_pll(unsigned long resolutionmode)
{
	/* *(volatile unsigned int*)(0x1000c040) = (0x0);*//*PLL Test Control Register 0 */
	vWriteIoPll(0x040, 0);

	switch (resolutionmode) {

		/*TVD PLL 432MHz */
	case HDMI_VIDEO_720x480p_60Hz:
	case HDMI_VIDEO_720x576p_50Hz:
		vWriteIoPllMsk(0x014, 0x9e, 0x9e);	/* Analog DE to confirmed, Enable CLKSQ */
		udelay(20);
		vWriteIoPllMsk(0x014, 0xBE, 0xBE);	/*Analog DE to confirmed, Enable CLKSQ_LPF */
		udelay(20);
		vWriteIoPllMsk(MHL_TVDPLL_PWR, 0x3, RG_TVDPLL_PWR_ON | RG_TVDPLL_SDM_ISO_EN);
		udelay(10);
		vWriteIoPllMsk(MHL_TVDPLL_PWR, 0x0, RG_TVDPLL_SDM_ISO_EN);
		udelay(10);
		vWriteIoPllMsk(MHL_TVDPLL_CON1, (0x80109d89 << RG_TVDPLL_SDM_PCW),
			RG_TVDPLL_SDM_PCW_MASK);	/* 0x109d8a */

		udelay(10);
		/* *(volatile unsigned int*)(0x1000c270) = (0x121);*//*PLL Test Control Register 0 */
		vWriteIoPll(MHL_TVDPLL_CON0, 0x121);

		break;

		/*TVD PLL 594MHz */
	case HDMI_VIDEO_1920x1080p_30Hz:
	case HDMI_VIDEO_1280x720p_50Hz:
	case HDMI_VIDEO_1920x1080i_50Hz:
	case HDMI_VIDEO_1920x1080p_25Hz:
	case HDMI_VIDEO_1920x1080p_24Hz:
	case HDMI_VIDEO_1920x1080p_50Hz:
	case HDMI_VIDEO_1280x720p3d_50Hz:
	case HDMI_VIDEO_1920x1080i3d_50Hz:
	case HDMI_VIDEO_1920x1080p3d_24Hz:
		vWriteIoPllMsk(0x014, 0x9e, 0x9e);	/* Analog DE to confirmed, Enable CLKSQ */
		udelay(20);
		vWriteIoPllMsk(0x014, 0xBE, 0xBE);	/*Analog DE to confirmed, Enable CLKSQ_LPF */

		udelay(20);
		vWriteIoPllMsk(MHL_TVDPLL_PWR, 0x3, RG_TVDPLL_PWR_ON | RG_TVDPLL_SDM_ISO_EN);
		udelay(10);
		vWriteIoPllMsk(MHL_TVDPLL_PWR, 0x0, RG_TVDPLL_SDM_ISO_EN);
		udelay(10);
		vWriteIoPllMsk(MHL_TVDPLL_CON1, (0x8016D89D << RG_TVDPLL_SDM_PCW),
			RG_TVDPLL_SDM_PCW_MASK);	/* 16d89e */
		udelay(10);
		/* *(volatile unsigned int*)(0x1000c270) = (0x121);*//*PLL Test Control Register 0 */
		vWriteIoPll(MHL_TVDPLL_CON0, 0x121);

		break;

		/* TVD PLL 593.4MHz */
	case HDMI_VIDEO_1280x720p_60Hz:
	case HDMI_VIDEO_1920x1080i_60Hz:
	case HDMI_VIDEO_1920x1080p_23Hz:
	case HDMI_VIDEO_1920x1080p_29Hz:
	case HDMI_VIDEO_1920x1080p_60Hz:
	case HDMI_VIDEO_1280x720p3d_60Hz:
	case HDMI_VIDEO_1920x1080i3d_60Hz:
	case HDMI_VIDEO_1920x1080p3d_23Hz:
		vWriteIoPllMsk(0x014, 0x9e, 0x9e);	/* Analog DE to confirmed, Enable CLKSQ */
		udelay(20);
		vWriteIoPllMsk(0x014, 0xBE, 0xBE);	/*Analog DE to confirmed, Enable CLKSQ_LPF */

		udelay(20);
		vWriteIoPllMsk(MHL_TVDPLL_PWR, 0x3, RG_TVDPLL_PWR_ON | RG_TVDPLL_SDM_ISO_EN);
		udelay(10);
		vWriteIoPllMsk(MHL_TVDPLL_PWR, 0x0, RG_TVDPLL_SDM_ISO_EN);
		udelay(10);
		vWriteIoPllMsk(MHL_TVDPLL_CON1, (0x8016d2b5 << RG_TVDPLL_SDM_PCW),
			RG_TVDPLL_SDM_PCW_MASK);	/* 16d2b5 */

		udelay(10);
		/* *(volatile unsigned int*)(0x1000c270) = (0x121);*//*PLL Test Control Register 0 */
		vWriteIoPll(MHL_TVDPLL_CON0, 0x121);

		break;
	default:
		{
			break;
		}
	}


}

void dpi_setting_res(unsigned long arg)
{
	switch (arg) {
	case HDMI_VIDEO_720x480p_60Hz:
		{

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x0) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x4) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x8) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xc) = 0x00000007;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x10) = 0x00410010;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xe0) = 0x02000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x14) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x18) = 0x01e002d0;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x20) = 0x0000003e;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x24) = 0x0010003c;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x28) = 0x00000006;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x2c) = 0x0009001e;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x68) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x6c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x70) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x74) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x78) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x7c) = 0x00000000;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xf00) = 0x00000041;


			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xa0) = 0x00000000;


			break;
		}
	case HDMI_VIDEO_720x576p_50Hz:
		{

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x0) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x4) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x8) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xc) = 0x00000007;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x10) = 0x00410010;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xe0) = 0x02000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x14) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x18) = 0x024002d0;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x20) = 0x00000040;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x24) = 0x000c0044;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x28) = 0x00000005;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x2c) = 0x00050027;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x30) = 0x00010001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x68) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x6c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x70) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x74) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x78) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x7c) = 0x00000000;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xf00) = 0x00000041;


			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xa0) = 0x00000000;

			break;
		}
	case HDMI_VIDEO_1280x720p_60Hz:
		{

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x0) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x4) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x8) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xc) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x10) = 0x00410000;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xe0) = 0x82000200;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x14) = 0x00006000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x18) = 0x02d00500;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x20) = 0x00000028;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x24) = 0x006e00dc;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x28) = 0x00000005;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x2c) = 0x00050014;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x68) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x6c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x70) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x74) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x78) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x7c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xf00) = 0x00000041;


			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xa0) = 0x00000000;


			break;
		}
	case HDMI_VIDEO_1280x720p_50Hz:
		{

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x0) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x4) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x8) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xc) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x10) = 0x00410000;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xe0) = 0x82000200;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x14) = 0x00006000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x18) = 0x02d00500;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x20) = 0x00000028;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x24) = 0x01b800dc;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x28) = 0x00000005;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x2c) = 0x00050014;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x68) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x6c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x70) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x74) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x78) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x7c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xf00) = 0x00000041;


			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xa0) = 0x00000000;


			break;
		}
	case HDMI_VIDEO_1920x1080p_24Hz:
		{
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x0) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x4) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x8) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xc) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x10) = 0x00410000;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xe0) = 0x82000200;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x14) = 0x00006000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x18) = 0x04380780;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x20) = 0x0000002c;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x24) = 0x027e0094;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x28) = 0x00000005;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x2c) = 0x00040024;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x68) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x6c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x70) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x74) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x78) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x7c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xf00) = 0x00000041;


			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xa0) = 0x00000000;

			break;
		}


	case HDMI_VIDEO_1920x1080p_60Hz:
		{

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x0) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x4) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x8) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xc) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x10) = 0x00410000;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xe0) = 0x82000200;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x14) = 0x00006000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x18) = 0x04380780;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x20) = 0x0000002c;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x24) = 0x00580094;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x28) = 0x00000005;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x2c) = 0x00040024;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x68) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x6c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x70) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x74) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x78) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x7c) = 0x00000000;


			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xa0) = 0x00000001;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xf00) = 0x00000041;



			break;
		}
	case HDMI_VIDEO_1920x1080p_50Hz:
		{

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x0) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x4) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x8) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xc) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x10) = 0x00410000;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xe0) = 0x82000200;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x14) = 0x00006000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x18) = 0x04380780;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x20) = 0x0000002c;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x24) = 0x02100094;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x28) = 0x00000005;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x2c) = 0x00040024;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x68) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x6c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x70) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x74) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x78) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x7c) = 0x00000000;



			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xa0) = 0x00000001;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xf00) = 0x00000041;

			break;
		}
	case HDMI_VIDEO_1920x1080i_60Hz:
		{

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x0) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x4) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x8) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xc) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x10) = 0x00430004;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xe0) = 0x82000200;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x14) = 0x00006000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x18) = 0x021c0780;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x20) = 0x0000002c;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x24) = 0x00580094;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x28) = 0x00000005;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x2c) = 0x0002000f;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x68) = 0x00010005;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x6c) = 0x00020010;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x70) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x74) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x78) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x7c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xf00) = 0x00000041;


			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xa0) = 0x00000000;


			break;
		}
	case HDMI_VIDEO_1920x1080i_50Hz:
		{

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x0) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x4) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x8) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xc) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x10) = 0x00430004;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xe0) = 0x82000200;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x14) = 0x00006000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x18) = 0x021c0780;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x20) = 0x0000002c;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x24) = 0x02100094;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x28) = 0x00000005;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x2c) = 0x0002000f;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x68) = 0x00010005;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x6c) = 0x00020010;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x70) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x74) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x78) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x7c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xf00) = 0x00000041;


			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xa0) = 0x00000000;



			break;
		}

	case HDMI_VIDEO_1280x720p3d_60Hz:
		{

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x0) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x4) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x8) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xc) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x10) = 0x004100a8;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xe0) = 0x02000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x14) = 0x00006002;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x18) = 0x02d00500;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x20) = 0x00000028;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x24) = 0x006e00dc;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x28) = 0x00000005;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x2c) = 0x00050014;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x68) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x6c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x70) = 0x00000005;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x74) = 0x00050014;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x78) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x7c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xf00) = 0x00000041;



			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xa0) = 0x00000001;
			break;
		}

	case HDMI_VIDEO_1920x1080p3d_24Hz:
		{
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x0) = 0x00000001;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x4) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x8) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xc) = 0x00000007;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x10) = 0x00410078;

			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xe0) = 0x02000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x14) = 0x00006000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x18) = 0x04380780;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x20) = 0x0000002c;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x24) = 0x027e0094;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x28) = 0x00000005;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x2c) = 0x00040024;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x68) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x6c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x70) = 0x00000005;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x74) = 0x00040024;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x78) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0x7c) = 0x00000000;
			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xf00) = 0x00000041;



			*(volatile unsigned int *)(hdmi_ref_reg[DPI1_REG] + 0xa0) = 0x00000000;

			break;
		}

	default:
		break;
	}

	mdelay(10);
	*((volatile unsigned int *)((hdmi_ref_reg[DPI1_REG] + 0x4))) = (1);
	mdelay(40);
	*((volatile unsigned int *)((hdmi_ref_reg[DPI1_REG] + 0x4))) = (0);

}


void hdmi_set_audio_param(HDMITX_AUDIO_PARA *audio_param)
{
	hdmi_drv->audiosetting(audio_param);
}


void hdmi_set_colordeep(unsigned int colorspace, unsigned int deep)
{
	hdmi_drv->colordeep(colorspace, deep);
}


void hdmi_init(void)
{
	int ret = 0;
	int boot_mode;

	static const struct HDMI_UTIL_FUNCS hdmi_utils = {
		.udelay = hdmi_udelay,
		.mdelay = hdmi_mdelay,
		.state_callback = hdmi_state_callback,
	};

	hdmi_drv = (struct HDMI_DRIVER *) HDMI_GetDriver();

	if (NULL == hdmi_drv) {
		pr_debug("[hdmi]%s, hdmi_init fail, can not get hdmi driver handle\n", __func__);
		return;
	}

	hdmi_drv->set_util_funcs(&hdmi_utils);
	hdmi_drv->get_params(hdmi_params);
	hdmi_drv->init();	/* need to remove to power on function Donglei */

	memset((void *)&hdmi_context, 0, sizeof(_t_hdmi_context));
	memset((void *)&extd_dpi_params, 0, sizeof(extd_dpi_params));

	p->output_mode = hdmi_params->output_mode;

	/* for support hdmi hotplug, inform AP the event */
	hdmi_switch_data.name = "hdmi";
	hdmi_switch_data.index = 0;
	hdmi_switch_data.state = HDMI_STATE_NO_DEVICE;
	ret = switch_dev_register(&hdmi_switch_data);

	if (ret)
		pr_debug("[hdmi][HDMI]switch_dev_register failed, returned:%d!\n", ret);

	hdmires_switch_data.name = "res_hdmi";
	hdmires_switch_data.index = 0;
	hdmires_switch_data.state = 0;
	ret = switch_dev_register(&hdmires_switch_data);

	if (ret)
		pr_debug("[hdmi][HDMI]switch_dev_register failed, returned:%d!\n", ret);

	boot_mode = get_boot_mode();

	if ((boot_mode == FACTORY_BOOT) || (boot_mode == ATE_FACTORY_BOOT))
		factory_mode = true;

	SET_HDMI_OFF();
	HDMI_DBG_Init();

	init_waitqueue_head(&hdmi_rdma_config_wq);
	init_waitqueue_head(&hdmi_vsync_wq);
}

/* external display - create HDMI path and configure buffers to ovl or rdma*/
void extd_hdmi_path_init(EXT_DISP_PATH_MODE mode, int session)
{
	HDMI_LOG("extd_hdmi_path_init mode:%d\n", mode);

	ext_disp_path_set_mode(mode);
	ext_disp_init(NULL, session);
}

void extd_hdmi_path_deinit(void)
{
	ext_disp_deinit(NULL);
	hdmi_layer_num = 0;
}

void extd_hdmi_path_resume(void)
{
	ext_disp_resume();
}

int extd_hdmi_trigger(int blocking, void *callback, unsigned int session)
{
	return ext_disp_trigger(blocking, callback, session);
}

int extd_hdmi_config_input_multiple(ext_disp_input_config *input, int idx)
{
	return ext_disp_config_input_multiple(input, idx);
}

void extd_hdmi_path_set_mode(EXT_DISP_PATH_MODE mode)
{
	HDMI_LOG("extd_hdmi_path_set_mode mode:%d\n", mode);
	ext_disp_path_set_mode(mode);

}

EXT_DISP_PATH_MODE extd_hdmi_path_get_mode(void)
{
	EXT_DISP_PATH_MODE mode = ext_disp_path_get_mode();

	HDMI_LOG("extd_hdmi_path_get_mode mode:%d\n", mode);
	return mode;
}

int extd_get_path_handle(disp_path_handle *dp_handle, cmdqRecHandle *pHandle)
{
	return extd_get_handle(dp_handle, pHandle);
}

int extd_get_device_type(void)
{
	int device_type = -1;

	if (IS_HDMI_ON()) {
		if (hdmi_params->cabletype == MHL_SMB_CABLE)
			device_type = DISP_IF_HDMI_SMARTBOOK;
		else if (hdmi_params->cabletype == MHL_CABLE)
			device_type = DISP_IF_MHL;
	}

	return device_type;
}
#endif
