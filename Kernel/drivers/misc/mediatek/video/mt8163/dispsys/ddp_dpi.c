#ifdef BUILD_UBOOT
/* #define ENABLE_DSI_INTERRUPT 0 */

#include <asm/arch/disp_drv_platform.h>
#else
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/mutex.h>

#include "cmdq_record.h"
#include <disp_drv_log.h>
#endif

/*#include "mach/mt_typedefs.h"*/
#include <mt-plat/sync_write.h>
#include <linux/clk.h>
/*#include <mach/irqs.h>*/
#ifdef CONFIG_MTK_CLKMGR
#include <mach/mt_clkmgr.h>
#endif
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/wait.h>

#include "mtkfb.h"
#include "ddp_drv.h"
#include "ddp_hal.h"
#include "ddp_info.h"
#include "ddp_manager.h"
#include "ddp_dpi_reg.h"
#include "ddp_dpi.h"
#include "ddp_reg.h"
#include "ddp_log.h"
#include "ddp_irq.h"
#include "disp_drv_platform.h"
#include "ddp_dsi.h"

#include <debug.h>

#undef  LOG_TAG
#define LOG_TAG "DPI"
#define ENABLE_DPI_INTERRUPT        0
#define DPI_INTERFACE_NUM           2
#define DPI_IDX(module)             ((module == DISP_MODULE_DPI0)?0:1)

#undef LCD_BASE
#define LCD_BASE (0xF4024000)
#define DPI_REG_OFFSET(r)       offsetof(struct DPI_REGS, r)
#define REG_ADDR(base, offset)  (((BYTE *)(base)) + (offset))
#define msleep(x)    mdelay(x)

#if 0
static int dpi_reg_op_debug;

#define DPI_OUTREG32(cmdq, addr, val) \
	{\
		if (dpi_reg_op_debug) \
			pr_debug("[dsi/reg]0x%08x=0x%08x, cmdq:0x%08x\n", addr, val, cmdq);\
		if (cmdq) \
			cmdqRecWrite(cmdq, (unsigned int)(addr)&0x1fffffff, val, ~0); \
		else \
			mt65xx_reg_sync_writel(val, addr); }
#else
#define DPI_OUTREG32(cmdq, addr, val) mt_reg_sync_writel(val, addr)
#endif

struct LVDS_TX_REGS *LVDS_TX_REG = 0;
struct LVDS_ANA_REGS *LVDS_ANA_REG = 0;

static bool s_isDpiPowerOn;
static bool s_isDpi1PowerOn;

static bool s_isDpiStart;
static bool s_isDpiConfig;
static bool s_isDpi1Config;

static int dpi_vsync_irq_count[DPI_INTERFACE_NUM];
static int dpi_undflow_irq_count[DPI_INTERFACE_NUM];
/*static struct DPI_REGS regBackup;*/
struct DPI_REGS *DPI_REG[2];



static LCM_UTIL_FUNCS lcm_utils_dpi;

const uint32_t BACKUP_DPI_REG_OFFSETS[] = {
	DPI_REG_OFFSET(INT_ENABLE),
	DPI_REG_OFFSET(CNTL),
	DPI_REG_OFFSET(SIZE),

	DPI_REG_OFFSET(TGEN_HWIDTH),
	DPI_REG_OFFSET(TGEN_HPORCH),
	DPI_REG_OFFSET(TGEN_VWIDTH_LODD),
	DPI_REG_OFFSET(TGEN_VPORCH_LODD),

	DPI_REG_OFFSET(BG_HCNTL),
	DPI_REG_OFFSET(BG_VCNTL),
	DPI_REG_OFFSET(BG_COLOR),

	DPI_REG_OFFSET(TGEN_VWIDTH_LEVEN),
	DPI_REG_OFFSET(TGEN_VPORCH_LEVEN),
	DPI_REG_OFFSET(TGEN_VWIDTH_RODD),

	DPI_REG_OFFSET(TGEN_VPORCH_RODD),
	DPI_REG_OFFSET(TGEN_VWIDTH_REVEN),

	DPI_REG_OFFSET(TGEN_VPORCH_REVEN),
	DPI_REG_OFFSET(ESAV_VTIM_LOAD),
	DPI_REG_OFFSET(ESAV_VTIM_ROAD),
	DPI_REG_OFFSET(ESAV_FTIM),
};

/*the static functions declare*/
static void lcm_udelay(uint32_t us)
{
	udelay(us);
}

static void lcm_mdelay(uint32_t ms)
{
	msleep(ms);
}

static void lcm_set_reset_pin(uint32_t value)
{
	DPI_OUTREG32(0, DISPSYS_CONFIG_BASE + 0x150, value);
}

static void lcm_send_cmd(uint32_t cmd)
{
	/*DPI_OUTREG32(0, LCD_BASE+0x0F80, cmd); */
}

static void lcm_send_data(uint32_t data)
{
	/*DPI_OUTREG32(0, LCD_BASE+0x0F90, data); */
}

/*static void _BackupDPIRegisters(DISP_MODULE_ENUM module)
{
    UINT32 i;
    DPI_REGS *reg = &regBackup;

    for (i = 0; i < ARY_SIZE(BACKUP_DPI_REG_OFFSETS); ++i)
    {
	DPI_OUTREG32(0, REG_ADDR(reg, BACKUP_DPI_REG_OFFSETS[i]),
		 AS_UINT32(REG_ADDR(DPI_REG[DPI_IDX(module)], BACKUP_DPI_REG_OFFSETS[i])));
    }
}

static void _RestoreDPIRegisters(DISP_MODULE_ENUM module)
{
    UINT32 i;
    DPI_REGS *reg = &regBackup;

    for (i = 0; i < ARY_SIZE(BACKUP_DPI_REG_OFFSETS); ++i)
    {
	DPI_OUTREG32(0, REG_ADDR(DPI_REG[DPI_IDX(module)], BACKUP_DPI_REG_OFFSETS[i]),
		 AS_UINT32(REG_ADDR(reg, BACKUP_DPI_REG_OFFSETS[i])));
    }
}*/

