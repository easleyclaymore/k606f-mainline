// SPDX-License-Identifier: GPL-2.0-only
/*
 * Lenovo Yoga Tab 13 (YT-K606F) MIPI-DSI -> eDP bridge (PSA8331A)
 *
 * The panel on this tablet is not a DSI panel: it is an eDP panel. Between the
 * two DSI links and the panel sits a PSA8331A ("eDP-MUX-8331") bridge that
 * takes the DSI video stream, forwards it over eDP, and also owns most of the
 * panel power rails, the AUX port select and the backlight enables.
 *
 * Consequences for the display stack:
 *   - the panel has no DCS init sequence (it is eDP, there is nothing to init
 *     over DSI), and
 *   - enabling DSI alone is not enough: DSI reports TIMEOUT|FIFO errors and no
 *     image appears until this bridge is powered and put into MIPI->eDP mode.
 *
 * This driver models the bridge as a regular drm_bridge that sits between
 * msm's dsi_mgr_bridge and the panel: DRM then guarantees the right order
 * (DSI enabled first, then this bridge, then the panel).
 *
 * Power sequence is taken from the vendor downstream driver
 * (techpack/display/msm/dsi/dsi86-edp-mux.c), minus everything that is
 * product specific (board_id / TV-in mode switching / touch coupling).
 *
 * Stage B (current): rails + GPIO sequence + I2C "MIPI to eDP" mux setup only.
 * The 106-entry eDP link configuration and link training are NOT done here yet.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_bridge.h>
#include <drm/drm_print.h>

/*
 * There are TWO chips on the same I2C bus (vendor dsi86-edp-mux.c):
 *   - TI SN65DSI86 @ 0x2C: the actual DSI -> eDP bridge. It receives the DSI
 *     stream and forwards it as eDP. It needs the 59-register init table, the
 *     TPS1 write and the link-training polls (0x0A == 0x82, 0x96 == 0x01).
 *   - PS8331 mux @ 0x33: only a port-select mux behind the bridge. It gets just
 *     4 registers (0x00 / 0x03 / 0x10 / 0x11).
 *
 * The vendor DT node "eDP-MUX@66" is reg = <0x66> (8-bit) = 0x33 (7-bit). The
 * DSI86 lives on the same bus at 0x2C (= 0x58 8-bit) and is NOT a separate DT
 * node: the vendor driver flips the slave address on the single i2c_client. We
 * do the same with i2c_transfer().
 *
 * B38 之前把整张 59 条表写到了 0x33（mux），真正的 DSI86(0x2C) 从未配置 ——
 * 这正是 status=5（HS TX 超时 + FIFO 溢出）的根因：桥的 DSI 接收器根本没起来。
 */
#define PSA8331_I2C_ADDR_MUX	0x33
#define PSA8331_I2C_ADDR_DSI86	0x2C

/* Vendor: dsi86_set_to_tps1() */
#define PSA8331_REG_TPLUS		0x96
#define PSA8331_TPLUS_TPS1		0x02

/* Vendor: dsi86_edp_mux_init() */
struct psa8331_reg_val {
	u8 reg;
	u8 val;
};

struct psa8331_edp_reg {
	u8 reg;
	u8 val;
	u8 delay_ms;	/* vendor metadata, not applied (vendor loop ignores it) */
};

static const struct psa8331_reg_val psa8331_mux_init[] = {
	{ 0x00, 0x00 },
	{ 0x03, 0x38 },
	{ 0x10, 0x0a },
	{ 0x11, 0x02 },
};

/*
 * Vendor dsi86_init[] -- eDP side bring-up (52 entries, mechanically extracted
 * from techpack/display/msm/dsi/dsi86-edp-mux.c). Written right after the chip
 * is put into TPS1, before the mux registers. Without it the bridge has no eDP
 * output, so the panel stays lit-but-blank even though DSI is running.
 */
