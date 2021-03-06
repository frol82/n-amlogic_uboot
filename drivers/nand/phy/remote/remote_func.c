#include <asm/ioctl.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <asm/irq.h>
#include <linux/io.h>
/*#include <mach/am_regs.h>*/
#include "remote_main.h"

#ifdef REMOTE_FIQ
#include <plat/fiq_bridge.h>
#else
#define fiq_bridge_pulse_trigger(x)
#endif
static DEFINE_SPINLOCK(remote_lock);

#ifdef CONFIG_AML_HDMI_TX
unsigned char cec_repeat = 10;
#endif
static const struct reg_s *remoteregsTab[] = {
	RDECODEMODE_NEC,
	RDECODEMODE_DUOKAN,
	RDECODEMODE_MITSUBISHI,
	RDECODEMODE_THOMSON,
	RDECODEMODE_TOSHIBA,
	RDECODEMODE_SONYSIRC,
	RDECODEMODE_RC5,
	RDECODEMODE_RESERVED,
	RDECODEMODE_RC6,
	RDECODEMODE_RCMM,
	RDECODEMODE_COMCAST,
	RDECODEMODE_SANYO,
	RDECODEMODE_SKIPLEADER,
	RDECODEMODE_SW,
	RDECODEMODE_NEC_RCA_2IN1,
	RDECODEMODE_NEC_TOSHIBA_2IN1,
	RDECODEMODE_NEC_RCMM_2IN1,
	RDECODEMODE_SW_NEC,
	NULL,
	RDECODEMODE_SW_DUOKAN
};

static int auto_repeat_count, repeat_count;
static void remote_rel_timer_sr(unsigned long data);
static void remote_repeat_sr(unsigned long data);
static void remote_rca_repeat_sr(unsigned long data);
static int dbg_printk(const char *fmt, ...)
{
	char buf[100];
	va_list args;

	va_start(args, fmt);
	vscnprintf(buf, 100, fmt, args);
	if (strlen(remote_log_buf) + (strlen(buf) + 64) > REMOTE_LOG_BUF_LEN)
		remote_log_buf[0] = '\0';
	strcat(remote_log_buf, buf);
	va_end(args);
	return 0;
}

int set_remote_mode(int mode)
{
	const struct reg_s *reg;
	reg = remoteregsTab[mode];
	while (CONFIG_END != reg->reg)
		setremotereg(reg++);
	input_dbg("%s[%d]\n", __func__, __LINE__);
	return 0;

}

void setremotereg(const struct reg_s *r)
{
	am_remote_write_reg(r->reg, r->val);
	pr_debug("[0x%lx] = 0x%x\n", (g_remote_ao_offset + ((r->reg) << 2)),
		r->val);
}

void config_sw_init_window(struct remote *remote_data)
{
	switch (remote_data->work_mode) {
	case DECODEMODE_SW_NEC:
		remote_data->bit_count = 32;
		remote_data->debug_enable = 1;
		remote_data->release_delay[remote_data->map_num] = 108;
		remote_data->repeat_enable = 0;
		remote_data->time_window[0] = 500;
		remote_data->time_window[1] = 700;
		remote_data->time_window[2] = 50;
		remote_data->time_window[3] = 80;
		remote_data->time_window[4] = 100;
		remote_data->time_window[5] = 130;
		remote_data->time_window[6] = 800;
		remote_data->time_window[7] = 900;
		break;
	case DECODEMODE_SW_DUOKAN:
		remote_data->bit_count = 20;
		remote_data->debug_enable = 1;
		remote_data->repeat_enable = 0;
		remote_data->time_window[0] = 79;
		remote_data->time_window[1] = 83;
		remote_data->time_window[2] = 54;
		remote_data->time_window[3] = 61;
		remote_data->time_window[4] = 70;
		remote_data->time_window[5] = 78;
		remote_data->time_window[6] = 256;
		remote_data->time_window[7] = 768;
		remote_data->time_window[8] = 84;
		remote_data->time_window[9] = 93;
		remote_data->time_window[10] = 99;
		remote_data->time_window[11] = 106;
		break;
	default:
		break;

	}

}

void kdb_send_key(struct input_dev *dev, unsigned int scancode,
		  unsigned int type, int event);

void set_remote_init(struct remote *remote_data)
{
	if (remote_data->work_mode <= DECODEMODE_MAX) {
		if (remote_data->work_mode > DECODEMODE_NEC) {
			if (remote_data->work_mode == DECODEMODE_NEC_RCA_2IN1)
				setup_timer(&remote_data->repeat_timer,
					    remote_rca_repeat_sr, 0);
			else
				setup_timer(&remote_data->repeat_timer,
					    remote_repeat_sr, 0);
			input_dbg("enter in sw repeat mode\n");
		}
		return;
	}
	config_sw_init_window(remote_data);
}

void changeduokandecodeorder(struct remote *remote_data)
{
	unsigned int scancode = remote_data->cur_lsbkeycode;
	remote_data->cur_lsbkeycode =
	    ((scancode & 0x3) << 18) | ((scancode & 0xc) << 14) |
	    ((scancode & 0x30) << 10) | ((scancode & 0xc0) << 6) |
	    ((scancode & 0x300) << 2) | ((scancode & 0xc00) >> 2) |
	    ((scancode & 0x3000) >> 6) | ((scancode & 0xc000) >> 10) |
	    ((scancode & 0x30000) >> 14) | ((scancode & 0xc0000) >> 18);
	if (remote_data->cur_lsbkeycode == 0x0003cccf)
		remote_data->cur_lsbkeycode =
		    ((remote_data->custom_code[0] & 0xff) << 12) | 0xa0;
}

void get_cur_scancode(struct remote *remote_data)
{
	int temp_cur_lsbkeycode = 0;
	if (remote_data->work_mode == DECODEMODE_SANYO) {
		remote_data->cur_lsbkeycode = am_remote_read_reg(FRAME_BODY);
		remote_data->cur_msbkeycode =
		    am_remote_read_reg(FRAME_BODY1) & 0x2ff;
	} else if (remote_data->work_mode == DECODEMODE_NEC_RCA_2IN1) {
		temp_cur_lsbkeycode = am_remote_read_reg(FRAME_BODY);
		if (temp_cur_lsbkeycode != 0) {	/*new */
			remote_data->temp_work_mode = DECODEMODE_RCA;
			remote_data->cur_lsbkeycode = temp_cur_lsbkeycode;
			temp_cur_lsbkeycode = 0;
		}
		if (am_remote_read_reg(DURATION_REG1_AND_STATUS - 0x40)
		    >> 3 & 0x1) {	/*old */
			temp_cur_lsbkeycode =
			    am_remote_read_reg(FRAME_BODY - 0x40);
			if (temp_cur_lsbkeycode != 0) {
				remote_data->temp_work_mode = DECODEMODE_NEC;
				remote_data->cur_lsbkeycode =
				    temp_cur_lsbkeycode;
				temp_cur_lsbkeycode = 0;
			}
		}
	} else if (remote_data->work_mode == DECODEMODE_NEC_TOSHIBA_2IN1) {
		temp_cur_lsbkeycode = am_remote_read_reg(FRAME_BODY);
		if (temp_cur_lsbkeycode != 0) {	/*new */
			remote_data->temp_work_mode = DECODEMODE_TOSHIBA;
			remote_data->cur_lsbkeycode = temp_cur_lsbkeycode;
			temp_cur_lsbkeycode = 0;
		}
		if (am_remote_read_reg(DURATION_REG1_AND_STATUS - 0x40)
		    >> 3 & 0x1) {	/*old */
			temp_cur_lsbkeycode =
			    am_remote_read_reg(FRAME_BODY - 0x40);
			if (temp_cur_lsbkeycode != 0) {
				remote_data->temp_work_mode = DECODEMODE_NEC;
				remote_data->cur_lsbkeycode =
				    temp_cur_lsbkeycode;
				temp_cur_lsbkeycode = 0;
			}
		}
	} else if (remote_data->work_mode == DECODEMODE_NEC_RCMM_2IN1) {
		if (am_remote_read_reg(DURATION_REG1_AND_STATUS)>>3&0x1) {
			/*new*/
			temp_cur_lsbkeycode = am_remote_read_reg(FRAME_BODY);
		if (temp_cur_lsbkeycode) {
			remote_data->temp_work_mode = DECODEMODE_RCMM;
			remote_data->cur_lsbkeycode = temp_cur_lsbkeycode;
			temp_cur_lsbkeycode = 0;
			}
		}
		if (am_remote_read_reg(DURATION_REG1_AND_STATUS-0x40)>>3&0x1) {
			/*old*/
			temp_cur_lsbkeycode =
			am_remote_read_reg(FRAME_BODY-0x40);
		if (temp_cur_lsbkeycode) {
			remote_data->temp_work_mode = DECODEMODE_NEC;
			remote_data->cur_lsbkeycode =  temp_cur_lsbkeycode;
			temp_cur_lsbkeycode = 0;
			}
		}

	} else if (remote_data->work_mode > DECODEMODE_MAX) {
		remote_data->cur_lsbkeycode = remote_data->cur_keycode;
		if (remote_data->work_mode == DECODEMODE_SW_DUOKAN)
			changeduokandecodeorder(remote_data);
	} else
		remote_data->cur_lsbkeycode = am_remote_read_reg(FRAME_BODY);

}