/*the functions declare*/
/*DPI clock setting - use TVDPLL provide DPI clock for HDMI*/
DPI_STATUS ddp_dpi_ConfigPclk(DISP_MODULE_ENUM module, cmdqRecHandle cmdq, unsigned int clk_req,
			      DPI_POLARITY polarity)
{
	unsigned int dpickpol = 1, dpickoutdiv = 1, dpickdut = 1;
	unsigned int pcw = 0, postdiv = 0, ck_div = 0;
	unsigned long bclk = 0;
	struct clk *clksrc = NULL;
	struct DPI_REG_OUTPUT_SETTING ctrl = DPI_REG[DPI_IDX(module)]->OUTPUT_SETTING;
	struct DPI_REG_CLKCNTL clkcon = DPI_REG[DPI_IDX(module)]->DPI_CLKCON;

	/*DPI_OUTREG32(cmdq, DDP_REG_TVDPLL_CON0, 0x120);*/	/* TVDPLL disable */
	switch (clk_req) {
	case DPI_VIDEO_720x480p_60Hz:
	case DPI_VIDEO_720x576p_50Hz:
		{
			pcw = 0x109d89;
			postdiv = 1;
			ck_div = 4;
			bclk = 432000000;	/*432M*/
			break;
		}

	case DPI_VIDEO_1920x1080p_30Hz:
	case DPI_VIDEO_1280x720p_50Hz:
	case DPI_VIDEO_1920x1080i_50Hz:
	case DPI_VIDEO_1920x1080p_25Hz:
	case DPI_VIDEO_1920x1080p_24Hz:
	case DPI_VIDEO_1920x1080p_50Hz:
	case DPI_VIDEO_1280x720p3d_50Hz:
	case DPI_VIDEO_1920x1080i3d_50Hz:
	case DPI_VIDEO_1920x1080p3d_24Hz:
		{
			pcw = 0x16d89d;
			postdiv = 0;
			ck_div = 2;
			bclk = 594000000;	/*594M*/
			break;
		}

	case DPI_VIDEO_1280x720p_60Hz:
	case DPI_VIDEO_1920x1080i_60Hz:
	case DPI_VIDEO_1920x1080p_23Hz:
	case DPI_VIDEO_1920x1080p_29Hz:
	case DPI_VIDEO_1920x1080p_60Hz:
	case DPI_VIDEO_1280x720p3d_60Hz:
	case DPI_VIDEO_1920x1080i3d_60Hz:
	case DPI_VIDEO_1920x1080p3d_23Hz:
		{
			pcw = 0x16d2b5;
			postdiv = 0;
			ck_div = 3;
			bclk = 593400000;	/*593.4M*/
			break;
		}

	default:
		{
			pr_warn("DISP/DPI,unknown clock frequency: %d\n", clk_req);
			break;
		}
	}

	switch (clk_req) {
	case DPI_VIDEO_720x480p_60Hz:
	case DPI_VIDEO_720x576p_50Hz:
	case DPI_VIDEO_1920x1080p3d_24Hz:
	case DPI_VIDEO_1280x720p_60Hz:
		{
			dpickpol = 0;
			dpickdut = 0;
			break;
		}

	case DPI_VIDEO_1920x1080p_30Hz:
	case DPI_VIDEO_1280x720p_50Hz:
	case DPI_VIDEO_1920x1080i_50Hz:
	case DPI_VIDEO_1920x1080p_25Hz:
	case DPI_VIDEO_1920x1080p_24Hz:
	case DPI_VIDEO_1920x1080p_50Hz:
	case DPI_VIDEO_1280x720p3d_50Hz:
	case DPI_VIDEO_1920x1080i3d_50Hz:
	case DPI_VIDEO_1920x1080i_60Hz:
	case DPI_VIDEO_1920x1080p_23Hz:
	case DPI_VIDEO_1920x1080p_29Hz:
	case DPI_VIDEO_1920x1080p_60Hz:
	case DPI_VIDEO_1280x720p3d_60Hz:
	case DPI_VIDEO_1920x1080i3d_60Hz:
	case DPI_VIDEO_1920x1080p3d_23Hz:
		{
			break;
		}

	default:
		{
			pr_warn("DISP/DPI,unknown clock frequency: %d\n", clk_req);
			break;
		}
	}

	/* pr_warn("DISP/DPI,TVDPLL clock setting clk %d, clksrc: %d\n", clk_req,  clksrc); */
#if 0
#ifndef CONFIG_FPGA_EARLY_PORTING	/* FOR BRING_UP */
	DPI_OUTREG32(cmdq, DDP_REG_TVDPLL_CON1, pcw | (1 << 31));	/* set TVDPLL output clock frequency */
	DPI_OUTREG32(cmdq, DDP_REG_TVDPLL_CON0, 0x121);	/* TVDPLL enable */
#endif
#ifdef CONFIG_MTK_CLKMGR
	clkmux_sel(MT_MUX_DPI1, ck_div, "dpi1");	/*CLK_CFG_7  bit0~2 */
#endif
	/*IO driving setting */
	/* MASKREG32(DISPSYS_IO_DRIVING, 0x3C00, 0x0); // 0x1400 for 8mA, 0x0 for 4mA */
#else
	if (ck_div == 1)
		clksrc = ddp_clk_map[TOP_TVDPLL_D2];
	else if (ck_div == 2)
		clksrc = ddp_clk_map[TOP_TVDPLL_D4];
	else if (ck_div == 3)
		clksrc = ddp_clk_map[TOP_TVDPLL_D8];
	else if (ck_div == 4)
		clksrc = ddp_clk_map[TOP_TVDPLL_D16];

	clk_prepare_enable(ddp_clk_map[MM_CLK_MUX_DPI1_SEL]);
	clk_set_parent(ddp_clk_map[MM_CLK_MUX_DPI1_SEL], clksrc);
	clk_disable_unprepare(ddp_clk_map[MM_CLK_MUX_DPI1_SEL]);

	clk_prepare_enable(ddp_clk_map[APMIXED_TVDPLL]);
	clk_set_rate(ddp_clk_map[APMIXED_TVDPLL], bclk);
	clk_disable_unprepare(ddp_clk_map[APMIXED_TVDPLL]);

#endif
	/*DPI output clock polarity */
	ctrl.CLK_POL = (DPI_POLARITY_FALLING == polarity) ? 1 : 0;
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->OUTPUT_SETTING, AS_UINT32(&ctrl));

	clkcon.DPI_CKOUT_DIV = dpickoutdiv;
	clkcon.DPI_CK_POL = dpickpol;
	clkcon.DPI_CK_DUT = dpickdut;
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->DPI_CLKCON, AS_UINT32(&clkcon));

	return DPI_STATUS_OK;
}

DPI_STATUS ddp_dpi_ConfigCLK(DISP_MODULE_ENUM module, cmdqRecHandle cmdq, DPI_POLARITY polarity,
			     bool LVDSEN)
{
	struct DPI_REG_OUTPUT_SETTING pol = DPI_REG[DPI_IDX(module)]->OUTPUT_SETTING;

	pol.CLK_POL = (DPI_POLARITY_FALLING == polarity) ? 1 : 0;
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->OUTPUT_SETTING, AS_UINT32(&pol));
	DPI_OUTREGBIT(cmdq, struct DPI_REG_DDR_SETTING, DPI_REG[DPI_IDX(module)]->DDR_SETTING, DDR_4PHASE,
		      0);
	DPI_OUTREGBIT(cmdq, struct DPI_REG_CLKCNTL, DPI_REG[DPI_IDX(module)]->DPI_CLKCON, DPI_CK_DIV, 0);
	if (LVDSEN || (module == DISP_MODULE_DPI1)) {
		DPI_OUTREGBIT(cmdq, struct DPI_REG_CLKCNTL, DPI_REG[DPI_IDX(module)]->DPI_CLKCON,
			      DPI_CKOUT_DIV, 1);
		DISPCHECK("LVDS DPI_CKOUT_DIV set to 1.\n");
	} else
		DPI_OUTREGBIT(cmdq, struct DPI_REG_CLKCNTL, DPI_REG[DPI_IDX(module)]->DPI_CLKCON,
			      DPI_CKOUT_DIV, 0);
	return DPI_STATUS_OK;
}