static const struct psa8331_edp_reg psa8331_edp_init[] = {
	{ 0xff, 0x07, 2 },
	{ 0x16, 0x01, 2 },
	{ 0xff, 0x00, 2 },
	{ 0x0a, 0x02, 2 },
	{ 0xe0, 0x01, 2 },
	{ 0x5c, 0x01, 2 },
	/* B56 单链：0x10 = 单通道 A（bits[6:5]=01）+ CHA 4 lane；CHA=2160、CHB 关 */
	{ 0x10, 0x20, 2 },
	{ 0x12, 0x6c, 2 },	/* clock lane 542 MHz：8+floor((542-40)/5)=0x6c */
	{ 0x13, 0x00, 2 },
	{ 0x94, 0x80, 2 },
	{ 0x0d, 0x01, 2 },
	/* DP TX PHY 配置（lane 极性/摆幅/预加重）—— 厂商在 0x0d 之后、0x5A 之前写，
	 * 我们之前整段漏掉，导致 0x96 链路训练锁不上（B28 定位）。 */
	{ 0x64, 0x01, 2 },
	{ 0x74, 0x00, 2 },
	{ 0x75, 0x01, 2 },
	{ 0x76, 0x0a, 2 },
	{ 0x77, 0x01, 2 },
	{ 0x78, 0x81, 2 },
	{ 0xee, 0x0a, 0 },	/* 延时 10ms（0xEE 的 value 是毫秒数，见写入循环） */
	{ 0x5a, 0x04, 2 },
	{ 0x93, 0x20, 2 },
	{ 0xb0, 0x04, 2 },
	{ 0xb1, 0x08, 2 },
	{ 0xb2, 0x0c, 2 },
	{ 0xb3, 0x24, 2 },
	{ 0xb4, 0x28, 2 },
	{ 0xb5, 0x2c, 2 },
	{ 0xb6, 0x34, 2 },
	{ 0xb7, 0x38, 2 },
	{ 0xb8, 0x3c, 2 },
	{ 0xb9, 0x44, 2 },
	{ 0xba, 0x48, 2 },
	{ 0xbb, 0x4c, 2 },
	{ 0xbc, 0x54, 2 },
	{ 0xbd, 0x58, 2 },
	{ 0xbe, 0x5c, 2 },
	{ 0xbf, 0x7c, 2 },
	{ 0xc0, 0xff, 2 },
	{ 0xc1, 0xff, 2 },
	{ 0xc2, 0xff, 2 },
	{ 0xc3, 0xff, 2 },
	{ 0x95, 0x00, 2 },
	{ 0x96, 0x09, 2 },
	{ 0x20, 0x70, 2 },	/* CHA_ACTIVE_LINE_LENGTH = 2160 (单链全宽) */
	{ 0x21, 0x08, 2 },
	{ 0x22, 0x00, 2 },	/* CHB 不用 */
	{ 0x23, 0x00, 2 },
	{ 0x24, 0x46, 2 },
	{ 0x25, 0x05, 2 },
	{ 0x2c, 0x10, 2 },
	{ 0x2d, 0x00, 2 },
	{ 0x30, 0x01, 2 },
	{ 0x31, 0x00, 2 },
	{ 0x34, 0x0c, 2 },
	{ 0x36, 0x08, 2 },
	{ 0x38, 0x10, 2 },
	{ 0x3a, 0x08, 2 },
	{ 0x5b, 0x00, 2 },
	{ 0x3c, 0x07, 2 },
	{ 0x5a, 0x0c, 2 },
};

struct psa8331 {
	struct drm_bridge bridge;
	struct i2c_client *client;

	struct regulator *vdd_1p2;
	struct regulator *vdd_1p5;
	struct regulator *vdd_1p8;
	struct regulator *vdd_3p3;
	struct clk *refclk;

	/* power / mux GPIOs, all optional */
	struct gpio_desc *gpio_lcd_bob_en;	/* panel VBOOST */
	struct gpio_desc *gpio_lcd_3v3_en;	/* LCD_VCC_PANEL_3V3 load switch */
	struct gpio_desc *gpio_edp_mux_pwr_en;
	struct gpio_desc *gpio_mipi_to_edp_en;
	struct gpio_desc *gpio_panel_gpio_mux_en;
	struct gpio_desc *gpio_io_mux_en;
	struct gpio_desc *gpio_i2s_mux_en;
	struct gpio_desc *gpio_io_mux_select;
	struct gpio_desc *gpio_edp_aux_port_sel;
	struct gpio_desc *gpio_i2s_mux_select;
	struct gpio_desc *gpio_boost_bypass_en;
	struct gpio_desc *gpio_boost_vsel_en;
	struct gpio_desc *gpio_boost_nbyp_en;
	struct gpio_desc *gpio_bl_pwr_en;
	struct gpio_desc *gpio_bl_en;
	struct gpio_desc *gpio_pwr_status;	/* input, reads 0 when bridge is up */

	bool powered;
	bool edp_trained;
	ktime_t last_off;
};

static inline struct psa8331 *to_psa8331(struct drm_bridge *bridge)
{
	return container_of(bridge, struct psa8331, bridge);
}