void get_cur_scanstatus(struct remote *remote_data)
{
	if (remote_data->work_mode == DECODEMODE_NEC_RCA_2IN1) {
		if (remote_data->temp_work_mode == DECODEMODE_RCA)
			remote_data->frame_status =
			    am_remote_read_reg(DURATION_REG1_AND_STATUS);
		if (remote_data->temp_work_mode == DECODEMODE_NEC)
			remote_data->frame_status =
			    am_remote_read_reg(DURATION_REG1_AND_STATUS - 0x40);

	} else if (remote_data->work_mode == DECODEMODE_NEC_TOSHIBA_2IN1) {
		if (remote_data->temp_work_mode == DECODEMODE_TOSHIBA) {
			remote_data->frame_status =
			    am_remote_read_reg(DURATION_REG1_AND_STATUS);
			if (remote_data->cur_lsbkeycode == 0x1
			    || remote_data->cur_lsbkeycode == 0x0) {
				remote_data->frame_status = 0x1;
				remote_data->cur_lsbkeycode = 0x0;
			}

		}
		if (remote_data->temp_work_mode == DECODEMODE_NEC)
			remote_data->frame_status =
			    am_remote_read_reg(DURATION_REG1_AND_STATUS - 0x40);
	} else if (remote_data->work_mode ==  DECODEMODE_NEC_RCMM_2IN1) {
		if (remote_data->temp_work_mode == DECODEMODE_RCMM) {
			remote_data->frame_status =
				am_remote_read_reg(DURATION_REG1_AND_STATUS);
			if (remote_data->cur_lsbkeycode == 0x1 ||
				remote_data->cur_lsbkeycode == 0x0) {
				remote_data->frame_status = 0x1;
				remote_data->cur_lsbkeycode =  0x0;
			}

		}
		if (remote_data->temp_work_mode == DECODEMODE_NEC) {
			remote_data->frame_status =
			am_remote_read_reg(DURATION_REG1_AND_STATUS-0x40);
		}
	} else
		remote_data->frame_status =
		    am_remote_read_reg(DURATION_REG1_AND_STATUS);

}

/*
   DECODEMODE_NEC = 0,
   DECODEMODE_SKIPLEADER,
   DECODEMODE_SW,
   DECODEMODE_MITSUBISHI,
   DECODEMODE_THOMSON,
   DECODEMODE_TOSHIBA,
   DECODEMODE_SONYSIRC,
   DECODEMODE_RC5,
   DECODEMODE_RESERVED,
   DECODEMODE_RC6,
   DECODEMODE_RCMM,
   DECODEMODE_DUOKAN,
   DECODEMODE_RESERVED,
   DECODEMODE_RESERVED,
   DECODEMODE_COMCAST,
   DECODEMODE_SANYO,
   DECODEMODE_MAX*/

unsigned int COMCAST_DOMAIN(struct remote *remote_data, int domain)
{
	return remote_data->cur_keycode & 0xff;
}

/*SANYO frame body
  Leader + 13bit Address + 13bit (~Address) + 8bit Data + 8bit (~Data)
 */
unsigned int SANYO_DOMAIN(struct remote *remote_data, int domain)
{
	if (domain) {
		remote_data->frame_mode = 0;
		return (remote_data->cur_lsbkeycode >> 8) & 0xff;
	} else {
		remote_data->frame_mode = 0;
		return ((remote_data->cur_lsbkeycode >> 29) & 0x7) |
		    ((remote_data->cur_msbkeycode << 3)
		     & 0x1fff);
	}
}

/*

 */
unsigned int RCMM_DOMAIN(struct remote *remote_data, int domain)
{
#if 1
	if (domain)
		return (remote_data->cur_lsbkeycode) & 0xff;
	else
		return (remote_data->cur_lsbkeycode>>16) & 0xffff;

#else
	if (domain) {
		if (((remote_data->cur_lsbkeycode >> 12) & 0xfff)) {
			switch ((remote_data->cur_lsbkeycode >> 20) & 0xf) {
			case 0x0:	/*OEM mode */
				remote_data->frame_mode = 0;
				return remote_data->cur_lsbkeycode & 0xff;
			case 0x1:	/*Extended Mouse mode */
				remote_data->frame_mode = 1;
				break;
			case 0x2:	/*Extended Keyboard mode */
				remote_data->frame_mode = 2;
				break;
			case 0x3:	/*Extended Game pad mode */
				remote_data->frame_mode = 3;
				break;
			}
			return remote_data->cur_lsbkeycode & 0xfffff;
		} else {
			switch ((remote_data->cur_lsbkeycode >> 10) & 0x3) {
			case 0x0:	/*OEM mode */
				remote_data->frame_mode = 0;
				return remote_data->cur_lsbkeycode & 0xff;
			case 0x1:	/*Extended Mouse mode */
				remote_data->frame_mode = 1;
				break;
			case 0x2:	/*Extended Keyboard mode */
				remote_data->frame_mode = 2;
				break;
			case 0x3:	/*Extended Game pad mode */
				remote_data->frame_mode = 3;
				break;
			}
			return remote_data->cur_lsbkeycode & 0xff;
		}
	} else {
		if (((remote_data->cur_lsbkeycode >> 12) & 0xfff)) {
			switch ((remote_data->cur_lsbkeycode >> 20) & 0xf) {
			case 0x0:	/*OEM mode */
				remote_data->frame_mode = 0;
				return remote_data->cur_lsbkeycode >> 12 & 0x3f;
			case 0x1:	/*Extended Mouse mode */
				remote_data->frame_mode = 1;
				break;
			case 0x2:	/*Extended Keyboard mode */
				remote_data->frame_mode = 2;
				break;
			case 0x3:	/*Extended Game pad mode */
				remote_data->frame_mode = 3;
				break;
			}
			return 0;
		} else {
			switch ((remote_data->cur_lsbkeycode >> 20) & 0xf) {
			case 0x0:	/*Extended mode */
				remote_data->frame_mode = 0;
			case 0x1:	/*Extended Mouse mode */
				remote_data->frame_mode = 1;
				break;
			case 0x2:	/*Extended Keyboard mode */
				remote_data->frame_mode = 2;
				break;
			case 0x3:	/*Extended Game pad mode */
				remote_data->frame_mode = 3;
				break;
			}
			return (remote_data->cur_lsbkeycode >> 8) & 0x3;
		}
	}
#endif
}