DPI_STATUS ddp_dpi_ConfigDE(DISP_MODULE_ENUM module, cmdqRecHandle cmdq, DPI_POLARITY polarity)
{
	struct DPI_REG_OUTPUT_SETTING pol = DPI_REG[DPI_IDX(module)]->OUTPUT_SETTING;

	pol.DE_POL = (DPI_POLARITY_FALLING == polarity) ? 1 : 0;
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->OUTPUT_SETTING, AS_UINT32(&pol));

	return DPI_STATUS_OK;
}

DPI_STATUS ddp_dpi_ConfigVsync(DISP_MODULE_ENUM module, cmdqRecHandle cmdq, DPI_POLARITY polarity,
			       uint32_t pulseWidth, uint32_t backPorch, uint32_t frontPorch)
{
	struct DPI_REG_TGEN_VWIDTH_LODD vwidth_lodd = DPI_REG[DPI_IDX(module)]->TGEN_VWIDTH_LODD;
	struct DPI_REG_TGEN_VPORCH_LODD vporch_lodd = DPI_REG[DPI_IDX(module)]->TGEN_VPORCH_LODD;
	struct DPI_REG_OUTPUT_SETTING pol = DPI_REG[DPI_IDX(module)]->OUTPUT_SETTING;
	struct DPI_REG_CNTL VS = DPI_REG[DPI_IDX(module)]->CNTL;

	pol.VSYNC_POL = (DPI_POLARITY_FALLING == polarity) ? 1 : 0;
	vwidth_lodd.VPW_LODD = pulseWidth;
	vporch_lodd.VBP_LODD = backPorch;
	vporch_lodd.VFP_LODD = frontPorch;

	VS.VS_LODD_EN = 1;
	VS.VS_LEVEN_EN = 0;
	VS.VS_RODD_EN = 0;
	VS.VS_REVEN_EN = 0;

	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->OUTPUT_SETTING, AS_UINT32(&pol));
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->TGEN_VWIDTH_LODD, AS_UINT32(&vwidth_lodd));
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->TGEN_VPORCH_LODD, AS_UINT32(&vporch_lodd));
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->CNTL, AS_UINT32(&VS));

	return DPI_STATUS_OK;
}

DPI_STATUS ddp_dpi_ConfigHsync(DISP_MODULE_ENUM module, cmdqRecHandle cmdq, DPI_POLARITY polarity,
			       uint32_t pulseWidth, uint32_t backPorch, uint32_t frontPorch)
{
	struct DPI_REG_TGEN_HPORCH hporch = DPI_REG[DPI_IDX(module)]->TGEN_HPORCH;
	struct DPI_REG_OUTPUT_SETTING pol = DPI_REG[DPI_IDX(module)]->OUTPUT_SETTING;

	hporch.HBP = backPorch;
	hporch.HFP = frontPorch;
	pol.HSYNC_POL = (DPI_POLARITY_FALLING == polarity) ? 1 : 0;
	DPI_REG[DPI_IDX(module)]->TGEN_HWIDTH = pulseWidth;

	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->TGEN_HWIDTH, pulseWidth);
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->TGEN_HPORCH, AS_UINT32(&hporch));
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->OUTPUT_SETTING, AS_UINT32(&pol));

	return DPI_STATUS_OK;
}

DPI_STATUS ddp_dpi_ConfigDualEdge(DISP_MODULE_ENUM module, cmdqRecHandle cmdq, bool enable,
				  uint32_t mode)
{
	DPI_OUTREGBIT(cmdq, struct DPI_REG_OUTPUT_SETTING, DPI_REG[DPI_IDX(module)]->OUTPUT_SETTING,
		      DUAL_EDGE_SEL, enable);

#if 1				/* ndef CONFIG_FPGA_EARLY_PORTING */
	DPI_OUTREGBIT(cmdq, struct DPI_REG_DDR_SETTING, DPI_REG[DPI_IDX(module)]->DDR_SETTING, DDR_4PHASE,
		      0);
	DPI_OUTREGBIT(cmdq, struct DPI_REG_DDR_SETTING, DPI_REG[DPI_IDX(module)]->DDR_SETTING, DDR_EN, 0);
#endif

	return DPI_STATUS_OK;
}

DPI_STATUS ddp_dpi_ConfigBG(DISP_MODULE_ENUM module, cmdqRecHandle cmdq, bool enable, int BG_W,
			    int BG_H)
{
	if (enable == false) {
		DPI_OUTREGBIT(cmdq, struct DPI_REG_CNTL, DPI_REG[DPI_IDX(module)]->CNTL, BG_EN, 0);
	} else if (BG_W || BG_H) {
		DPI_OUTREGBIT(cmdq, struct DPI_REG_CNTL, DPI_REG[DPI_IDX(module)]->CNTL, BG_EN, 1);
		DPI_OUTREGBIT(cmdq, struct DPI_REG_BG_HCNTL, DPI_REG[DPI_IDX(module)]->BG_HCNTL, BG_RIGHT,
			      BG_W / 4);
		DPI_OUTREGBIT(cmdq, struct DPI_REG_BG_HCNTL, DPI_REG[DPI_IDX(module)]->BG_HCNTL, BG_LEFT,
			      BG_W - BG_W / 4);
		DPI_OUTREGBIT(cmdq, struct DPI_REG_BG_VCNTL, DPI_REG[DPI_IDX(module)]->BG_VCNTL, BG_BOT,
			      BG_H / 4);
		DPI_OUTREGBIT(cmdq, struct DPI_REG_BG_VCNTL, DPI_REG[DPI_IDX(module)]->BG_VCNTL, BG_TOP,
			      BG_H - BG_H / 4);
		DPI_OUTREGBIT(cmdq, struct DPI_REG_CNTL, DPI_REG[DPI_IDX(module)]->CNTL, BG_EN, 1);
		DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->BG_COLOR, 0);
	}

	return DPI_STATUS_OK;
}

DPI_STATUS ddp_dpi_ConfigSize(DISP_MODULE_ENUM module, cmdqRecHandle cmdq, uint32_t width,
			      uint32_t height)
{
	struct DPI_REG_SIZE size = DPI_REG[DPI_IDX(module)]->SIZE;

	size.WIDTH = width;
	size.HEIGHT = height;
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->SIZE, AS_UINT32(&size));

	return DPI_STATUS_OK;
}

DPI_STATUS ddp_dpi_EnableColorBar(DISP_MODULE_ENUM module)
{
	/*enable internal pattern - color bar */
	if (module == DISP_MODULE_DPI0)
		DPI_OUTREG32(0, DISPSYS_DPI0_BASE + 0xF00, 0x41);
	else
		DPI_OUTREG32(0, DISPSYS_DPI1_BASE + 0xF00, 0x41);

	return DPI_STATUS_OK;
}

DPI_STATUS ddp_dpi_yuv422_setting(DISP_MODULE_ENUM module, cmdqRecHandle cmdq, uint32_t uvsw)
{
	struct DPI_REG_YUV422_SETTING uvset = DPI_REG[DPI_IDX(module)]->YUV422_SETTING;

	uvset.UV_SWAP = uvsw;
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->YUV422_SETTING, AS_UINT32(&uvset));

	return DPI_STATUS_OK;
}