/*
 * Raw byte write to an arbitrary 7-bit address on the bridge's bus. Used to
 * reach both the DSI86 (0x2C) and the mux (0x33) through the single client we
 * are bound to.
 */
static int psa8331_write_addr(struct psa8331 *ctx, u8 addr, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };
	struct i2c_msg msg = {
		.addr = addr,
		.flags = 0,
		.len = 2,
		.buf = buf,
	};
	int ret = i2c_transfer(ctx->client->adapter, &msg, 1);

	if (ret != 1) {
		dev_err(&ctx->client->dev,
			"i2c write 0x%02x[0x%02x]=0x%02x failed: %d\n",
			addr, reg, val, ret);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int psa8331_read_addr(struct psa8331 *ctx, u8 addr, u8 reg)
{
	u8 val = 0;
	struct i2c_msg msgs[2] = {
		{ .addr = addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = addr, .flags = I2C_M_RD, .len = 1, .buf = &val },
	};
	int ret = i2c_transfer(ctx->client->adapter, msgs, 2);

	if (ret != 2)
		return ret < 0 ? ret : -EIO;

	return val;
}

/*
 * Vendor polls these two after writing them; without the poll the eDP link
 * never finishes (semi-auto training). addr is the chip to poll (0x2C DSI86).
 */
static int psa8331_poll(struct psa8331 *ctx, u8 addr, u8 reg, u8 want, int tries)
{
	int i, val;

	for (i = 0; i < tries; i++) {
		val = psa8331_read_addr(ctx, addr, reg);
		if (val == want)
			return 0;
		mdelay(10);
	}

	dev_warn(&ctx->client->dev,
		 "0x%02x reg 0x%02x never reached 0x%02x (last 0x%02x)\n",
		 addr, reg, want, val);
	return -ETIMEDOUT;
}

static int psa8331_edp_regs_init(struct psa8331 *ctx)
{
	struct device *dev = &ctx->client->dev;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(psa8331_edp_init); i++) {
		u8 reg = psa8331_edp_init[i].reg;

		if (reg == 0xee) {
			/* 厂商：0xEE 的 value[0] 是延时毫秒数（不写寄存器） */
			mdelay(psa8331_edp_init[i].val);
			continue;
		}

		ret = psa8331_write_addr(ctx, PSA8331_I2C_ADDR_DSI86, reg,
					 psa8331_edp_init[i].val);
		if (ret)
			return ret;

		if (reg == 0x0d) {
			/* vendor: wait then expect DP PLL lock via 0x0A == 0x82 */
			mdelay(10);
			if (psa8331_poll(ctx, PSA8331_I2C_ADDR_DSI86, 0x0a, 0x82, 10))
				dev_warn(dev, "0x0d: DP PLL lock not reported\n");
		} else if (reg == 0x96) {
			/* vendor: link training finished when 0x96 reads 0x01 */
			mdelay(20);
			if (psa8331_poll(ctx, PSA8331_I2C_ADDR_DSI86, 0x96, 0x01, 10))
				dev_warn(dev, "0x96: link training not finished\n");
		}
	}

	dev_info(dev, "eDP init table written (%zu registers)\n",
		 ARRAY_SIZE(psa8331_edp_init));
	return 0;
}

/* Vendor: DSI86_POWER_OFF_TIME - minimum off-time before re-powering 3V3 */
#define PSA8331_MIN_OFF_MS	500




/*
 * Vendor: turn_on_lcd_vcc() -> turn_on_lcd_vcc_loadswitch() -> turn_on_dp_mux()
 *         -> turn_on_io_mux() -> switch_to_port1() -> turn_on_led_vss()
 *
 * The io-mux and mux-select lines are active low in the vendor code
 * ("value = on ? 0 : 1"). Here that polarity lives in the device tree
 * (GPIO_ACTIVE_LOW), so the driver only ever uses logical values.
 */