/*
   8 bit address and 8 bit command length
   Address and command are transmitted twice for reliability
   Pulse distance modulation
   Carrier frequency of 38kHz
   Bit time of 1.125ms or 2.25ms
   NEC frame body
   C15 ~ C8      C7 ~ C0    D15~D8      D7~D0
   Header    ~Custom code   Custom code    Data Code ~Data Code
 */
unsigned int NEC_DOMAIN(struct remote *remote_data, int domain)
{
	if (domain) {		/*D15 ~ D8 */
		return (remote_data->cur_lsbkeycode >> 16) & 0xff;
	} else {		/* C7 ~ C0 */
		return (remote_data->cur_lsbkeycode) & 0xffff;
	}
}

/*
   8 bit address and 8 bit command length
   Pulse distance modulation
   Carrier frequency of 38kHz
   Bit time of 1ms or 2ms
 */
unsigned int MITSUBISHI_DOMAIN(struct remote *remote_data, int domain)
{
	if (domain)
		return remote_data->cur_keycode & 0xff;
	else
		return (remote_data->cur_lsbkeycode >> 8) & 0xff;
}

unsigned int TOSHIBA_DOMAIN(struct remote *remote_data, int domain)
{
	if (domain)
		return (remote_data->cur_lsbkeycode >> 16) & 0xff;
	else
		return (remote_data->cur_lsbkeycode) & 0xffff;
}

/*
   Pulse width modulation
   Carrier frequency of 40kHz
   Bit time of 1.2ms or 0.6ms
   5-bit address and 7-bit command length (12-bit protocol)
 */

unsigned int SONYSIRC_DOMAIN(struct remote *remote_data, int domain)
{
	if (domain)
		return (remote_data->cur_lsbkeycode >> 5) & 0x7f;
	else
		return remote_data->cur_lsbkeycode & 0x1f;
}

unsigned int RC5_DOMAIN(struct remote *remote_data, int domain)
{
	if (domain)
		return (remote_data->cur_lsbkeycode >> 5) & 0x7f;
	else
		return remote_data->cur_lsbkeycode & 0x1f;

}

unsigned int RC6_DOMAIN(struct remote *remote_data, int domain)
{
	if (domain)
		return (remote_data->cur_lsbkeycode) & 0xff;
	else
		return (remote_data->cur_lsbkeycode >> 16) & 0xffff;

}

unsigned int RCA_DOMAIN(struct remote *remote_data, int domain)
{
	if (domain)
		return (remote_data->cur_lsbkeycode) & 0xff;
	else
		return (remote_data->cur_lsbkeycode >> 8) & 0xf;

}

/*DUOKAN frame body OPERATION_CTRL_REG2 dd,0x0}, hard decode mode
C7 ~ C4	 C3 ~ C0	D7 ~ D4      D3 ~ D0       P3 ~ P0
Header	Custom code	Data Code  Parity Code    Stop Bit */
unsigned int DUOKAN_DOMAIN(struct remote *remote_data, int domain)
{
	if (remote_data->cur_lsbkeycode == 0x0003cccf)	/* power key */
		remote_data->cur_lsbkeycode =
		    ((remote_data->custom_code[0] & 0xff) << 12) | 0xa4;
	if (domain)
		return (remote_data->cur_lsbkeycode >> 4) & 0xff;
	else
		return (remote_data->cur_lsbkeycode >> 12) & 0xff;
}

unsigned int KDB_NEC_DOMAIN(struct remote *remote_data, int domain)
{
	if (domain)
		return (remote_data->cur_lsbkeycode >> 4) & 0xff;
	else
		return (remote_data->cur_lsbkeycode >> 12) & 0xff;
}

unsigned int KDB_DUOKAN_DOMAIN(struct remote *remote_data, int domain)
{
	if (domain)
		return (remote_data->cur_lsbkeycode >> 4) & 0xff;
	else
		return (remote_data->cur_lsbkeycode >> 12) & 0xff;
}

unsigned int NULL_DUOKAN_DOMAIN(struct remote *remote_data, int domain)
{
	return 0;
}

unsigned int (*get_cur_key_domian[])
(struct remote *remote_data, int domain) = {
	NEC_DOMAIN,
	DUOKAN_DOMAIN,
	RCMM_DOMAIN,
	KDB_NEC_DOMAIN,
	COMCAST_DOMAIN,
	MITSUBISHI_DOMAIN,
	SONYSIRC_DOMAIN,
	TOSHIBA_DOMAIN,
	RC6_DOMAIN,
	RC5_DOMAIN,
	NULL_DUOKAN_DOMAIN,
	RCA_DOMAIN,
	NULL_DUOKAN_DOMAIN,
	NULL_DUOKAN_DOMAIN,
	NULL_DUOKAN_DOMAIN,
	NULL_DUOKAN_DOMAIN,
	NULL_DUOKAN_DOMAIN, NULL_DUOKAN_DOMAIN, KDB_DUOKAN_DOMAIN};

int remote_hw_report_null_key(struct remote *remote_data)
{
	input_dbg("%s,it is a null key\n", __func__);
	get_cur_scancode(remote_data);
	get_cur_scanstatus(remote_data);
	return 0;
}

irqreturn_t remote_null_bridge_isr(int irq, void *dev_id)
{
	return IRQ_HANDLED;
}


/* 0-success other-failed */
int remote_ig_custom_check(struct remote *rd)
{
	int workmode;
	unsigned int customcode;
	int i;

	workmode = rd->work_mode;
	customcode = get_cur_key_domian[workmode](rd, CUSTOMDOMAIN);

	for (i = 0; i < ARRAY_SIZE(rd->custom_code); i++) {
		if (rd->custom_code[i] == customcode) {
			rd->map_num = i;
			return 0;
		}
	}

	input_dbg("Wrong custom code 0x%08x\n",	rd->cur_lsbkeycode);
	return -1;
}