DPI_STATUS ddp_dpi_CLPFSetting(DISP_MODULE_ENUM module, cmdqRecHandle cmdq, uint8_t clpfType,
			       bool roundingEnable)
{
	struct DPI_REG_CLPF_SETTING setting = DPI_REG[DPI_IDX(module)]->CLPF_SETTING;

	setting.CLPF_TYPE = clpfType;
	setting.ROUND_EN = roundingEnable ? 1 : 0;
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->CLPF_SETTING, AS_UINT32(&setting));

	return DPI_STATUS_OK;
}

DPI_STATUS ddp_dpi_ConfigHDMI(DISP_MODULE_ENUM module, cmdqRecHandle cmdq, uint32_t yuv422en,
			      uint32_t rgb2yuven, uint32_t ydfpen, uint32_t r601sel,
			      uint32_t clpfen)
{
	struct DPI_REG_CNTL ctrl = DPI_REG[DPI_IDX(module)]->CNTL;

	ctrl.YUV422_EN = yuv422en;
	ctrl.RGB2YUV_EN = rgb2yuven;
	ctrl.TDFP_EN = ydfpen;
	ctrl.R601_SEL = r601sel;
	ctrl.CLPF_EN = clpfen;

	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->CNTL, AS_UINT32(&ctrl));

	return DPI_STATUS_OK;
}

/*
DPI_VIDEO_1920x1080i_50Hz
DPI_VIDEO_1920x1080i_60Hz
*/
DPI_STATUS ddp_dpi_ConfigVsync_LEVEN(DISP_MODULE_ENUM module, cmdqRecHandle cmdq,
				     uint32_t pulseWidth, uint32_t backPorch, uint32_t frontPorch,
				     bool fgInterlace)
{
	struct DPI_REG_TGEN_VWIDTH_LEVEN vwidth_leven = DPI_REG[DPI_IDX(module)]->TGEN_VWIDTH_LEVEN;
	struct DPI_REG_TGEN_VPORCH_LEVEN vporch_leven = DPI_REG[DPI_IDX(module)]->TGEN_VPORCH_LEVEN;
	struct DPI_REG_CNTL VS = DPI_REG[DPI_IDX(module)]->CNTL;

	vwidth_leven.VPW_LEVEN = pulseWidth;
	vwidth_leven.VPW_HALF_LEVEN = fgInterlace;
	vporch_leven.VBP_LEVEN = (fgInterlace ? (backPorch + 1) : backPorch);
	vporch_leven.VFP_LEVEN = frontPorch;

	VS.INTL_EN = fgInterlace;
	VS.VS_LEVEN_EN = fgInterlace;
	VS.VS_LODD_EN = 1;
	VS.FAKE_DE_RODD = fgInterlace;

	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->TGEN_VWIDTH_LEVEN, AS_UINT32(&vwidth_leven));
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->TGEN_VPORCH_LEVEN, AS_UINT32(&vporch_leven));
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->CNTL, AS_UINT32(&VS));

	return DPI_STATUS_OK;
}

DPI_STATUS ddp_dpi_ConfigVsync_REVEN(DISP_MODULE_ENUM module, cmdqRecHandle cmdq,
				     uint32_t pulseWidth, uint32_t backPorch, uint32_t frontPorch,
				     bool fgInterlace)
{
	struct DPI_REG_TGEN_VWIDTH_REVEN vwidth_reven = DPI_REG[DPI_IDX(module)]->TGEN_VWIDTH_REVEN;
	struct DPI_REG_TGEN_VPORCH_REVEN vporch_reven = DPI_REG[DPI_IDX(module)]->TGEN_VPORCH_REVEN;

	vwidth_reven.VPW_REVEN = pulseWidth;
	vwidth_reven.VPW_HALF_REVEN = fgInterlace;
	vporch_reven.VBP_REVEN = backPorch;
	vporch_reven.VFP_REVEN = frontPorch;

	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->TGEN_VWIDTH_REVEN, AS_UINT32(&vwidth_reven));
	DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->TGEN_VPORCH_REVEN, AS_UINT32(&vporch_reven));

	return DPI_STATUS_OK;
}


int Is_interlace_resolution(uint32_t resolution)
{
	if ((resolution == DPI_VIDEO_1920x1080i_60Hz) || (resolution == DPI_VIDEO_1920x1080i_50Hz))
		return true;
	else
		return false;
}

int LVDSTX_IsEnabled(DISP_MODULE_ENUM module, cmdqRecHandle cmdq)
{

	/*int i = 0; */
	int ret = 0;

	if (LVDS_TX_REG != NULL) {
		if ((LVDS_TX_REG->LVDS_OUT_CTRL.LVDS_EN) && (module == DISP_MODULE_DPI0))
			ret++;
	}
	DISPMSG("LVDSTX for DPI0 is %s\n", ret ? "on" : "off");
	return ret;
}

int ddp_dpi_reset(DISP_MODULE_ENUM module, void *cmdq_handle)
{
	pr_warn("DISP/DPI,ddp_dpi_reset\n");

	DPI_OUTREGBIT(cmdq_handle, struct DPI_REG_RST, DPI_REG[DPI_IDX(module)]->DPI_RST, RST, 1);
	DPI_OUTREGBIT(cmdq_handle, struct DPI_REG_RST, DPI_REG[DPI_IDX(module)]->DPI_RST, RST, 0);

	return 0;
}

int ddp_dpi_start(DISP_MODULE_ENUM module, void *cmdq)
{
	return 0;
}

int ddp_dpi_trigger(DISP_MODULE_ENUM module, void *cmdq)
{
	if (s_isDpiStart == false) {
		pr_warn("DISP/DPI,ddp_dpi_start\n");
		ddp_dpi_reset(module, cmdq);
		/*enable DPI */
		DPI_OUTREG32(cmdq, &DPI_REG[DPI_IDX(module)]->DPI_EN, 0x00000001);

		s_isDpiStart = true;
	}
	return 0;
}

int ddp_dpi_stop(DISP_MODULE_ENUM module, void *cmdq_handle)
{
	pr_warn("DISP/DPI,ddp_dpi_stop\n");

	/*disable DPI and background, and reset DPI */
	DPI_OUTREG32(cmdq_handle, &DPI_REG[DPI_IDX(module)]->DPI_EN, 0x00000000);
	ddp_dpi_ConfigBG(module, cmdq_handle, false, 0, 0);
	ddp_dpi_reset(module, cmdq_handle);

	s_isDpiStart = false;
	if (module == DISP_MODULE_DPI0)
		s_isDpiConfig = false;

	if (module == DISP_MODULE_DPI1)
		s_isDpi1Config = false;
	dpi_vsync_irq_count[0] = 0;
	dpi_vsync_irq_count[1] = 0;
	dpi_undflow_irq_count[0] = 0;
	dpi_undflow_irq_count[1] = 0;

	return 0;
}

int ddp_dpi_is_busy(DISP_MODULE_ENUM module)
{
	unsigned int status = INREG32(&DPI_REG[DPI_IDX(module)]->STATUS);

	return (status & (0x1 << 16) ? 1 : 0);
}

int ddp_dpi_is_idle(DISP_MODULE_ENUM module)
{
	return !ddp_dpi_is_busy(module);
}