static int psa8331_rails_on(struct psa8331 *ctx)
{
	s64 off_ms;
	int ret;

	/* Vendor waits before re-powering 3V3 after a recent power-off. */
	if (ctx->last_off) {
		off_ms = ktime_ms_delta(ktime_get_boottime(), ctx->last_off);
		if (off_ms < PSA8331_MIN_OFF_MS)
			msleep(PSA8331_MIN_OFF_MS - off_ms);
	}

	gpiod_set_value_cansleep(ctx->gpio_lcd_bob_en, 1);
	gpiod_set_value_cansleep(ctx->gpio_lcd_3v3_en, 1);

	if (ctx->vdd_3p3) {
		ret = regulator_enable(ctx->vdd_3p3);
		if (ret) {
			dev_err(&ctx->client->dev, "3p3 rail failed: %d\n", ret);
			return ret;
		}
	}

	/*
	 * Vendor turn_on_dp_mux(): 3V3 and 1V5 come up together in the pre-ULPS
	 * phase, i.e. BEFORE 1V2/1V8/mipi-to-edp-en -- and 1V5 is 1.5 V, not the
	 * 1.7 V this board's old DT range implied (measured on the vendor kernel
	 * via /sys/class/regulator: pm8150_l7 = 1500000 uV).
	 */
	if (ctx->vdd_1p5) {
		/*
		 * 1.5 V nominal, but this PMIC's step granularity cannot hit
		 * 1500000 exactly (min<=max with a single value fails with
		 * "unsupportable voltage range: 1504000-1500000uV"), so give it a
		 * window and let the regulator pick the closest selector.
		 */
		regulator_set_voltage(ctx->vdd_1p5, 1500000, 1600000);
		ret = regulator_enable(ctx->vdd_1p5);
		if (ret) {
			dev_err(&ctx->client->dev, "1p5 rail failed: %d\n", ret);
			return ret;
		}
		dev_info(&ctx->client->dev, "1p5 rail on\n");
	}

	gpiod_set_value_cansleep(ctx->gpio_edp_mux_pwr_en, 1);

	/* io-mux group and switch_to_port1(): assert = select the MIPI input */
	gpiod_set_value_cansleep(ctx->gpio_panel_gpio_mux_en, 1);
	gpiod_set_value_cansleep(ctx->gpio_io_mux_en, 1);
	gpiod_set_value_cansleep(ctx->gpio_i2s_mux_en, 1);
	gpiod_set_value_cansleep(ctx->gpio_io_mux_select, 1);
	gpiod_set_value_cansleep(ctx->gpio_edp_aux_port_sel, 1);
	gpiod_set_value_cansleep(ctx->gpio_i2s_mux_select, 1);

	/* turn_on_led_vss(): boost configuration */
	gpiod_set_value_cansleep(ctx->gpio_boost_bypass_en, 1);
	gpiod_set_value_cansleep(ctx->gpio_boost_vsel_en, 1);
	gpiod_set_value_cansleep(ctx->gpio_boost_nbyp_en, 1);

	/* backlight rails can come up now; brightness is the pwm-backlight's job */
	gpiod_set_value_cansleep(ctx->gpio_bl_pwr_en, 1);

	return 0;
}

/* Vendor: turn_on_dsi86(true) */
static int psa8331_bridge_power(struct psa8331 *ctx, bool on)
{
	int ret = 0;

	if (on) {
		if (ctx->vdd_1p2) {
			regulator_set_voltage(ctx->vdd_1p2, 1200000, 1200000);
			ret = regulator_enable(ctx->vdd_1p2);
			if (ret)
				return ret;
		}

		udelay(100);

		if (ctx->vdd_1p8) {
			regulator_set_voltage(ctx->vdd_1p8, 1800000, 1800000);
			ret = regulator_enable(ctx->vdd_1p8);
			if (ret)
				goto err_1p2;
		}

		/* 19.2 MHz reference ("bb_clk2" in the vendor DT) */
		if (!IS_ERR_OR_NULL(ctx->refclk))
			clk_prepare_enable(ctx->refclk);

		mdelay(1);
		gpiod_set_value_cansleep(ctx->gpio_mipi_to_edp_en, 1);

		if (ctx->vdd_1p5 && regulator_enable(ctx->vdd_1p5))
			dev_warn(&ctx->client->dev, "1p5 rail enable failed\n");

		return 0;

err_1p2:
		if (ctx->vdd_1p2)
			regulator_disable(ctx->vdd_1p2);
		return ret;
	}

	gpiod_set_value_cansleep(ctx->gpio_mipi_to_edp_en, 0);

	if (!IS_ERR_OR_NULL(ctx->refclk))
		clk_disable_unprepare(ctx->refclk);

	if (ctx->vdd_1p5)
		regulator_disable(ctx->vdd_1p5);
	if (ctx->vdd_1p8)
		regulator_disable(ctx->vdd_1p8);
	mdelay(2);
	if (ctx->vdd_1p2)
		regulator_disable(ctx->vdd_1p2);

	return 0;
}

/*
 * Vendor: dsi86_power_on_post_ulps()
 *   wait for the bridge's own power-good line, then put it in TPS1.
 */