int remote_hw_report_key(struct remote *remote_data)
{
	static int last_scan_code;
	int i;
	get_cur_scancode(remote_data);
	get_cur_scanstatus(remote_data);
	if (remote_data->status)/* repeat enable & come in S timer is open */
		return 0;
	if (remote_data->cur_lsbkeycode) {	/*key first press */
		if (remote_data->ig_custom_enable) {
			for (i = 0; i < ARRAY_SIZE(remote_data->custom_code);) {
				if (remote_data->custom_code[i] !=
				    get_cur_key_domian[remote_data->work_mode]
				    (remote_data, CUSTOMDOMAIN)) {
					/*return -1; */
					i++;
				} else {
					remote_data->map_num = i;
					break;
				}
				if (i == ARRAY_SIZE(remote_data->custom_code)) {
					input_dbg
					    ("Wrong custom code is 0x%08x\n",
					     remote_data->cur_lsbkeycode);
					return -1;
				}
			}
		}
		repeat_count = 0;
		if (time_before(jiffies, remote_data->timer.expires))
			remote_data->remote_send_key(remote_data->input,
						     remote_data->
						     repeat_release_code, 0, 0);

		remote_data->remote_send_key(remote_data->input,
					     get_cur_key_domian
					     [remote_data->work_mode]
					     (remote_data, KEYDOMIAN), 1, 0);
		remote_data->repeat_release_code =
		    get_cur_key_domian[remote_data->work_mode] (remote_data,
								KEYDOMIAN);
		remote_data->enable_repeat_falg = 1;
		if ((remote_data->work_mode > DECODEMODE_NEC) &&
		    remote_data->enable_repeat_falg) {
			if (remote_data->repeat_enable) {
				remote_data->repeat_timer.data =
				    (unsigned long)remote_data;
				/*here repeat  delay is time interval from the
				   first frame end to first repeat end. */
				remote_data->repeat_tick = jiffies;
				mod_timer(&remote_data->repeat_timer,
					  jiffies +
					  msecs_to_jiffies
					  (remote_data->repeat_delay
					   [remote_data->map_num]));
				remote_data->status = TIMER;
			} else {
				setup_timer(&remote_data->rel_timer,
					    remote_rel_timer_sr, 0);
				mod_timer(&remote_data->timer, jiffies);
				remote_data->rel_timer.data =
				    (unsigned long)remote_data;
				mod_timer(&remote_data->rel_timer,
					  jiffies +
					  msecs_to_jiffies
					  (remote_data->relt_delay
					   [remote_data->map_num]));
				remote_data->status = TIMER;
			}
		}
		for (i = 0;
		     i <
		     ARRAY_SIZE(remote_data->key_repeat_map
				[remote_data->map_num]); i++) {
			if (remote_data->key_repeat_map[remote_data->map_num][i]
			    == remote_data->repeat_release_code)
				remote_data->want_repeat_enable = 1;
			else
				remote_data->want_repeat_enable = 0;
		}

		if (remote_data->repeat_enable
		    && remote_data->want_repeat_enable)
			remote_data->repeat_tick =
			    jiffies +
			    msecs_to_jiffies(remote_data->repeat_delay
					     [remote_data->map_num]);
		if (remote_data->repeat_enable)
			mod_timer(&remote_data->timer,
				  jiffies +
				  msecs_to_jiffies(remote_data->release_delay
						   [remote_data->map_num] +
						   remote_data->repeat_delay
						   [remote_data->map_num]));
		else
			mod_timer(&remote_data->timer,
				  jiffies +
				  msecs_to_jiffies(remote_data->release_delay
						   [remote_data->map_num] +
						   remote_data->repeat_delay
						   [remote_data->map_num]));
	} else if ((remote_data->frame_status & REPEARTFLAG) &&
			remote_data->enable_repeat_falg) {	/*repeate key */
#ifdef CONFIG_AML_HDMI_TX
		if ((remote_data->repeat_release_code == 0x1a) &&
						(!cec_repeat)) {
			/*rc_long_press_pwr_key = 1; */
			cec_repeat = 10;
		}
		if (remote_data->repeat_release_code == 0x1a)
			cec_repeat--;

#endif
               //DTS 20160226 add remote led 
				{
				   remote_data->led_status_on ^=0x01;
     				  gpiod_direction_output(remote_data->led_pin_desc,
     			                                          remote_data->led_status_on);
				}
                //DTS  end
		if (remote_data->repeat_enable) {
			repeat_count++;
			if (time_after(jiffies, remote_data->repeat_tick)) {
				if (repeat_count > 3)
					remote_data->remote_send_key
					    (remote_data->input,
					     remote_data->repeat_release_code,
					     2, 0);
				remote_data->repeat_tick +=
				    msecs_to_jiffies(remote_data->repeat_peroid
						     [remote_data->map_num]);
			}
		} else {
			if (time_before(jiffies, remote_data->timer.expires))
				mod_timer(&remote_data->timer,
					  jiffies +
					  msecs_to_jiffies
					  (remote_data->release_delay
					   [remote_data->map_num]));
			/*return -1;*/
		}
		mod_timer(&remote_data->timer,
			  jiffies +
			  msecs_to_jiffies(remote_data->release_delay
					   [remote_data->map_num]) +
			  msecs_to_jiffies(110));
	}
	last_scan_code = remote_data->cur_lsbkeycode;
	remote_data->cur_keycode = last_scan_code;
	remote_data->cur_lsbkeycode = 0;
	remote_data->timer.data = (unsigned long)remote_data;
	return 0;
}

int remote_duokan_parity_check(struct remote *remote_data)
{
	unsigned int data;
	unsigned int code;
	unsigned int c74, c30, d74, d30, p30;

	code = remote_data->cur_lsbkeycode;
	c74 = (code >> 16) & 0xF;
	c30 = (code >> 12) & 0xF;
	d74 = (code >> 8)  & 0xF;
	d30 = (code >> 4)  & 0xF;
	p30 = (code >> 0)  & 0xF;

	data = c74 ^ c30 ^ d74 ^ d30;

	if (p30 == data) {
		input_dbg("parity check ok code=0x%x p30=0x%x parity=0x%x\n",
			code, p30, data);
		return 0;
	} else {
		input_dbg("parity check error code=0x%x p30=0x%x parity=0x%x\n",
			code, p30, data);
		remote_data->cur_lsbkeycode = 0;
		return -1;
	}
}

/* 1:frame is repeat 0: frame is normal */
int is_repeat_key(struct remote *rd)
{
	return rd->frame_status & 0x1; /* bit0 */
}

