#ifndef __DDP_DEBUG_H__
#define __DDP_DEBUG_H__

#include <linux/kernel.h>
#include "ddp_mmp.h"
#include "ddp_dump.h"
#include "ddp_path.h"

extern unsigned int gDDPError;
extern unsigned int g_mobilelog;
extern int _display_set_lcm_refresh_rate(int fps);

extern int disp_create_session(disp_session_config *config);
extern int disp_destroy_session(disp_session_config *config);

extern OVL_CONFIG_STRUCT cached_layer_config[DDP_OVL_LAYER_MUN];

#define DISP_ENABLE_SODI_FOR_VIDEO_MODE
void ddp_debug_init(void);
void ddp_debug_exit(void);

unsigned int ddp_debug_analysis_to_buffer(void);
unsigned int ddp_debug_dbg_log_level(void);
unsigned int ddp_debug_irq_log_level(void);

int ddp_mem_test(void);
int ddp_lcd_test(void);

#endif				/* __DDP_DEBUG_H__ */