static int psa8331_wait_power_good(struct psa8331 *ctx)
{
	unsigned int i;
	int val;

	if (!ctx->gpio_pwr_status)
		return 0;

	for (i = 0; i < 40; i++) {
		val = gpiod_get_value_cansleep(ctx->gpio_pwr_status);
		if (val == 0)
			return 0;
		mdelay(5);
	}

	dev_warn(&ctx->client->dev, "power-good never asserted\n");
	return -ETIMEDOUT;
}

/*
 * Staged power-up, exposed through sysfs as well as used by DRM.
 *
 * Reason: a wrong GPIO or rail can take the whole SoC down (USB included), and
 * then there is no log to read. Splitting the sequence lets the host poke one
 * stage at a time and check that the debug channel survived before going on.
 *
 *   0 = all off, 1 = panel rails + mux group, 2 = bridge rails/refclk/enable,
 *   3 = wait power-good + TPS1 + MIPI->eDP mux registers, 4 = backlight enable
 */
#define PSA8331_STAGE_OFF	0
#define PSA8331_STAGE_RAILS	1
#define PSA8331_STAGE_BRIDGE	2
#define PSA8331_STAGE_MUX	3
#define PSA8331_STAGE_BL	4
#define PSA8331_STAGE_EDP_INIT	5
#define PSA8331_STAGE_EDP_MUX	6
/* sub-steps of stage 2, so a killer rail can be isolated */
#define PSA8331_SUB_1P2		21
#define PSA8331_SUB_1P8		22
#define PSA8331_SUB_REFCLK	23
#define PSA8331_SUB_MIPI_EN	24
#define PSA8331_SUB_1P5		25

static void psa8331_power_off_all(struct psa8331 *ctx);