int remote_duokan_report_key(struct remote *rd)
{
	static int last_scan_code;
	unsigned long flags;
	int ret;
	unsigned int keycode;
	int workmode;
	unsigned int releasedelay;
	unsigned long jiffies_old;
	int parity_flags = 0x00;

	get_cur_scanstatus(rd);
	get_cur_scancode(rd);

	parity_flags = 0x00;
	ret = remote_duokan_parity_check(rd);
	if (ret)
		parity_flags = 0x01;

	workmode = rd->work_mode;
	keycode = get_cur_key_domian[workmode](rd, KEYDOMIAN);

	spin_lock_irqsave(&remote_lock, flags);

	if  (!parity_flags && !is_repeat_key(rd)) {
		rd->jiffies_new = rd->jiffies_irq;
		jiffies_old = rd->jiffies_old;
		rd->jiffies_old = rd->jiffies_new;

		rd->jiffies_old = rd->jiffies_new;
		if (rd->keystate == RC_KEY_STATE_UP) {
			if (rd->ig_custom_enable &&
				remote_ig_custom_check(rd)) {
				spin_unlock_irqrestore(&remote_lock, flags);
				return -1;
			}

			rd->remote_send_key(rd->input, keycode, 1, 0);
			rd->keystate = RC_KEY_STATE_DN;
			auto_repeat_count++;
			rd->repeat_release_code = keycode;
			rd->enable_repeat_falg = 1;
		} else {
			input_dbg("abnormal frame come up\n");
		}
	}

	if (rd->keystate == RC_KEY_STATE_DN) {
		last_scan_code = rd->cur_lsbkeycode;
		rd->cur_keycode = last_scan_code;
		rd->cur_lsbkeycode = 0;
		rd->timer.data = (unsigned long)rd;
		releasedelay = rd->release_delay[rd->map_num];
		input_dbg("ready to release keycode=0x%x\n", rd->cur_keycode);
		mod_timer(&rd->timer, jiffies + msecs_to_jiffies(releasedelay));
	}

	spin_unlock_irqrestore(&remote_lock, flags);
#if 0
	if (remote_data->status) {/* repeat enable & come in S timer is open */
		mod_timer(&remote_data->timer,
			jiffies +
			msecs_to_jiffies(remote_data->release_delay
			[remote_data->map_num] +
			remote_data->repeat_delay
			[remote_data->map_num]));
		return 0;
	}
	if (remote_data->cur_lsbkeycode) {	/*key first press */
		if (remote_data->ig_custom_enable) {
			for (i = 0; i < ARRAY_SIZE(remote_data->custom_code);) {
				if (remote_data->custom_code[i] !=
				    get_cur_key_domian[remote_data->work_mode]
				    (remote_data, CUSTOMDOMAIN)) {
					/*return -1; */
					i++;
				} else {
					remote_data->map_num = i;
					break;
				}
				if (i == ARRAY_SIZE(remote_data->custom_code)) {
					input_dbg
					    ("Wrong custom code is 0x%08x\n",
					     remote_data->cur_lsbkeycode);
					return -1;
				}
			}
		}
		repeat_count = 0;
#if 0
		if (time_before(jiffies, remote_data->timer.expires))	{
			remote_data->remote_send_key(remote_data->input,
						     remote_data->
						     repeat_release_code, 0, 0);
		}
#endif
		remote_data->remote_send_key(remote_data->input,
					     get_cur_key_domian
					     [remote_data->work_mode]
					     (remote_data, KEYDOMIAN), 1, 0);

		remote_data->repeat_release_code =
		    get_cur_key_domian[remote_data->work_mode] (remote_data,
								KEYDOMIAN);
		remote_data->enable_repeat_falg = 1;
		if ((remote_data->work_mode > DECODEMODE_NEC) &&
		    remote_data->enable_repeat_falg) {
			#if 1
			{
				remote_data->repeat_timer.data =
				    (unsigned long)remote_data;
				/*here repeat  delay is time interval from the
				   first frame end to first repeat end. */
				remote_data->repeat_tick = jiffies;
				mod_timer(&remote_data->repeat_timer,
					  jiffies +
					  msecs_to_jiffies
					  (remote_data->repeat_delay
					   [remote_data->map_num]));
				remote_data->status = TIMER;
			}
			#else
				setup_timer(&remote_data->rel_timer,
					    remote_rel_timer_sr, 0);
				mod_timer(&remote_data->timer, jiffies);
				remote_data->rel_timer.data =
				    (unsigned long)remote_data;
				mod_timer(&remote_data->rel_timer,
					  jiffies +
					  msecs_to_jiffies
					  (remote_data->relt_delay
					   [remote_data->map_num]));
				remote_data->status = TIMER;

			#endif
		}
		for (i = 0;
		     i <
		     ARRAY_SIZE(remote_data->key_repeat_map
				[remote_data->map_num]); i++) {
			if (remote_data->key_repeat_map[remote_data->map_num][i]
			    == remote_data->repeat_release_code)
				remote_data->want_repeat_enable = 1;
			else
				remote_data->want_repeat_enable = 0;
		}

		if (remote_data->repeat_enable
		    && remote_data->want_repeat_enable)
			remote_data->repeat_tick =
			    jiffies +
			    msecs_to_jiffies(remote_data->repeat_delay
					     [remote_data->map_num]);
		if (remote_data->repeat_enable)
			mod_timer(&remote_data->timer,
				  jiffies +
				  msecs_to_jiffies(remote_data->release_delay
						   [remote_data->map_num] +
						   remote_data->repeat_delay
						   [remote_data->map_num]));
		else
			mod_timer(&remote_data->timer,
				  jiffies +
				  msecs_to_jiffies(remote_data->release_delay
						   [remote_data->map_num] +
						   remote_data->repeat_delay
						   [remote_data->map_num]));
	} else if ((remote_data->frame_status & REPEARTFLAG) &&
			remote_data->enable_repeat_falg) {	/*repeate key */
#ifdef CONFIG_AML_HDMI_TX
		if ((remote_data->repeat_release_code == 0x1a) &&
						(!cec_repeat)) {
			/*rc_long_press_pwr_key = 1; */
			cec_repeat = 10;
		}
		if (remote_data->repeat_release_code == 0x1a)
			cec_repeat--;

#endif
		if (remote_data->repeat_enable) {
			repeat_count++;
			if (time_after(jiffies, remote_data->repeat_tick)) {
				if (repeat_count > 1)
					remote_data->remote_send_key
					    (remote_data->input,
					     remote_data->repeat_release_code,
					     2, 0);

				remote_data->repeat_tick +=
				    msecs_to_jiffies(remote_data->repeat_peroid
						     [remote_data->map_num]);
			}
		} else {
			if (time_before(jiffies, remote_data->timer.expires))
				mod_timer(&remote_data->timer,
					  jiffies +
					  msecs_to_jiffies
					  (remote_data->release_delay
					   [remote_data->map_num]));
			return -1;
		}
		mod_timer(&remote_data->timer,
			  jiffies +
			  msecs_to_jiffies(remote_data->release_delay
					   [remote_data->map_num]) +
			  msecs_to_jiffies(110));
	}
	last_scan_code = remote_data->cur_lsbkeycode;
	remote_data->cur_keycode = last_scan_code;
	remote_data->cur_lsbkeycode = 0;
	remote_data->timer.data = (unsigned long)remote_data;
#endif
	return 0;
}
int remote_rc6_report_key(struct remote *remote_data)
{
	static int last_scan_code;
	int i;

	get_cur_scanstatus(remote_data);
	get_cur_scancode(remote_data);
	if (!auto_repeat_count) {
		if (remote_data->cur_lsbkeycode) {	/*key first press */
			if (remote_data->ig_custom_enable) {
				for (i = 0; i < ARRAY_SIZE
					(remote_data->custom_code);) {
					if (remote_data->custom_code[i] !=
						get_cur_key_domian
						[remote_data->work_mode]
						(remote_data, CUSTOMDOMAIN)) {
						/*return -1; */
						i++;
					} else {
						remote_data->map_num = i;
						break;
					}
					if (i == ARRAY_SIZE
						(remote_data->custom_code)) {
						input_dbg
						("Wrong custom code 0x%08x\n",
						remote_data->cur_lsbkeycode);
						return -1;
				}
			}
		}

	remote_data->remote_send_key(remote_data->input,
		     get_cur_key_domian
		     [remote_data->work_mode]
		     (remote_data, KEYDOMIAN), 1, 0);
			auto_repeat_count++;
	remote_data->repeat_release_code =
			get_cur_key_domian
			[remote_data->work_mode]
			(remote_data , KEYDOMIAN);
		remote_data->enable_repeat_falg = 1;
		}
	}

	mod_timer(&remote_data->timer,
	  jiffies +
	  msecs_to_jiffies(remote_data->release_delay
			   [remote_data->map_num]));

	last_scan_code = remote_data->cur_lsbkeycode;
	remote_data->cur_keycode = last_scan_code;
	remote_data->cur_lsbkeycode = 0;
	remote_data->timer.data = (unsigned long)remote_data;
	return 0;
}