unsigned int ddp_dpi_get_cur_addr(bool rdma_mode, int layerid)
{
	uint32_t i;

	if (rdma_mode)
		i = (INREG32(DISP_REG_RDMA_MEM_START_ADDR + DISP_INDEX_OFFSET * 2));
	else
		i = (INREG32(DISP_INDEX_OFFSET + DISP_REG_OVL_L0_ADDR + layerid * 0x20));
	return i;
}


#if ENABLE_DPI_INTERRUPT
static irqreturn_t _DPI_InterruptHandler(DISP_MODULE_ENUM module, unsigned int param)
{
	static int counter;
	struct DPI_REG_INTERRUPT status = DPI_REG[DPI_IDX(module)]->INT_STATUS;

	if (status.VSYNC) {
		dpi_vsync_irq_count[DPI_IDX(module)]++;
		if (dpi_vsync_irq_count[DPI_IDX(module)] > 30) {
			/* pr_debug("dpi vsync %d\n", dpi_vsync_irq_count[DPI_IDX(module)]); */
			dpi_vsync_irq_count[DPI_IDX(module)] = 0;
		}

		if (counter) {
			pr_err("DISP/DPI error DPI FIFO is empty,received %d times interrupt !!!\n", counter);
			counter = 0;
		}
	}

	DPI_OUTREG32(0, &DPI_REG[DPI_IDX(module)]->INT_STATUS, 0);

	return IRQ_HANDLED;
}
#endif

int ddp_dpi_init(DISP_MODULE_ENUM module, void *cmdq)
{
	unsigned int i;

	pr_warn("DISP/DPI,ddp_dpi_init- %p\n", cmdq);

/* #ifdef CONFIG_FPGA_EARLY_PORTING */
	/* DPI_OUTREG32(0, DISPSYS_DPI0_BASE, 0x1); */
	/* DPI_OUTREG32(0, DISPSYS_DPI0_BASE + 0xE0, 0x404); */
/* #endif */

	DPI_REG[0] = (struct DPI_REGS *) (DISPSYS_DPI0_BASE);
	DPI_REG[1] = (struct DPI_REGS *) (DISPSYS_DPI1_BASE);
	LVDS_TX_REG = (struct LVDS_TX_REGS *) (DISPSYS_LVDS_TX_BASE);
	LVDS_ANA_REG = (struct LVDS_ANA_REGS *) (DISPSYS_LVDS_ANA_BASE);

	ddp_dpi_power_on(module, cmdq);

#if ENABLE_DPI_INTERRUPT
	for (i = 0; i < DPI_INTERFACE_NUM; i++)
		DPI_OUTREGBIT(cmdq, struct DPI_REG_INTERRUPT, DPI_REG[i]->INT_ENABLE, VSYNC, 1);

	disp_register_module_irq_callback(module, _DPI_InterruptHandler);

#endif
	for (i = 0; i < DPI_INTERFACE_NUM; i++)
		DISPCHECK("dpi%d init finished\n", i);

	return 0;
}

int ddp_dpi_deinit(DISP_MODULE_ENUM module, void *cmdq_handle)
{
	pr_warn("DISP/DPI,ddp_dpi_deinit- %p\n", cmdq_handle);

	ddp_dpi_stop(module, cmdq_handle);
	ddp_dpi_power_off(module, cmdq_handle);

	return 0;
}

int ddp_dpi_set_lcm_utils(DISP_MODULE_ENUM module, LCM_DRIVER *lcm_drv)
{
	/*DISPFUNC(); */
	LCM_UTIL_FUNCS *utils = NULL;

	if (lcm_drv == NULL) {
		pr_err("DISP/DPI,lcm_drv is null!\n");
		return -1;
	}

	utils = &lcm_utils_dpi;

	utils->set_reset_pin = lcm_set_reset_pin;
	utils->udelay = lcm_udelay;
	utils->mdelay = lcm_mdelay;
	utils->send_cmd = lcm_send_cmd,
	    utils->send_data = lcm_send_data, lcm_drv->set_util_funcs(utils);

	return 0;
}

int ddp_dpi_build_cmdq(DISP_MODULE_ENUM module, void *cmdq_trigger_handle, CMDQ_STATE state)
{
	return 0;
}

int ddp_dpi_dump(DISP_MODULE_ENUM module, int level)
{
	uint32_t i;

	DDPDUMP("---------- Start dump DPI registers ----------\n");

	for (i = 0; i <= 0x40; i += 4)
		DDPDUMP("DPI+%04x : 0x%08x\n", i, INREG32(DISPSYS_DPI0_BASE + i));

	for (i = 0x68; i <= 0x7C; i += 4)
		DDPDUMP("DPI+%04x : 0x%08x\n", i, INREG32(DISPSYS_DPI0_BASE + i));

	DDPDUMP("DPI+Color Bar : %04x : 0x%08x\n", 0xF00, INREG32(DISPSYS_DPI0_BASE + 0xF00));
#if 0
	DDPDUMP("DPI Addr IO Driving : 0x%08x\n", INREG32(DISPSYS_IO_DRIVING));

	DDPDUMP("DPI TVDPLL CON0 : 0x%08x\n", INREG32(DDP_REG_TVDPLL_CON0));
	DDPDUMP("DPI TVDPLL CON1 : 0x%08x\n", INREG32(DDP_REG_TVDPLL_CON1));

	DDPDUMP("DPI TVDPLL CON6 : 0x%08x\n", INREG32(DDP_REG_TVDPLL_CON6));
	DDPDUMP("DPI MMSYS_CG_CON1:0x%08x\n", INREG32(DISP_REG_CONFIG_MMSYS_CG_CON1));
#endif

	return 0;
}

int ddp_dpi_ioctl(DISP_MODULE_ENUM module, void *cmdq_handle, unsigned int ioctl_cmd,
		  unsigned long *params)
{

	int ret = 0;
	DDP_IOCTL_NAME ioctl = (DDP_IOCTL_NAME) ioctl_cmd;

	pr_debug("DISP/DPI,DPI ioctl: %d\n", ioctl);
	DISPFUNC();

	switch (ioctl) {
	case DDP_DPI_FACTORY_TEST:
		{
			disp_ddp_path_config *config_info = (disp_ddp_path_config *) params;

			ddp_dpi_power_on(module, NULL);
			ddp_dpi_stop(module, NULL);
			ddp_dpi_config(module, config_info, NULL);
			ddp_dpi_EnableColorBar(module);

			ddp_dpi_trigger(module, NULL);
			ddp_dpi_start(module, NULL);
			ddp_dpi_dump(module, 1);
			break;
		}
	default:
		break;
	}

	return ret;
}