static int psa8331_power_stage(struct psa8331 *ctx, int stage)
{
	struct device *dev = &ctx->client->dev;
	int sub, ret = 0;

	switch (stage) {
	case PSA8331_STAGE_RAILS:
		dev_info(dev, "stage 1: panel rails + io mux group\n");
		ret = psa8331_rails_on(ctx);
		break;
	case PSA8331_STAGE_BRIDGE:
		dev_info(dev, "stage 2: bridge power, sub-steps 21..25\n");
		for (sub = PSA8331_SUB_1P2; sub <= PSA8331_SUB_MIPI_EN; sub++) {
			ret = psa8331_power_stage(ctx, sub);
			if (ret)
				break;
			/* vendor delays: 100us after 1p2, 1ms after refclk */
			if (sub == PSA8331_SUB_1P2)
				udelay(100);
			if (sub == PSA8331_SUB_REFCLK)
				mdelay(1);
		}
		break;
	case PSA8331_SUB_1P2:
		dev_info(dev, "stage 21: vdd-1p2 1.2V + enable\n");
		if (ctx->vdd_1p2) {
			ret = regulator_set_voltage(ctx->vdd_1p2, 1200000, 1200000);
			if (!ret)
				ret = regulator_enable(ctx->vdd_1p2);
		}
		break;
	case PSA8331_SUB_1P8:
		dev_info(dev, "stage 22: vdd-1p8 1.8V + enable\n");
		if (ctx->vdd_1p8) {
			ret = regulator_set_voltage(ctx->vdd_1p8, 1800000, 1800000);
			if (!ret)
				ret = regulator_enable(ctx->vdd_1p8);
		}
		break;
	case PSA8331_SUB_REFCLK:
		dev_info(dev, "stage 23: refclk (bb_clk2) enable\n");
		if (!IS_ERR_OR_NULL(ctx->refclk))
			ret = clk_prepare_enable(ctx->refclk);
		break;
	case PSA8331_SUB_MIPI_EN:
		dev_info(dev, "stage 24: mipi-to-edp-en = 1\n");
		gpiod_set_value_cansleep(ctx->gpio_mipi_to_edp_en, 1);
		break;
	case PSA8331_SUB_1P5:
		/* already brought up together with 3V3 in stage 1 (vendor order) */
		dev_info(dev, "stage 25: vdd-1p5 1.5V + enable\n");
		if (ctx->vdd_1p5) {
			regulator_set_voltage(ctx->vdd_1p5, 1500000, 1600000);
			ret = regulator_enable(ctx->vdd_1p5);
		}
		break;
	case PSA8331_STAGE_MUX:
		dev_info(dev, "stage 3: wait power-good + TPS1 (DSI86 0x2C)\n");
		ret = psa8331_wait_power_good(ctx);
		if (ret)
			break;
		ret = psa8331_write_addr(ctx, PSA8331_I2C_ADDR_DSI86,
					 PSA8331_REG_TPLUS, PSA8331_TPLUS_TPS1);
		break;
	case PSA8331_STAGE_EDP_INIT:
		dev_info(dev, "stage 5: eDP init table (%zu registers, DSI86 0x2C)\n",
			 ARRAY_SIZE(psa8331_edp_init));
		ret = psa8331_edp_regs_init(ctx);
		break;
	case PSA8331_STAGE_EDP_MUX:
		dev_info(dev, "stage 6: MIPI->eDP mux registers (PS8331 0x33)\n");
		for (unsigned int i = 0; i < ARRAY_SIZE(psa8331_mux_init); i++) {
			ret = psa8331_write_addr(ctx, PSA8331_I2C_ADDR_MUX,
					    psa8331_mux_init[i].reg,
					    psa8331_mux_init[i].val);
			if (ret)
				break;
		}
		break;
	case PSA8331_STAGE_BL:
		dev_info(dev, "stage 4: backlight enable\n");
		gpiod_set_value_cansleep(ctx->gpio_bl_en, 1);
		break;
	case PSA8331_STAGE_OFF:
		dev_info(dev, "stage 0: all off\n");
		psa8331_power_off_all(ctx);
		ctx->powered = false;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	dev_info(dev, "stage %d done, rc=%d\n", stage, ret);
	return ret;
}

static void psa8331_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct psa8331 *ctx = to_psa8331(bridge);
	int stage, ret;

	pr_info("[K606F] psa8331 pre_enable 开始 powered=%d\n", ctx->powered);
	if (ctx->powered)
		return;

	/*
	 * Vendor order (seen in the downstream kernel's own log):
	 *   pre_ulps -> bridge power/TPS1 -> dsi86_edp_mux_init() -> dsi86_edp_regs_init()
	 * The four mux registers must go FIRST: without them the DP side never
	 * locks the PLL (0x0A stays 0x02 instead of 0x82) and link training never
	 * completes (0x96 stays 0x00 instead of 0x01).
	 */
	static const int order[] = {
		PSA8331_STAGE_RAILS, PSA8331_STAGE_BRIDGE, PSA8331_STAGE_MUX,
		PSA8331_STAGE_EDP_MUX,
	};

	for (unsigned int o = 0; o < ARRAY_SIZE(order); o++) {
		stage = order[o];
		ret = psa8331_power_stage(ctx, stage);
		if (ret) {
			dev_err(&ctx->client->dev, "power stage %d failed: %d\n",
				stage, ret);
			return;
		}
	}

	ctx->powered = true;
	dev_info(&ctx->client->dev, "bridge powered and set to MIPI->eDP\n");
}

static ssize_t power_stage_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct psa8331 *ctx = dev_get_drvdata(dev);
	int stage;

	if (kstrtoint(buf, 0, &stage))
		return -EINVAL;

	psa8331_power_stage(ctx, stage);
	return count;
}
static DEVICE_ATTR_WO(power_stage);


static void psa8331_power_off_all(struct psa8331 *ctx)
{
	gpiod_set_value_cansleep(ctx->gpio_bl_en, 0);
	gpiod_set_value_cansleep(ctx->gpio_bl_pwr_en, 0);

	psa8331_bridge_power(ctx, false);

	gpiod_set_value_cansleep(ctx->gpio_boost_nbyp_en, 0);
	gpiod_set_value_cansleep(ctx->gpio_boost_vsel_en, 0);
	gpiod_set_value_cansleep(ctx->gpio_boost_bypass_en, 0);

	gpiod_set_value_cansleep(ctx->gpio_i2s_mux_select, 0);
	gpiod_set_value_cansleep(ctx->gpio_edp_aux_port_sel, 0);
	gpiod_set_value_cansleep(ctx->gpio_io_mux_select, 0);

	gpiod_set_value_cansleep(ctx->gpio_i2s_mux_en, 0);
	gpiod_set_value_cansleep(ctx->gpio_io_mux_en, 0);
	gpiod_set_value_cansleep(ctx->gpio_panel_gpio_mux_en, 0);

	gpiod_set_value_cansleep(ctx->gpio_edp_mux_pwr_en, 0);
	if (ctx->vdd_3p3)
		regulator_disable(ctx->vdd_3p3);
	gpiod_set_value_cansleep(ctx->gpio_lcd_3v3_en, 0);
	gpiod_set_value_cansleep(ctx->gpio_lcd_bob_en, 0);

	ctx->last_off = ktime_get_boottime();
	ctx->powered = false;
}