int remote_hw_nec_rca_2in1_report_key(struct remote *remote_data)
{
	static int last_scan_code;
	int i;
	get_cur_scancode(remote_data);
	get_cur_scanstatus(remote_data);
	if (remote_data->status)/* repeat enable & come in S timer is open */
		return 0;
	if (remote_data->cur_lsbkeycode) {	/*key first press */
		if (remote_data->ig_custom_enable) {
			for (i = 0; i < ARRAY_SIZE(remote_data->custom_code);) {
				if (remote_data->custom_code[i] !=
				    get_cur_key_domian
				    [remote_data->temp_work_mode]
				    (remote_data, CUSTOMDOMAIN)) {
					/*return -1; */
					i++;
				} else {
					remote_data->map_num = i;
					break;
				}
				if (i == ARRAY_SIZE(remote_data->custom_code)) {
					input_dbg("Wrong custom code is 0x%08x,",
						remote_data->cur_lsbkeycode);
					input_dbg("temp_work_mode is %d\n",
						remote_data->temp_work_mode);
					return -1;
				}
			}
		}
		repeat_count = 0;
		if (time_before(jiffies, remote_data->timer.expires))
			remote_data->remote_send_key(remote_data->input,
						     remote_data->
						     repeat_release_code, 0, 0);
		remote_data->remote_send_key(remote_data->input,
					     get_cur_key_domian[remote_data->
								temp_work_mode]
					     (remote_data, KEYDOMIAN), 1, 0);
		remote_data->repeat_release_code =
		    get_cur_key_domian[remote_data->temp_work_mode]
		    (remote_data, KEYDOMIAN);
		remote_data->enable_repeat_falg = 1;
		if ((remote_data->temp_work_mode == DECODEMODE_RCA) &&
		    (remote_data->enable_repeat_falg)) {
			if (remote_data->repeat_enable) {
				remote_data->repeat_timer.data =
				    (unsigned long)remote_data;
				/*here repeat  delay is time interval
				from the first frame end to first repeat end.*/
				remote_data->repeat_tick = jiffies;
				mod_timer(&remote_data->repeat_timer,
					  jiffies +
					  msecs_to_jiffies
					  (remote_data->repeat_delay
					   [remote_data->map_num]));
				remote_data->status = TIMER;
			} else {
				setup_timer(&remote_data->rel_timer,
					    remote_rel_timer_sr, 0);
				mod_timer(&remote_data->timer, jiffies);
				remote_data->rel_timer.data =
				    (unsigned long)remote_data;
				mod_timer(&remote_data->rel_timer,
					  jiffies +
					  msecs_to_jiffies
					  (remote_data->relt_delay
					   [remote_data->map_num]));
				remote_data->status = TIMER;
			}
		}
		for (i = 0;
		     i <
		     ARRAY_SIZE(remote_data->key_repeat_map
				[remote_data->map_num]); i++) {
			if (remote_data->key_repeat_map[remote_data->map_num][i]
			    == remote_data->repeat_release_code)
				remote_data->want_repeat_enable = 1;
			else
				remote_data->want_repeat_enable = 0;
		}

		if (remote_data->repeat_enable
		    && remote_data->want_repeat_enable)
			remote_data->repeat_tick =
			    jiffies +
			    msecs_to_jiffies(remote_data->repeat_delay
					     [remote_data->map_num]);
		if (remote_data->repeat_enable)
			mod_timer(&remote_data->timer,
				  jiffies +
				  msecs_to_jiffies(remote_data->release_delay
						   [remote_data->map_num] +
						   remote_data->repeat_delay
						   [remote_data->map_num]));
		else
			mod_timer(&remote_data->timer,
				  jiffies +
				  msecs_to_jiffies(remote_data->release_delay
						   [remote_data->map_num] +
						   remote_data->repeat_delay
						   [remote_data->map_num]));
	} else if ((remote_data->frame_status & REPEARTFLAG) &&
			remote_data->enable_repeat_falg) {	/*repeate key */
#ifdef CONFIG_AML_HDMI_TX
		if ((remote_data->repeat_release_code == 0x1a)
						&& (!cec_repeat)) {
			/*rc_long_press_pwr_key = 1; */
			cec_repeat = 10;
		}
		if (remote_data->repeat_release_code == 0x1a)
			cec_repeat--;

#endif
		if (remote_data->repeat_enable) {
			repeat_count++;
			if (time_after(jiffies, remote_data->repeat_tick)) {
				if (repeat_count > 1)
					remote_data->remote_send_key
					    (remote_data->input,
					     remote_data->repeat_release_code,
					     2, 0);
				remote_data->repeat_tick +=
				    msecs_to_jiffies(remote_data->repeat_peroid
						     [remote_data->map_num]);
			}
		} else {
			if (time_before(jiffies, remote_data->timer.expires))
				mod_timer(&remote_data->timer,
					  jiffies +
					  msecs_to_jiffies
					  (remote_data->release_delay
					   [remote_data->map_num]));
			return -1;
		}
		mod_timer(&remote_data->timer,
			  jiffies +
			  msecs_to_jiffies(remote_data->release_delay
					   [remote_data->map_num]) +
			  msecs_to_jiffies(110));
	}
	last_scan_code = remote_data->cur_lsbkeycode;
	remote_data->cur_keycode = last_scan_code;
	remote_data->cur_lsbkeycode = 0;
	remote_data->timer.data = (unsigned long)remote_data;
	return 0;
}

int remote_hw_nec_toshiba_2in1_report_key(struct remote *remote_data)
{
	static int last_scan_code;
	int i;
	get_cur_scancode(remote_data);
	get_cur_scanstatus(remote_data);
	if (remote_data->cur_lsbkeycode) {	/*key first press */
		if (remote_data->ig_custom_enable) {
			for (i = 0; i < ARRAY_SIZE(remote_data->custom_code);) {
				if (remote_data->custom_code[i] !=
				    get_cur_key_domian
				    [remote_data->temp_work_mode]
				    (remote_data, CUSTOMDOMAIN)) {
					/*return -1; */
					i++;
				} else {
					remote_data->map_num = i;
					break;
				}

				if (i == ARRAY_SIZE(remote_data->custom_code)) {
					input_dbg("Wrong custom code 0x%08x,",
						remote_data->cur_lsbkeycode);
					input_dbg("temp_work_mode is %d\n",
						remote_data->temp_work_mode);
					return -1;
				}
			}
		}
		repeat_count = 0;
		if (time_before(jiffies, remote_data->timer.expires))
			remote_data->remote_send_key(remote_data->input,
						     remote_data->
						     repeat_release_code, 0, 0);
		remote_data->remote_send_key(remote_data->input,
					     get_cur_key_domian[remote_data->
								temp_work_mode]
					     (remote_data, KEYDOMIAN), 1, 0);
		remote_data->repeat_release_code =
		    get_cur_key_domian[remote_data->temp_work_mode]
		    (remote_data, KEYDOMIAN);
		remote_data->enable_repeat_falg = 1;
		if (remote_data->temp_work_mode == DECODEMODE_TOSHIBA) {
			/* setting frame bit = 1; */
			am_remote_write_reg(OPERATION_CTRL_REG1, 0x8000);
		}
		for (i = 0;
		     i <
		     ARRAY_SIZE(remote_data->key_repeat_map
				[remote_data->map_num]); i++) {
			if (remote_data->key_repeat_map[remote_data->map_num][i]
			    == remote_data->repeat_release_code)
				remote_data->want_repeat_enable = 1;
			else
				remote_data->want_repeat_enable = 0;
		}

		if (remote_data->repeat_enable
		    && remote_data->want_repeat_enable)
			remote_data->repeat_tick =
			    jiffies +
			    msecs_to_jiffies(remote_data->repeat_delay
					     [remote_data->map_num]);
		if (remote_data->repeat_enable)
			mod_timer(&remote_data->timer,
				  jiffies +
				  msecs_to_jiffies(remote_data->release_delay
						   [remote_data->map_num] +
						   remote_data->repeat_delay
						   [remote_data->map_num]));
		else
			mod_timer(&remote_data->timer,
				  jiffies +
				  msecs_to_jiffies(remote_data->release_delay
						   [remote_data->map_num] +
						   remote_data->repeat_delay
						   [remote_data->map_num]));
	} else if ((remote_data->frame_status & REPEARTFLAG) &&
			remote_data->enable_repeat_falg) {/*repeate key */
#ifdef CONFIG_AML_HDMI_TX
		if ((remote_data->repeat_release_code == 0x1a)
		    && (!cec_repeat)) {
			/*rc_long_press_pwr_key = 1; */
			cec_repeat = 10;
		}
		if (remote_data->repeat_release_code == 0x1a)
			cec_repeat--;

#endif
		if (remote_data->repeat_enable) {
			repeat_count++;
			if (time_after(jiffies, remote_data->repeat_tick)) {
				if (repeat_count > 3)
					remote_data->remote_send_key
					    (remote_data->input,
					     remote_data->repeat_release_code,
					     2, 0);
				remote_data->repeat_tick +=
				    msecs_to_jiffies(remote_data->repeat_peroid
						     [remote_data->map_num]);
			}
		} else {
			if (time_before(jiffies, remote_data->timer.expires))
				mod_timer(&remote_data->timer,
					  jiffies +
					  msecs_to_jiffies
					  (remote_data->release_delay
					   [remote_data->map_num]));
			/*return -1;*/
		}
		mod_timer(&remote_data->timer,
			  jiffies +
			  msecs_to_jiffies(remote_data->release_delay
					   [remote_data->map_num]) +
			  msecs_to_jiffies(110));
	}
	last_scan_code = remote_data->cur_lsbkeycode;
	remote_data->cur_keycode = last_scan_code;
	remote_data->cur_lsbkeycode = 0;
	remote_data->timer.data = (unsigned long)remote_data;
	return 0;
}