void LVDS_PLL_Init(DISP_MODULE_ENUM module, void *cmdq_handle, uint32_t PLL_CLK, uint32_t SSC_range,
		   bool SSC_disable)
{
	unsigned int pixel_clock = 0, ck_div = 0, postdiv = 0;
	unsigned int pcw = 0, n_info = 0;	/*delta1 = 5, pdelta1 = 0; */
	unsigned long bclk = 0;
	struct clk *clksrc = NULL;

	pixel_clock = PLL_CLK * 2;
	if (pixel_clock > 250) {
		DISPCHECK("lvds pll clock exceed limitation(%d)\n", pixel_clock);
		ASSERT(0);
	} else if (pixel_clock >= 125) {
		ck_div = 1;
		postdiv = 8;
	} else if (pixel_clock >= 63) {
		ck_div = 2;
		postdiv = 8;
	} else if (pixel_clock >= 32) {
		ck_div = 3;
		postdiv = 8;
	} else if (pixel_clock > 16) {
		ck_div = 4;
		postdiv = 8;
	} else if (pixel_clock >= 8) {
		ck_div = 1;
		postdiv = 16;
	} else {
		DISPCHECK("lvds pll clock exceed limitation(%d)\n", pixel_clock);
		ASSERT(0);
	}

	n_info = pixel_clock * ck_div * postdiv / 26;
	pcw = ((n_info * (1 << 14)) | (1 << 31));
	switch (postdiv) {
	case 1:
		postdiv = 0;
		break;
	case 2:
		postdiv = 1;
		break;
	case 4:
		postdiv = 2;
		break;
	default:
	case 8:
		postdiv = 3;
		break;
	case 16:
		postdiv = 4;
		break;
	}
	pr_warn("DPI0 LVDSPLL_init: pixel_clock = %d,postdiv = 0x%x, pcw = 0x%x, ck_div = 0x%x\n",
		  pixel_clock, postdiv, pcw, ck_div);
#if 0
#ifdef CONFIG_MTK_CLKMGR
	OUTREG32(clk_apmixed_base + 0x2c0, (1 << 8) | (postdiv << 4) | (1 << 0));	/*LVDSPLL_CON0 */
	OUTREG32(clk_apmixed_base + 0x2c4, pcw);	/*LVDSPLL_CON1 */

	clkmux_sel(MT_MUX_DPI0, ck_div, "dpi0");	/*CLK_CFG_6  bit0~2 */
#endif
#else
	bclk = pixel_clock * ck_div * 1000000 * 2;
	if (ck_div == 1)
		clksrc = ddp_clk_map[TOP_LVDSPLL_D2];
	else if (ck_div == 2)
		clksrc = ddp_clk_map[TOP_LVDSPLL_D2];
	else if (ck_div == 3)
		clksrc = ddp_clk_map[TOP_LVDSPLL_D8];
	else if (ck_div == 4)
		clksrc = ddp_clk_map[TOP_LVDSPLL_D8];

	clk_prepare_enable(ddp_clk_map[MM_CLK_MUX_DPI0_SEL]);
	clk_set_parent(ddp_clk_map[MM_CLK_MUX_DPI0_SEL], clksrc);
	clk_disable_unprepare(ddp_clk_map[MM_CLK_MUX_DPI0_SEL]);

	clk_prepare_enable(ddp_clk_map[APMIXED_LVDSPLL]);
	clk_set_rate(ddp_clk_map[APMIXED_LVDSPLL], bclk);
	clk_disable_unprepare(ddp_clk_map[APMIXED_LVDSPLL]);
#endif
	/*LVDS PLL SSC, 30k, range -8~0,default: 5 */
	/*if (1 != SSC_disable) {
	   OUTREG32(clk_apmixed_base + 0x2d0, (1 << 17) | 0x1B1);
	   if (0 != SSC_range)
	   delta1 = SSC_range;
	   ASSERT(delta1 <= 8);
	   pdelta1 = (delta1*n_info*12*262144)/260000;
	   OUTREG32(clk_apmixed_base + 0x2d4, (pdelta1 << 16) | pdelta1);
	   OUTREG32(clk_apmixed_base + 0x2d0, (1 << 17) | (1 << 16) | 0x1B1);
	   DISPCHECK("DPI0 LVDSPLL_SSC enable: delta1 = %d,pdelta1 = 0x%x\n", delta1, pdelta1);
	   } else {
	   OUTREG32(clk_apmixed_base + 0x2d0, (0 << 16));
	   DISPCHECK("DPI0 LVDSPLL_SSC disable: delta1 = %d,pdelta1 = 0x%x\n", delta1, pdelta1);
	   } */
}

void LVDS_ANA_Init(DISP_MODULE_ENUM module, void *cmdq_handle)
{
	DPI_OUTREG32(cmdq_handle, DDP_REG_LVDS_ANA + 0x18, 0x00203580);
	DPI_OUTREG32(cmdq_handle, DDP_REG_LVDS_ANA + 0x14, 0x0010e040);
	DPI_OUTREG32(cmdq_handle, DDP_REG_LVDS_ANA + 0x14, 0x0010e042);
	DPI_OUTREG32(cmdq_handle, DDP_REG_LVDS_ANA + 0x04, 0x00c10fb7);
	lcm_udelay(20);

	DPI_OUTREG32(cmdq_handle, DDP_REG_LVDS_ANA + 0x0c, 0x00000040);
	DPI_OUTREG32(cmdq_handle, DDP_REG_LVDS_ANA + 0x08, 0x00007fe0);
	lcm_udelay(20);
	/*clear mipi influence register */
	MASKREG32(MIPI_TX_REG_BASE + 0x40, (1 << 11), (0 << 11));
	pr_warn("LVDS_ANA_init finished\n");
}

void LVDS_DIG_RST(DISP_MODULE_ENUM module, void *cmdq_handle)
{
	DPI_OUTREG32(cmdq_handle, DDP_REG_LVDS_TX + 0x34, 0x00000000);
	DPI_OUTREG32(cmdq_handle, DDP_REG_LVDS_TX + 0x34, 0x00000003);
}

void LVDS_DIG_Init(DISP_MODULE_ENUM module, void *cmdq_handle)
{
	DPI_OUTREG32(cmdq_handle, DDP_REG_LVDS_TX + 0x18, 0x00000001);
	DPI_OUTREG32(cmdq_handle, DDP_REG_LVDS_TX + 0x20, 0x00000007);
	DPI_OUTREG32(cmdq_handle, DDP_REG_BASE_MMSYS_CONFIG + 0x90c, 0x00010000);	/*enable LVDS fifo out */
	LVDS_DIG_RST(module, cmdq_handle);	/*ADD for suspend&resume blurred screen issue */
	pr_warn("LVDS_DIG_init finished\n");

#if 0
	/* pattern enable for 800 x 1280 */
	DPI_OUTREGBIT(cmdq, LVDS_REG_RGTST_PAT, LVDS_TX_REG->LVDS_RGTST_PAT, RG_TST_PATN_EN, 0x01);
	DPI_OUTREGBIT(cmdq, LVDS_REG_RGTST_PAT, LVDS_TX_REG->LVDS_RGTST_PAT, RG_TST_PATN_TYPE,
		      0x02);
	/* pattern width */
	DPI_OUTREGBIT(cmdq, LVDS_REG_PATN_TOTAL, LVDS_TX_REG->LVDS_PATN_TOTAL, RG_PTGEN_HTOTAL,
		      0x360);
	DPI_OUTREGBIT(cmdq, LVDS_REG_PATN_ACTIVE, LVDS_TX_REG->LVDS_PATN_ACTIVE, RG_PTGEN_HACTIVE,
		      0x320);
	/* pattern height */
	DPI_OUTREGBIT(cmdq, LVDS_REG_PATN_TOTAL, LVDS_TX_REG->LVDS_PATN_TOTAL, RG_PTGEN_VTOTAL,
		      0x508);
	DPI_OUTREGBIT(cmdq, LVDS_REG_PATN_ACTIVE, LVDS_TX_REG->LVDS_PATN_ACTIVE, RG_PTGEN_VACTIVE,
		      0x500);
	/* pattern start */
	DPI_OUTREGBIT(cmdq, LVDS_REG_PATN_START, LVDS_TX_REG->LVDS_PATN_START, RG_PTGEN_VSTART,
		      0x08);
	DPI_OUTREGBIT(cmdq, LVDS_REG_PATN_START, LVDS_TX_REG->LVDS_PATN_START, RG_PTGEN_HSTART,
		      0x20);
	/* pattern sync width */
	DPI_OUTREGBIT(cmdq, LVDS_REG_PATN_WIDTH, LVDS_TX_REG->LVDS_PATN_WIDTH, RG_PTGEN_VWIDTH,
		      0x01);
	DPI_OUTREGBIT(cmdq, LVDS_REG_PATN_WIDTH, LVDS_TX_REG->LVDS_PATN_WIDTH, RG_PTGEN_HWIDTH,
		      0x01);
#endif
}