static void psa8331_bridge_post_disable(struct drm_bridge *bridge)
{
	struct psa8331 *ctx = to_psa8331(bridge);

	if (!ctx->powered)
		return;

	psa8331_power_off_all(ctx);
}

static void psa8331_bridge_enable(struct drm_bridge *bridge)
{
	struct psa8331 *ctx = to_psa8331(bridge);
	int ret;

	/*
	 * The eDP bring-up (mux + 52-register table + link training) MUST run
	 * after the panel is powered and out of reset. DRM calls pre_enable in
	 * chain order (dsi_mgr -> bridge -> panel), so during pre_enable the
	 * panel is still in reset -- the DP AUX then never answers and both
	 * 0x0A (PLL lock) and 0x96 (training) stay at their idle values.
	 * The downstream driver orders it the same way: dsi_panel_power_on()
	 * first, then dsi_display_dsi86_init().
	 */
	if (!ctx->edp_trained) {
		pr_info("[K606F] psa8331 pre_enable: EDP_MUX+INIT+训练开始\n");
		ret = psa8331_power_stage(ctx, PSA8331_STAGE_EDP_MUX);
		pr_info("[K606F] psa8331 pre_enable: EDP_MUX rc=%d\n", ret);
		if (!ret)
			ret = psa8331_power_stage(ctx, PSA8331_STAGE_EDP_INIT);
		pr_info("[K606F] psa8331 pre_enable: EDP_INIT rc=%d\n", ret);
		if (!ret)
			ctx->edp_trained = true;
	}

	/*
	 * Report the training result without relying on the debug channel: a
	 * failed msm load tends to take USB down with it, and then there is no
	 * way to read dmesg. Backlight blinks are readable by a human:
	 *   3 short blinks  = eDP trained (0x0A==0x82, 0x96==0x01)
	 *   1 long dark gap = training failed
	 */
	if (ctx->edp_trained) {
		for (int i = 0; i < 3; i++) {
			gpiod_set_value_cansleep(ctx->gpio_bl_en, 0);
			msleep(150);
			gpiod_set_value_cansleep(ctx->gpio_bl_en, 1);
			msleep(150);
		}
	} else {
		gpiod_set_value_cansleep(ctx->gpio_bl_en, 0);
		msleep(1500);
	}

	/* panel is up by now: let the backlight in */
	gpiod_set_value_cansleep(ctx->gpio_bl_en, 1);
}

static void psa8331_bridge_disable(struct drm_bridge *bridge)
{
	struct psa8331 *ctx = to_psa8331(bridge);

	gpiod_set_value_cansleep(ctx->gpio_bl_en, 0);
}

static int psa8331_bridge_attach(struct drm_bridge *bridge,
				 struct drm_encoder *encoder,
				 enum drm_bridge_attach_flags flags)
{
	struct psa8331 *ctx = to_psa8331(bridge);
	struct drm_bridge *next;

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR))
		return -EINVAL;

	next = devm_drm_of_get_bridge(&ctx->client->dev,
				      ctx->client->dev.of_node, 1, 0);
	if (IS_ERR(next))
		return dev_err_probe(&ctx->client->dev, PTR_ERR(next),
				     "no panel behind the bridge\n");

	return drm_bridge_attach(encoder, next, bridge, flags);
}

static const struct drm_bridge_funcs psa8331_bridge_funcs = {
	.attach = psa8331_bridge_attach,
	.pre_enable = psa8331_bridge_pre_enable,
	.post_disable = psa8331_bridge_post_disable,
	.enable = psa8331_bridge_enable,
	.disable = psa8331_bridge_disable,
};

static struct gpio_desc *psa8331_get_gpio(struct device *dev, const char *name)
{
	struct gpio_desc *gpio;

	gpio = devm_gpiod_get_optional(dev, name, GPIOD_ASIS);
	if (IS_ERR(gpio))
		dev_warn(dev, "gpio %s unavailable: %ld\n", name, PTR_ERR(gpio));

	return IS_ERR(gpio) ? NULL : gpio;
}

