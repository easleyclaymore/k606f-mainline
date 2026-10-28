// SPDX-License-Identifier: GPL-2.0-only
/*
 * Lenovo Yoga Tab 13 (YT-K606F) dual-DSI split video-mode panel driver.
 *
 * The panel is a 2160x1350 OLED driven over two DSI links in "split" mode
 * (each link carries 1080x1350). It has NO DCS init sequence and NO DSC —
 * it's a pure video-mode panel, so prepare/enable only need to handle the
 * optional reset GPIO and power supply.
 *
 * The dual-DSI registration follows the same pattern as panel-novatek-nt36532:
 * the secondary DSI device (for the second link) is looked up via the panel's
 * port@1 endpoint and registered/attached explicitly. This is what triggers
 * the second DSI host's component_add — which panel-simple does NOT do and is
 * exactly why plain panel-simple cannot drive this panel.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct k606f_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi[2];
	struct regulator *supply;
	struct gpio_desc *reset_gpio;
};

static inline struct k606f_panel *to_k606f(struct drm_panel *panel)
{
	return container_of(panel, struct k606f_panel, panel);
}

/*
 * 2160x1350@60. Timings taken verbatim from the vendor DT node
 * qcom,mdss_dsi_dual_sim_video (per-link width 0x438=1080, height 0x546=1350):
 *   hfp 16 / hbp 12 / hpw 16, vbp 8 / vfp 8 / vpw 1, 24 bpp, 60 fps.
 * The DRM mode carries the FULL 2160-wide frame; msm splits it across the two
 * bonded links internally.
 */
static const struct drm_display_mode k606f_mode = {
	.clock = 180772,
	.hdisplay = 2160,
	.hsync_start = 2160 + 16,
	.hsync_end = 2160 + 16 + 16,
	.htotal = 2160 + 16 + 16 + 12,
	.vdisplay = 1350,
	.vsync_start = 1350 + 8,
	.vsync_end = 1350 + 8 + 1,
	.vtotal = 1350 + 8 + 1 + 8,
	.flags = DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_NHSYNC,
};

static int k606f_get_modes(struct drm_panel *panel,
			   struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &k606f_mode);
	if (!mode)
		return -ENOMEM;

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 280;
	connector->display_info.height_mm = 175;
	connector->display_info.bpc = 8;

	return 1;
}

static int k606f_prepare(struct drm_panel *panel)
{
	struct k606f_panel *ctx = to_k606f(panel);
	int ret;

	if (ctx->supply) {
		ret = regulator_enable(ctx->supply);
		if (ret)
			return ret;
	}

	if (ctx->reset_gpio) {
		/* reset 低有效：deassert→assert→deassert（原厂 reset-sequence 高→低→高） */
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		msleep(10);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		msleep(10);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		msleep(10);
	}

	return 0;
}

static int k606f_enable(struct drm_panel *panel)
{
	/* video-mode panel, no init sequence */
	return 0;
}

static int k606f_disable(struct drm_panel *panel)
{
	return 0;
}

static int k606f_unprepare(struct drm_panel *panel)
{
	struct k606f_panel *ctx = to_k606f(panel);

	if (ctx->reset_gpio)
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);

	if (ctx->supply)
		regulator_disable(ctx->supply);

	return 0;
}

static const struct drm_panel_funcs k606f_panel_funcs = {
	.prepare = k606f_prepare,
	.enable = k606f_enable,
	.disable = k606f_disable,
	.unprepare = k606f_unprepare,
	.get_modes = k606f_get_modes,
};

static int k606f_probe(struct mipi_dsi_device *dsi)
{
	struct mipi_dsi_host *dsi_sec_host;
	struct k606f_panel *ctx;
	struct device *dev = &dsi->dev;
	struct device_node *dsi_sec;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct k606f_panel, panel,
				   &k606f_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->supply = devm_regulator_get_optional(dev, "vddio");
	if (IS_ERR(ctx->supply)) {
		ret = PTR_ERR(ctx->supply);
		if (ret == -EPROBE_DEFER)
			return ret;
		ctx->supply = NULL;
	}

	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "failed to get reset-gpios\n");

	/* Dual-DSI: register the secondary DSI device via panel's port@1. */
	dsi_sec = of_graph_get_remote_node(dsi->dev.of_node, 1, -1);
	if (dsi_sec) {
		const struct mipi_dsi_device_info info = { "k606f", 0, dsi_sec };

		dev_info(dev, "Using Dual-DSI with secondary node `%s`\n",
			 dsi_sec->name);

		dsi_sec_host = of_find_mipi_dsi_host_by_node(dsi_sec);
		of_node_put(dsi_sec);
		if (!dsi_sec_host)
			return dev_err_probe(dev, -EPROBE_DEFER,
					     "Cannot get secondary DSI host\n");

		ctx->dsi[1] = devm_mipi_dsi_device_register_full(dev,
								dsi_sec_host,
								&info);
		if (IS_ERR(ctx->dsi[1]))
			return dev_err_probe(dev, PTR_ERR(ctx->dsi[1]),
					     "Cannot register secondary DSI device\n");

		mipi_dsi_set_drvdata(ctx->dsi[1], ctx);
	}

	ctx->dsi[0] = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_NO_EOT_PACKET;
	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI0\n");
	}

	if (ctx->dsi[1]) {
		ctx->dsi[1]->lanes = 4;
		ctx->dsi[1]->format = MIPI_DSI_FMT_RGB888;
		ctx->dsi[1]->mode_flags = MIPI_DSI_MODE_VIDEO |
					  MIPI_DSI_MODE_NO_EOT_PACKET;
		ret = devm_mipi_dsi_attach(dev, ctx->dsi[1]);
		if (ret) {
			drm_panel_remove(&ctx->panel);
			return dev_err_probe(dev, ret, "Failed to attach to DSI1\n");
		}
	}

	return 0;
}

static const struct of_device_id k606f_of_match[] = {
	{ .compatible = "lenovo,k606f-panel" },
	{ }
};
MODULE_DEVICE_TABLE(of, k606f_of_match);

static struct mipi_dsi_driver k606f_driver = {
	.probe = k606f_probe,
	.driver = {
		.name = "panel-lenovo-k606f",
		.of_match_table = k606f_of_match,
	},
};
module_mipi_dsi_driver(k606f_driver);

MODULE_AUTHOR("k606f mainline port");
MODULE_DESCRIPTION("Lenovo Yoga Tab 13 (YT-K606F) dual-DSI panel driver");
MODULE_LICENSE("GPL");