static inline void kbd_software_mode_remote_send_key(unsigned long data)
{
	struct remote *remote_data = (struct remote *)data;
	int i;
	get_cur_scancode(remote_data);
	remote_data->step = REMOTE_STATUS_SYNC;
	if (remote_data->repeate_flag) {
		if (time_after(jiffies, remote_data->repeat_tick) &&
		    (remote_data->enable_repeat_falg == 1)) {
			remote_data->remote_send_key(remote_data->input,
						     remote_data->
						     repeat_release_code, 2, 0);
			remote_data->repeat_tick +=
			    msecs_to_jiffies(remote_data->input->
					     rep[REP_PERIOD]);
		}
	} else {
		if (remote_data->ig_custom_enable) {
			for (i = 0; i < ARRAY_SIZE(remote_data->custom_code);) {
				if (remote_data->custom_code[i] !=
				    get_cur_key_domian[remote_data->work_mode]
				    (remote_data, CUSTOMDOMAIN)) {
					/*return -1; */
					i++;
				} else {
					remote_data->map_num = i;
					break;
				}
				if (i == ARRAY_SIZE(remote_data->custom_code)) {
					input_dbg
					    ("Wrong custom code is 0x%08x\n",
					     remote_data->cur_lsbkeycode);
					return;
				}
			}
		}
		remote_data->remote_send_key(remote_data->input,
					     get_cur_key_domian
					     [remote_data->work_mode]
					     (remote_data, KEYDOMIAN), 1, 0);
		remote_data->repeat_release_code =
		    get_cur_key_domian[remote_data->work_mode] (remote_data,
								KEYDOMIAN);
		remote_data->enable_repeat_falg = 1;
		for (i = 0;
		     i <
		     ARRAY_SIZE(remote_data->key_repeat_map
				[remote_data->map_num]); i++) {
			if (remote_data->key_repeat_map[remote_data->map_num][i]
			    == remote_data->repeat_release_code)
				remote_data->want_repeat_enable = 1;
			else
				remote_data->want_repeat_enable = 0;
		}
		if (remote_data->repeat_enable
		    && remote_data->want_repeat_enable)
			remote_data->repeat_tick =
			    jiffies +
			    msecs_to_jiffies(remote_data->input->
					     rep[REP_DELAY]);
	}
}

static void remote_rca_repeat_sr(unsigned long data)
{
	struct remote *remote_data = (struct remote *)data;
	if (remote_data->cur_keycode == remote_data->cur_lsbkeycode) {
		repeat_count++;
		if (repeat_count > 2)
			remote_data->remote_send_key(remote_data->input,
						     remote_data->
						     repeat_release_code, 2, 0);
		remote_data->cur_lsbkeycode = 0;
		remote_data->repeat_timer.data = (unsigned long)remote_data;
		remote_data->timer.data = (unsigned long)remote_data;
		mod_timer(&remote_data->timer,
			  jiffies +
			  msecs_to_jiffies(remote_data->release_delay
					   [remote_data->map_num] +
					   remote_data->repeat_peroid
					   [remote_data->map_num]));
		mod_timer(&remote_data->repeat_timer,
			  jiffies +
			  msecs_to_jiffies(remote_data->repeat_peroid
					   [remote_data->map_num]));
		remote_data->status = TIMER;
	} else {
		remote_data->status = NORMAL;
		remote_data->timer.data = (unsigned long)remote_data;
		mod_timer(&remote_data->timer, jiffies + msecs_to_jiffies(1));
	}
}

static void remote_repeat_sr(unsigned long data)
{
	struct remote *remote_data = (struct remote *)data;
	if (remote_data->cur_keycode == remote_data->cur_lsbkeycode) {
		auto_repeat_count++;
		if (auto_repeat_count > 1) {
			if (remote_data->repeat_enable)
				remote_data->remote_send_key(remote_data->input,
						     remote_data->
						     repeat_release_code, 2, 0);
		}
		remote_data->cur_lsbkeycode = 0;
		remote_data->repeat_timer.data = (unsigned long)remote_data;
		remote_data->timer.data = (unsigned long)remote_data;
		mod_timer(&remote_data->repeat_timer,
			  jiffies +
			  msecs_to_jiffies(remote_data->repeat_peroid
					   [remote_data->map_num]));
		remote_data->status = TIMER;
	} else {
		remote_data->status = NORMAL;
		remote_data->timer.data = (unsigned long)remote_data;
		mod_timer(&remote_data->timer, jiffies + msecs_to_jiffies(1));
	}
}

static void remote_rel_timer_sr(unsigned long data)
{
	struct remote *remote_data = (struct remote *)data;
	if (remote_data->cur_keycode == remote_data->cur_lsbkeycode) {
		remote_data->cur_lsbkeycode = 0;
		remote_data->rel_timer.data = (unsigned long)remote_data;
		mod_timer(&remote_data->rel_timer,
			  jiffies +
			  msecs_to_jiffies(remote_data->relt_delay
					   [remote_data->map_num]));
		remote_data->status = TIMER;
	} else
		remote_data->status = NORMAL;

}

static int get_pulse_width(struct remote *remote_data)
{
	unsigned int pulse_width;
	const char *state;

	pulse_width =
	    (am_remote_read_reg(OPERATION_CTRL_REG1) & 0x1FFF0000) >> 16;
	state =
	    remote_data->step ==
	    REMOTE_STATUS_WAIT ? "wait" : remote_data->step ==
	    REMOTE_STATUS_LEADER ? "leader" : remote_data->step ==
	    REMOTE_STATUS_DATA ? "data" : remote_data->step ==
	    REMOTE_STATUS_SYNC ? "sync" : NULL;
	dbg_printk("%02d:pulse_wdith:%d==>%s\r\n",
		   remote_data->bit_count - remote_data->bit_num, pulse_width,
		   state);
	/*sometimes we found remote  pulse width==0.
	   in order to sync machine state we modify it . */
	if (pulse_width == 0) {
		switch (remote_data->step) {
		case REMOTE_STATUS_LEADER:
			pulse_width = remote_data->time_window[0] + 1;
			break;
		case REMOTE_STATUS_DATA:
			pulse_width = remote_data->time_window[2] + 1;
			break;
		}
	}
	return pulse_width;
}

static inline void kbd_software_mode_remote_wait(struct remote *remote_data)
{
	remote_data->step = REMOTE_STATUS_LEADER;
	remote_data->cur_keycode = 0;
	remote_data->bit_num = remote_data->bit_count;
}

static inline void kbd_software_mode_remote_leader(struct remote *remote_data)
{
	unsigned int pulse_width;
	pulse_width = get_pulse_width(remote_data);
	if ((pulse_width > remote_data->time_window[0])
	    && (pulse_width < remote_data->time_window[1]))
		remote_data->step = REMOTE_STATUS_DATA;
	else
		remote_data->step = REMOTE_STATUS_WAIT;

	remote_data->cur_keycode = 0;
	remote_data->bit_num = remote_data->bit_count;
}

static inline void kbd_software_mode_remote_data(struct remote *remote_data)
{
	unsigned int pulse_width;

	pulse_width = get_pulse_width(remote_data);
	remote_data->step = REMOTE_STATUS_DATA;
	switch (remote_data->work_mode) {
	case DECODEMODE_SW_NEC:
		if ((pulse_width > remote_data->time_window[2])
		    && (pulse_width < remote_data->time_window[3]))
			remote_data->bit_num--;
		else if ((pulse_width > remote_data->time_window[4])
			 && (pulse_width < remote_data->time_window[5])) {
			remote_data->bit_num--;
			remote_data->cur_keycode |=
			    1 << (remote_data->bit_count -
				  remote_data->bit_num);
		} else
			remote_data->step = REMOTE_STATUS_WAIT;
		if (remote_data->bit_num == 0) {
			remote_data->repeate_flag = 0;
			remote_data->send_data = 1;
			fiq_bridge_pulse_trigger(&remote_data->fiq_handle_item);
		}
		break;
	case DECODEMODE_SW_DUOKAN:
		if ((pulse_width > remote_data->time_window[2])
		    && (pulse_width < remote_data->time_window[3]))
			remote_data->bit_num -= 2;
		else if ((pulse_width > remote_data->time_window[4])
			 && (pulse_width < remote_data->time_window[5])) {
			remote_data->cur_keycode |=
			    1 << (remote_data->bit_count -
				  remote_data->bit_num);
			remote_data->bit_num -= 2;
		} else if ((pulse_width > remote_data->time_window[8])
			   && (pulse_width < remote_data->time_window[9])) {
			remote_data->cur_keycode |=
			    2 << (remote_data->bit_count -
				  remote_data->bit_num);
			remote_data->bit_num -= 2;
		} else if ((pulse_width > remote_data->time_window[10])
			   && (pulse_width < remote_data->time_window[11])) {
			remote_data->cur_keycode |=
			    3 << (remote_data->bit_count -
				  remote_data->bit_num);
			remote_data->bit_num -= 2;
		} else
			remote_data->step = REMOTE_STATUS_WAIT;
		if (remote_data->bit_num == 0) {
			remote_data->repeate_flag = 0;
			remote_data->send_data = 1;
			fiq_bridge_pulse_trigger(&remote_data->fiq_handle_item);
		}
		break;
	}
}