static int psa8331_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct psa8331 *ctx;
	int ret;

	/*
	 * Since 7.x a bridge must be allocated by DRM itself: drm_bridge_add()
	 * expects `list`, `container` and `refcount` to be initialised, and a
	 * zeroed embedded struct makes it believe the bridge is on the
	 * lingering list and list_del_init() a NULL entry (oops in
	 * drm_bridge_add). Same idiom as drivers/gpu/drm/bridge/simple-bridge.c.
	 */
	ctx = devm_drm_bridge_alloc(dev, struct psa8331, bridge,
				    &psa8331_bridge_funcs);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->client = client;
	i2c_set_clientdata(client, ctx);

	dev_info(dev, "probe: start\n");

	ctx->vdd_1p2 = devm_regulator_get_optional(dev, "vdd-1p2");
	ctx->vdd_1p5 = devm_regulator_get_optional(dev, "vdd-1p5");
	ctx->vdd_1p8 = devm_regulator_get_optional(dev, "vdd-1p8");
	ctx->vdd_3p3 = devm_regulator_get_optional(dev, "vdd-3p3");
	if (IS_ERR(ctx->vdd_1p2) || IS_ERR(ctx->vdd_1p5) ||
	    IS_ERR(ctx->vdd_1p8) || IS_ERR(ctx->vdd_3p3))
		return -EPROBE_DEFER;

	dev_info(dev, "probe: regulators ok\n");

	/* 参考时钟是 DP PLL 锁相的依据，**不能 optional**：拿不到就该让 probe 失败，
	 * 否则会静默跳过 enable，桥锁不上 PLL（B29 教训，0x0A 卡 0x02）。 */
	ctx->refclk = devm_clk_get(dev, "refclk");
	if (IS_ERR(ctx->refclk))
		return dev_err_probe(dev, PTR_ERR(ctx->refclk), "no refclk\n");
	dev_info(dev, "refclk rate = %lu Hz\n", clk_get_rate(ctx->refclk));

	ctx->gpio_lcd_bob_en = psa8331_get_gpio(dev, "lcd-bob-en");
	ctx->gpio_lcd_3v3_en = psa8331_get_gpio(dev, "lcd-3v3-en");
	ctx->gpio_edp_mux_pwr_en = psa8331_get_gpio(dev, "edp-mux-pwr-en");
	ctx->gpio_mipi_to_edp_en = psa8331_get_gpio(dev, "mipi-to-edp-en");
	ctx->gpio_panel_gpio_mux_en = psa8331_get_gpio(dev, "panel-gpio-mux-en");
	ctx->gpio_io_mux_en = psa8331_get_gpio(dev, "io-mux-en");
	ctx->gpio_i2s_mux_en = psa8331_get_gpio(dev, "i2s-mux-en");
	ctx->gpio_io_mux_select = psa8331_get_gpio(dev, "io-mux-select");
	ctx->gpio_edp_aux_port_sel = psa8331_get_gpio(dev, "edp-aux-port-sel");
	ctx->gpio_i2s_mux_select = psa8331_get_gpio(dev, "i2s-mux-select");
	ctx->gpio_boost_bypass_en = psa8331_get_gpio(dev, "boost-bypass-en");
	ctx->gpio_boost_vsel_en = psa8331_get_gpio(dev, "boost-vsel-en");
	ctx->gpio_boost_nbyp_en = psa8331_get_gpio(dev, "boost-nbyp-en");
	ctx->gpio_bl_pwr_en = psa8331_get_gpio(dev, "bl-pwr-en");
	ctx->gpio_bl_en = psa8331_get_gpio(dev, "bl-en");

	ctx->gpio_pwr_status = devm_gpiod_get_optional(dev, "pwr-status",
						       GPIOD_IN);
	dev_info(dev, "probe: gpios ok\n");

	ctx->bridge.of_node = dev->of_node;

	ret = devm_drm_bridge_add(dev, &ctx->bridge);
	if (ret)
		return ret;

	/* debugging aid: one power stage at a time from userspace */
	device_create_file(dev, &dev_attr_power_stage);

	dev_info(dev, "PSA8331 eDP bridge registered\n");
	return 0;
}

static const struct of_device_id psa8331_of_match[] = {
	{ .compatible = "lenovo,psa8331-edp-mux" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, psa8331_of_match);

static struct i2c_driver psa8331_driver = {
	.driver = {
		.name = "psa8331-edp-mux",
		.of_match_table = psa8331_of_match,
	},
	.probe = psa8331_probe,
};
module_i2c_driver(psa8331_driver);

MODULE_AUTHOR("k606f mainline port");
MODULE_DESCRIPTION("PSA8331A MIPI-DSI to eDP bridge (Lenovo YT-K606F)");
MODULE_LICENSE("GPL");