void ddp_dpi_lvds_config(DISP_MODULE_ENUM module, LCM_DPI_FORMAT format, void *cmdq_handle)
{
	LVDS_ANA_Init(module, cmdq_handle);
	LVDS_DIG_Init(module, cmdq_handle);
	if (format == LCM_DPI_FORMAT_RGB666)
		DPI_OUTREG32(cmdq_handle, DDP_REG_LVDS_TX, 0x00000010);
	pr_warn("LVDS_config finished\n");
}

void ddp_dpi_RGB_config(DISP_MODULE_ENUM module, void *cmdq_handle)
{
	DPI_OUTREG32(cmdq_handle, DDP_REG_BASE_MMSYS_CONFIG + 0x90c, 0x00000001);	/*enable RGB out */
	pr_warn("RGB config finished\n");
}

void ddp_lvds_power_off(DISP_MODULE_ENUM module, void *cmdq_handle)
{
	DPI_OUTREGBIT(cmdq_handle, struct LVDS_ANA_REG_CTL2, LVDS_ANA_REG->LVDSTX_ANA_CTL2,
		      LVDSTX_ANA_LDO_EN, 0x0);
	DPI_OUTREGBIT(cmdq_handle, struct LVDS_ANA_REG_CTL2, LVDS_ANA_REG->LVDSTX_ANA_CTL2,
		      LVDSTX_ANA_BIAS_EN, 0x0);
	DPI_OUTREGBIT(cmdq_handle, struct LVDS_VPLL_REG_CTL2, LVDS_ANA_REG->LVDS_VPLL_CTL2, LVDS_VPLL_EN,
		      0x0);
	DPI_OUTREGBIT(cmdq_handle, struct LVDS_ANA_REG_CTL3, LVDS_ANA_REG->LVDSTX_ANA_CTL3,
		      LVDSTX_ANA_EXT_EN, 0x00);
	DPI_OUTREGBIT(cmdq_handle, struct LVDS_ANA_REG_CTL3, LVDS_ANA_REG->LVDSTX_ANA_CTL3,
		      LVDSTX_ANA_DRV_EN, 0x00);
	DPI_OUTREG32(cmdq_handle, DDP_REG_LVDS_TX + 0x18, 0x00000000);
	DPI_OUTREG32(cmdq_handle, DDP_REG_LVDS_TX + 0x20, 0x00000000);
	pr_warn("LVDS_power_off finished\n");
}

int ddp_dpi_config(DISP_MODULE_ENUM module, disp_ddp_path_config *config, void *cmdq_handle)
{
	if (((s_isDpiConfig == false) && (module == DISP_MODULE_DPI0)) || ((s_isDpi1Config == false)
									   && (module ==
									       DISP_MODULE_DPI1))) {
		LCM_DPI_PARAMS *dpi_config = &(config->dispif_config.dpi);

		pr_warn("DISP/DPI,ddp_dpi_config DPI status:%x, cmdq:%p\n",
			INREG32(&DPI_REG[DPI_IDX(module)]->STATUS), cmdq_handle);
		ddp_dpi_ConfigCLK(module, cmdq_handle, dpi_config->clk_pol, dpi_config->lvds_tx_en);
		ddp_dpi_ConfigSize(module, cmdq_handle, dpi_config->width, dpi_config->height);
		ddp_dpi_ConfigBG(module, cmdq_handle, true, dpi_config->bg_width,
				 dpi_config->bg_height);
		ddp_dpi_ConfigDE(module, cmdq_handle, dpi_config->de_pol);
		ddp_dpi_ConfigVsync(module, cmdq_handle, dpi_config->vsync_pol,
				    dpi_config->vsync_pulse_width, dpi_config->vsync_back_porch,
				    dpi_config->vsync_front_porch);
		ddp_dpi_ConfigHsync(module, cmdq_handle, dpi_config->hsync_pol,
				    dpi_config->hsync_pulse_width, dpi_config->hsync_back_porch,
				    dpi_config->hsync_front_porch);
		ddp_dpi_ConfigDualEdge(module, cmdq_handle, dpi_config->i2x_en,
				       dpi_config->i2x_edge);
		if (module == DISP_MODULE_DPI0) {

			LVDS_PLL_Init(module, NULL, dpi_config->PLL_CLOCK, dpi_config->ssc_range,
				      dpi_config->ssc_disable);
			ddp_dpi_ConfigCLK(module, NULL, dpi_config->clk_pol,
					  dpi_config->lvds_tx_en);
			if (dpi_config->lvds_tx_en)
				ddp_dpi_lvds_config(module, dpi_config->format, NULL);
			else
				ddp_dpi_RGB_config(module, NULL);
			s_isDpiConfig = true;
		}
		if (module == DISP_MODULE_DPI1) {
			ddp_dpi_ConfigPclk(module, cmdq_handle, dpi_config->dpi_clock,
					   dpi_config->clk_pol);
			ddp_dpi_ConfigSize(module, cmdq_handle, dpi_config->width,
					   Is_interlace_resolution(dpi_config->
								   dpi_clock) ? dpi_config->height /
					   2 : dpi_config->height);
			ddp_dpi_ConfigVsync_LEVEN(module, cmdq_handle,
						  dpi_config->vsync_pulse_width,
						  dpi_config->vsync_back_porch,
						  dpi_config->vsync_front_porch,
						  Is_interlace_resolution(dpi_config->dpi_clock));
			s_isDpi1Config = true;
		}
		pr_warn("DISP/DPI,ddp_dpi_config done\n");
	}

	return 0;
}