static inline void kbd_software_mode_remote_sync(struct remote *remote_data)
{
	unsigned int pulse_width;

	pulse_width = get_pulse_width(remote_data);
	if ((pulse_width > remote_data->time_window[6])
	    && (pulse_width < remote_data->time_window[7])) {
		remote_data->repeate_flag = 1;
		if (remote_data->repeat_enable)
			remote_data->send_data = 1;
		else {
			remote_data->step = REMOTE_STATUS_SYNC;
			return;
		}
	}
	remote_data->step = REMOTE_STATUS_SYNC;
	fiq_bridge_pulse_trigger(&remote_data->fiq_handle_item);

}

int remote_sw_report_key(struct remote *remote_data)
{
	int current_jiffies = jiffies;

	if (((current_jiffies - remote_data->last_jiffies) > 20)
	    && (remote_data->step <= REMOTE_STATUS_SYNC))
		remote_data->step = REMOTE_STATUS_WAIT;
	remote_data->last_jiffies = current_jiffies;
	/*ignore a little msecs */
	switch (remote_data->step) {
	case REMOTE_STATUS_WAIT:
		kbd_software_mode_remote_wait(remote_data);
		break;
	case REMOTE_STATUS_LEADER:
		kbd_software_mode_remote_leader(remote_data);
		break;
	case REMOTE_STATUS_DATA:
		kbd_software_mode_remote_data(remote_data);
		break;
	case REMOTE_STATUS_SYNC:
		kbd_software_mode_remote_sync(remote_data);
		break;
	default:
		break;
	}
	return 0;
}

irqreturn_t remote_bridge_isr(int irq, void *dev_id)
{
	struct remote *remote_data = (struct remote *)dev_id;

	if (remote_data->send_data) {	/*report key */
		kbd_software_mode_remote_send_key((unsigned long)remote_data);
		remote_data->send_data = 0;
	}
	remote_data->timer.data = (unsigned long)remote_data;
	mod_timer(&remote_data->timer,
		  jiffies +
		  msecs_to_jiffies(remote_data->release_delay
				   [remote_data->map_num]));
	return IRQ_HANDLED;
}

void kdb_send_key(struct input_dev *dev, unsigned int scancode,
		  unsigned int type, int event)
{
	return;
}

void remote_nec_report_release_key(struct remote *remote_data)
{
	if (remote_data->enable_repeat_falg) {
		remote_data->remote_send_key(remote_data->input,
					     remote_data->repeat_release_code,
					     0, 0);
		remote_data->enable_repeat_falg = 0;
	}
}

void remote_duokan_report_release_key(struct remote *remote_data)
{
	if (remote_data->enable_repeat_falg) {
		remote_data->remote_send_key(remote_data->input,
					     remote_data->repeat_release_code,
					     0, 0);
		remote_data->keystate = RC_KEY_STATE_UP;
		remote_data->enable_repeat_falg = 0;
		auto_repeat_count = 0;
	}
}
void remote_rc6_report_release_key(struct remote *remote_data)
{
	if (remote_data->enable_repeat_falg) {
		remote_data->remote_send_key(remote_data->input,
					     remote_data->repeat_release_code,
					     0, 0);
		remote_data->enable_repeat_falg = 0;

		auto_repeat_count = 0;
	}
}

void remote_sw_report_release_key(struct remote *remote_data)
{
	if (remote_data->enable_repeat_falg) {
		remote_data->remote_send_key(remote_data->input,
					     remote_data->repeat_release_code,
					     0, 0);
		remote_data->enable_repeat_falg = 0;
	}
}

void remote_nec_rca_2in1_report_release_key(struct remote *remote_data)
{
	if (remote_data->enable_repeat_falg) {
		remote_data->remote_send_key(remote_data->input,
					     remote_data->repeat_release_code,
					     0, 0);
		remote_data->enable_repeat_falg = 0;
	}
}

void remote_nec_toshiba_2in1_report_release_key(struct remote *remote_data)
{
	if (remote_data->enable_repeat_falg) {
		remote_data->remote_send_key(remote_data->input,
					     remote_data->repeat_release_code,
					     0, 0);
		remote_data->enable_repeat_falg = 0;
		am_remote_write_reg(OPERATION_CTRL_REG1, 0x9f40);

	}
}
int remote_hw_nec_rcmm_2in1_report_key(struct remote *remote_data)
{
	static int last_scan_code;
	int i;
	get_cur_scancode(remote_data);
	get_cur_scanstatus(remote_data);
	input_dbg("rcmm-%d-0x%08x\n", auto_repeat_count,
					remote_data->cur_lsbkeycode);
	if (!auto_repeat_count) {
		if (remote_data->cur_lsbkeycode) {	/*key first press */
			if (remote_data->ig_custom_enable) {
				for (i = 0; i < ARRAY_SIZE
					(remote_data->custom_code);) {
					if (remote_data->custom_code[i] !=
						get_cur_key_domian
						[remote_data->temp_work_mode]
						(remote_data, CUSTOMDOMAIN)) {
						/*return -1; */
						i++;
					} else {
						remote_data->map_num = i;
						break;
					}
					if (i == ARRAY_SIZE
						(remote_data->custom_code)) {
						input_dbg
						("Wrong custom code 0x%08x\n",
						remote_data->cur_lsbkeycode);
						return -1;
				}
			}
		}

	remote_data->remote_send_key(remote_data->input,
		     get_cur_key_domian
		     [remote_data->temp_work_mode]
		     (remote_data, KEYDOMIAN), 1, 0);
			auto_repeat_count++;
	remote_data->repeat_release_code =
			get_cur_key_domian
			[remote_data->temp_work_mode]
			(remote_data , KEYDOMIAN);
		remote_data->enable_repeat_falg = 1;
		}
	}

	mod_timer(&remote_data->timer,
	  jiffies +
	  msecs_to_jiffies(remote_data->release_delay
			   [remote_data->map_num]));

	last_scan_code = remote_data->cur_lsbkeycode;
	remote_data->cur_keycode = last_scan_code;
	remote_data->cur_lsbkeycode = 0;
	remote_data->timer.data = (unsigned long)remote_data;
	return 0;
}
void remote_nec_rcmm_2in1_report_release_key(struct remote *remote_data)
{
	if (remote_data->enable_repeat_falg) {
		remote_data->remote_send_key(remote_data->input,
			remote_data->repeat_release_code, 0, 0);
		remote_data->enable_repeat_falg = 0;
		/*am_remote_write_reg(OPERATION_CTRL_REG1,0x9f40);*/
		auto_repeat_count = 0;
	}
	input_dbg("status reg:0x%08x|Framedata:0x%08x\n",
		am_remote_read_reg(DURATION_REG1_AND_STATUS),
		am_remote_read_reg(FRAME_BODY));
}


void remote_null_report_release_key(struct remote *remote_data)
{

}