int ddp_dpi_power_on(DISP_MODULE_ENUM module, void *cmdq_handle)
{
	int ret = 0;

	pr_warn("DISP/DPI, ddp_dpi_power_on, s_isDpiPowerOn %d, s_isDpi1PowerOn %d\n",
		s_isDpiPowerOn, s_isDpi1PowerOn);
	if ((!s_isDpiPowerOn) && (module == DISP_MODULE_DPI0)) {
#if 0
#ifdef CONFIG_MTK_CLKMGR
		enable_mux(MT_MUX_DPI0, "dpi0");
		ret += enable_pll(LVDSPLL, "DPI0");
		ret += enable_clock(MT_CG_DISP1_DPI_PIXEL, "DPI0");
		ret += enable_clock(MT_CG_DISP1_DPI_ENGINE, "DPI0");
		ret += enable_clock(MT_CG_DISP1_LVDS_PIXEL, "LVDS");
		ret += enable_clock(MT_CG_DISP1_LVDS_CTS, "LVDS");
#endif
#else
		/*ret += clk_prepare(ddp_clk_map[MM_CLK_MUX_DPI0_SEL]);*/
		/*ret += ddp_module_clock_enable(APMIXED_LVDSPLL, true);*/
		ret += ddp_module_clock_enable(MM_CLK_DPI0_PIXEL, true);
		ret += ddp_module_clock_enable(MM_CLK_DPI0_ENGINE, true);
		ret += ddp_module_clock_enable(MM_CLK_LVDS_PIXEL, true);
		ret += ddp_module_clock_enable(MM_CLK_LVDS_CTS, true);



#endif
		s_isDpiPowerOn = true;
	}
	if ((!s_isDpi1PowerOn) && (module == DISP_MODULE_DPI1)) {
#if 0
#ifdef CONFIG_MTK_CLKMGR
		ret += enable_clock(MT_CG_DISP1_DPI1_PIXEL, "DPI");
		ret += enable_clock(MT_CG_DISP1_DPI1_ENGINE, "DPI");
		ret += enable_clock(MT_CG_DISP1_HDMI_PIXEL, "DPI");
		ret += enable_clock(MT_CG_DISP1_HDMI_PLLCK, "DPI");
		ret += enable_clock(MT_CG_DISP1_HDMI_ADSP, "DPI");
		ret += enable_clock(MT_CG_DISP1_HDMI_SPDIF, "DPI");
#endif
#else
		ret += ddp_module_clock_enable(MM_CLK_DPI1_PIXEL, true);
		ret += ddp_module_clock_enable(MM_CLK_DPI1_ENGINE, true);
#ifdef CONFIG_MTK_INTERNAL_HDMI_SUPPORT
		hdmi_clock_enable(true);
#endif
#endif
		s_isDpi1PowerOn = true;
	}
	if (ret > 0)
		pr_err("DISP/DPI,power manager API return FALSE\n");
	return 0;
}

int ddp_dpi_power_off(DISP_MODULE_ENUM module, void *cmdq_handle)
{
	int ret = 0;

	pr_warn("DISP/DPI, ddp_dpi_power_off, s_isDpiPowerOn %d, s_isDpi1PowerOn %d\n",
		s_isDpiPowerOn, s_isDpi1PowerOn);
	if ((s_isDpiPowerOn) && (module == DISP_MODULE_DPI0)) {
		ddp_lvds_power_off(module, cmdq_handle);
#if 0
#ifdef CONFIG_MTK_CLKMGR
		ret += disable_clock(MT_CG_DISP1_DPI_PIXEL, "DPI");
		ret += disable_clock(MT_CG_DISP1_DPI_ENGINE, "DPI");
		ret += disable_clock(MT_CG_DISP1_LVDS_PIXEL, "DPI");
		ret += disable_clock(MT_CG_DISP1_LVDS_CTS, "DPI");
		disable_mux(MT_MUX_DPI0, "dpi0");
		ret += disable_pll(LVDSPLL, "DPI0");
#endif
#else
		ret += ddp_module_clock_enable(MM_CLK_DPI0_PIXEL, false);
		ret += ddp_module_clock_enable(MM_CLK_DPI0_ENGINE, false);
		ret += ddp_module_clock_enable(MM_CLK_LVDS_PIXEL, false);
		ret += ddp_module_clock_enable(MM_CLK_LVDS_CTS, false);
		/*clk_unprepare(ddp_clk_map[MM_CLK_MUX_DPI0_SEL]);
		ret += ddp_module_clock_enable(APMIXED_LVDSPLL, false);*/
#endif
		s_isDpiPowerOn = false;
	}
	if ((s_isDpi1PowerOn) && (module == DISP_MODULE_DPI1)) {
#if 0
#ifdef CONFIG_MTK_CLKMGR

		ret += disable_clock(MT_CG_DISP1_DPI1_PIXEL, "DPI");
		ret += disable_clock(MT_CG_DISP1_DPI1_ENGINE, "DPI");
		ret += disable_clock(MT_CG_DISP1_HDMI_PIXEL, "DPI");
		ret += disable_clock(MT_CG_DISP1_HDMI_PLLCK, "DPI");
		ret += disable_clock(MT_CG_DISP1_HDMI_ADSP, "DPI");
		ret += disable_clock(MT_CG_DISP1_HDMI_SPDIF, "DPI");
#endif
#else
		ret += ddp_module_clock_enable(MM_CLK_DPI1_PIXEL, false);
		ret += ddp_module_clock_enable(MM_CLK_DPI1_ENGINE, false);
#ifdef CONFIG_MTK_INTERNAL_HDMI_SUPPORT
		hdmi_clock_enable(false);
#endif
#endif
		s_isDpi1PowerOn = false;
	}
	if (ret > 0)
		pr_err("DISP/DPI,power manager API return FALSE\n");
	return 0;
}

bool ddp_dpi_is_top_filed(DISP_MODULE_ENUM module)
{
	if (module == DISP_MODULE_DPI1) {
		if (DPI_REG[DPI_IDX(module)]->STATUS.FIELD == 0)
			return true;
	}

	return false;
}


DDP_MODULE_DRIVER ddp_driver_dpi0 = {
	.module = DISP_MODULE_DPI0,
	.init = ddp_dpi_init,
	.deinit = ddp_dpi_deinit,
	.config = ddp_dpi_config,
	.build_cmdq = ddp_dpi_build_cmdq,
	.trigger = ddp_dpi_trigger,
	.start = ddp_dpi_start,
	.stop = ddp_dpi_stop,
	.reset = ddp_dpi_reset,
	.power_on = ddp_dpi_power_on,
	.power_off = ddp_dpi_power_off,
	.is_idle = ddp_dpi_is_idle,
	.is_busy = ddp_dpi_is_busy,
	.dump_info = ddp_dpi_dump,
	.set_lcm_utils = ddp_dpi_set_lcm_utils,
	.ioctl = ddp_dpi_ioctl
};

DDP_MODULE_DRIVER ddp_driver_dpi1 = {
	.module = DISP_MODULE_DPI1,
	.init = ddp_dpi_init,
	.deinit = ddp_dpi_deinit,
	.config = ddp_dpi_config,
	.build_cmdq = ddp_dpi_build_cmdq,
	.trigger = ddp_dpi_trigger,
	.start = ddp_dpi_start,
	.stop = ddp_dpi_stop,
	.reset = ddp_dpi_reset,
	.power_on = ddp_dpi_power_on,
	.power_off = ddp_dpi_power_off,
	.is_idle = ddp_dpi_is_idle,
	.is_busy = ddp_dpi_is_busy,
	.dump_info = ddp_dpi_dump,
	.set_lcm_utils = ddp_dpi_set_lcm_utils,
	.ioctl = ddp_dpi_ioctl
};
