// SPDX-License-Identifier: GPL-2.0
/*
 * drmpaint -- 不用 libdrm，直接 ioctl 把画面送到 DRM 显示管道。
 *
 * 为什么需要它：这块板子的 fbdev 没有内核虚拟地址（fbcon 会打印
 * "framebuffer is not in virtual address space"），所以内核控制台和 /dev/fb0
 * 都画不进东西。要判断"显示链路到底通没通"，必须自己建 dumb buffer 并 modeset。
 *
 * 画什么：默认是**让人一眼能认出来**的画面 ——
 *   · 整屏 8 条彩条（白/黄/青/绿/品红/红/蓝/黑）
 *   · 正中间一个超大数字，每秒 +1（不动=没在扫描，动=通了）
 *   · 数字上方一行小方块随时间从左往右扫
 * 静态画面容易被误判成"背光均匀亮"，所以默认带动态元素。
 *
 * 用法：
 *   drmpaint [/dev/dri/card0] [pattern] [seconds]
 *     pattern: bars（默认，彩条+数字）| count（黑底+超大数字）|
 *              white | red | green | blue | ramp | noise
 *     seconds: 进程存活时间，默认 900（进程退出后 buffer 会被回收而黑屏）
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>
#include <linux/fb.h>

static int fd;
static uint8_t *pix;
static uint32_t W, H, PITCH;

static int ioctl_ret(unsigned long req, void *arg, const char *what)
{
	int r = ioctl(fd, req, arg);

	if (r)
		fprintf(stderr, "%s: %s\n", what, strerror(errno));
	return r;
}

/*
 * 必须在画之前把 fbcon 从 framebuffer 上解绑。
 *
 * 探针日志里有两行 "Console: switching to colour frame buffer device 270x84"：
 * 一次在 0.24 s（simpledrm），一次在 37.15 s（msmdrmfb 注册时）—— 说明 msm 一注册
 * framebuffer，fbcon 就重新绑上去。此后内核任何一条 printk 都会触发 fbcon 重绘，
 * 把我们刚画的东西整屏刷掉。表现和"链路没通"完全一样：只有背光、没有图案。
 *
 * 做法：遍历 /sys/class/vtconsole/vtcon*，把 name 里带 "frame buffer" 的那个
 * 写 bind=0。返回实际解绑的个数（0 = 本来就没绑，也是好结果）。
 */
static int detach_fbcon(void)
{
	DIR *d = opendir("/sys/class/vtconsole");
	struct dirent *e;
	int unbound = 0;

	if (!d) {
		printf("vtconsole: 不存在（%s），fbcon 可能没编进来\n", strerror(errno));
		return 0;
	}

	while ((e = readdir(d))) {
		char path[256], name[128] = {0};
		FILE *f;
		int n;

		if (strncmp(e->d_name, "vtcon", 5))
			continue;

		snprintf(path, sizeof(path), "/sys/class/vtconsole/%s/name", e->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (!fgets(name, sizeof(name), f))
			name[0] = 0;
		fclose(f);
		if (strlen(name))
			name[strlen(name) - 1] = 0;

		printf("vtcon %s = \"%s\"\n", e->d_name, name);

		if (!strstr(name, "frame buffer"))
			continue;

		snprintf(path, sizeof(path), "/sys/class/vtconsole/%s/bind", e->d_name);
		f = fopen(path, "w");
		if (!f) {
			printf("  解绑失败（打不开 %s）：%s\n", path, strerror(errno));
			continue;
		}
		n = fprintf(f, "0");
		fclose(f);
		printf("  解绑 fbcon: write rc=%d\n", n);
		if (n > 0)
			unbound++;
	}
	closedir(d);
	printf("fbcon 解绑数 = %d\n", unbound);
	fflush(stdout);
	return unbound;
}

static inline void put(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b)
{
	if (x >= W || y >= H)
		return;
	uint8_t *d = pix + (size_t)y * PITCH + (size_t)x * 4;

	d[0] = b;
	d[1] = g;
	d[2] = r;
	d[3] = 0xff;
}

static void rect(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h,
		 uint8_t r, uint8_t g, uint8_t b)
{
	for (uint32_t y = y0; y < y0 + h; y++)
		for (uint32_t x = x0; x < x0 + w; x++)
			put(x, y, r, g, b);
}

/* 3x5 点阵数字，放大成超大字号 */
static const uint8_t digit3x5[10][5] = {
	{0b111, 0b101, 0b101, 0b101, 0b111}, /* 0 */
	{0b010, 0b110, 0b010, 0b010, 0b111}, /* 1 */
	{0b111, 0b001, 0b111, 0b100, 0b111}, /* 2 */
	{0b111, 0b001, 0b111, 0b001, 0b111}, /* 3 */
	{0b101, 0b101, 0b111, 0b001, 0b001}, /* 4 */
	{0b111, 0b100, 0b111, 0b001, 0b111}, /* 5 */
	{0b111, 0b100, 0b111, 0b101, 0b111}, /* 6 */
	{0b111, 0b001, 0b001, 0b001, 0b001}, /* 7 */
	{0b111, 0b101, 0b111, 0b101, 0b111}, /* 8 */
	{0b111, 0b101, 0b111, 0b001, 0b111}, /* 9 */
};

/* 把一个数字放大画出来，返回占用宽度 */
static uint32_t big_digit(uint32_t x0, uint32_t y0, int d, uint32_t scale,
			  uint8_t r, uint8_t g, uint8_t b)
{
	for (int row = 0; row < 5; row++)
		for (int col = 0; col < 3; col++)
			if (digit3x5[d][row] & (0b100 >> col))
				rect(x0 + col * scale, y0 + row * scale,
				     scale, scale, r, g, b);
	return 3 * scale + scale; /* 字宽 + 字距 */
}

static void draw_number(uint32_t cx, uint32_t cy, unsigned n, uint32_t scale,
			uint8_t r, uint8_t g, uint8_t b)
{
	char buf[16];
	int len;

	snprintf(buf, sizeof(buf), "%u", n);
	len = (int)strlen(buf);
	if (len > 6) { /* 太长就取低 6 位 */
		memmove(buf, buf + len - 6, 7);
		len = 6;
	}

	uint32_t total = (uint32_t)len * 4 * scale - scale;
	uint32_t x = cx > total / 2 ? cx - total / 2 : 0;

	for (int i = 0; i < len; i++)
		x += big_digit(x, cy, buf[i] - '0', scale, r, g, b);
}

/* 8 条标准彩条 */
static void draw_bars(void)
{
	static const uint8_t col[8][3] = {
		{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
		{255, 0, 255},   {255, 0, 0},   {0, 0, 255},   {0, 0, 0},
	};
	uint32_t bw = W / 8;

	for (int i = 0; i < 8; i++)
		rect(i * bw, 0, (i == 7 ? W - 7 * bw : bw), H,
		     col[i][0], col[i][1], col[i][2]);
}

/*
 * legacy SETCRTC：把某个 crtc 绑到 connector 上并扫描 fb。
 * 注意 mode_valid 必须置 1 —— 否则内核认为"没传模式"，直接返回 EINVAL。
 * （B27 就栽在这上面：connector/crtc/模式都找对了，SETCRTC 一直是 Invalid argument。）
 */
static int set_crtc_on(uint32_t crtc_id, uint32_t fb_id, uint32_t conn_id,
		       const struct drm_mode_modeinfo *mode)
{
	struct drm_mode_crtc set;

	memset(&set, 0, sizeof(set));
	set.crtc_id = crtc_id;
	set.fb_id = fb_id;
	set.mode_valid = 1;
	set.mode = *mode;
	set.count_connectors = 1;
	set.set_connectors_ptr = (uint64_t)(uintptr_t)&conn_id;

	if (ioctl(fd, DRM_IOCTL_MODE_SETCRTC, &set)) {
		printf("SETCRTC crtc=%u → %s\n", crtc_id, strerror(errno));
		return -errno;
	}
	printf("SETCRTC crtc=%u 成功\n", crtc_id);
	return 0;
}

/*
 * 把 connector 的 "DPMS" 属性强制设成 ON(0)。
 * 为什么需要：msm 的 fbdev 起来时 blank=4（POWERDOWN），legacy SETCRTC 只配 CRTC
 * 不解除 DPMS。fb0/blank=4 意味着 scanout 管道是关的 —— 寄存器/DSI/时钟全对，屏就是黑。
 * （B28 定位：SETCRTC 成功但 fb0/blank 仍是 4，屏幕只有背光。）
 */
static void set_dpms_on(uint32_t conn_id)
{
	struct drm_mode_obj_get_properties props;
	struct drm_mode_get_property gp;
	uint32_t *ids;
	uint64_t *vals;
	uint32_t count;

	memset(&props, 0, sizeof(props));
	props.obj_id = conn_id;
	props.obj_type = DRM_MODE_OBJECT_CONNECTOR;
	if (ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &props)) {
		printf("OBJ_GETPROPERTIES(conn=%u) 失败: %s\n", conn_id, strerror(errno));
		return;
	}
	count = props.count_props;
	if (!count) {
		printf("connector %u 无属性\n", conn_id);
		return;
	}
	ids = calloc(count, sizeof(uint32_t));
	vals = calloc(count, sizeof(uint64_t));
	if (!ids || !vals)
		return;
	props.props_ptr = (uint64_t)(uintptr_t)ids;
	props.prop_values_ptr = (uint64_t)(uintptr_t)vals;
	if (ioctl(fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &props)) {
		printf("OBJ_GETPROPERTIES(2) 失败: %s\n", strerror(errno));
		free(ids); free(vals);
		return;
	}

	for (uint32_t i = 0; i < count; i++) {
		memset(&gp, 0, sizeof(gp));
		gp.prop_id = ids[i];
		if (ioctl(fd, DRM_IOCTL_MODE_GETPROPERTY, &gp))
			continue;
		if (strcmp(gp.name, "DPMS"))
			continue;
		printf("connector %u DPMS = %llu（0=on）\n", conn_id, vals[i]);
		if (vals[i] != DRM_MODE_DPMS_ON) {
			struct drm_mode_connector_set_property sp;

			memset(&sp, 0, sizeof(sp));
			sp.connector_id = conn_id;
			sp.prop_id = ids[i];
			sp.value = DRM_MODE_DPMS_ON;
			if (ioctl(fd, DRM_IOCTL_MODE_SETPROPERTY, &sp))
				printf("SETPROPERTY DPMS=ON 失败: %s\n", strerror(errno));
			else
				printf("SETPROPERTY DPMS=ON 成功\n");
		}
		break;
	}
	free(ids);
	free(vals);
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "";
	const char *pattern = argc > 2 ? argv[2] : "bars";
	int hold = argc > 3 ? atoi(argv[3]) : 900;

	/*
	 * 第一步永远是解绑 fbcon。
	 * 探针里 msm 一注册 framebuffer，fbcon 就绑到 fb0 上；之后任何一条 printk
	 * 都会让它重绘整屏，把我们画的东西刷掉 —— 那种"只有背光"和"链路没通"
	 * 长得一模一样，必须先排除掉。
	 */
	setvbuf(stdout, NULL, _IOLBF, 0);
	printf("=== drmpaint: pattern=%s hold=%d ===\n", pattern, hold);
	detach_fbcon();

	/*
	 * fbdev 后端：这块板子上一次写整屏会失败（"framebuffer is not in
	 * virtual address space"），所以按整行对齐的小块写。
	 * 这条路不依赖 DRM 卡号，比 DRM 路径更稳。
	 */
	if (!strncmp(dev, "/dev/fb", 7)) {
		struct fb_var_screeninfo var;
		struct fb_fix_screeninfo fix;
		uint8_t *buf;
		size_t size, chunk;
		uint32_t lines_per_chunk;

		/* 每一步都单独报，失败点不同指向完全不同的问题：
		 *   open 失败      → 设备节点不存在 / 权限
		 *   GET_FSCREENINFO 失败 → 不是 fbdev 或者驱动没实现
		 *   write 失败     → 内存没有内核映射（"not in virtual address space" 那一类） */
		fd = open(dev, O_RDWR);
		printf("open(%s) rc=%d%s\n", dev, fd,
		       fd < 0 ? strerror(errno) : "");
		if (fd < 0)
			return 1;

		if (ioctl(fd, FBIOGET_VSCREENINFO, &var)) {
			printf("FBIOGET_VSCREENINFO 失败: %s\n", strerror(errno));
			return 1;
		}
		if (ioctl(fd, FBIOGET_FSCREENINFO, &fix)) {
			printf("FBIOGET_FSCREENINFO 失败: %s\n", strerror(errno));
			return 1;
		}

		W = var.xres;
		H = var.yres;
		PITCH = fix.line_length ? fix.line_length : var.xres * 4;
		size = (size_t)PITCH * H;

		printf("fbdev: %ux%u pitch=%u bpp=%u smem_len=%u size=%zu accel=%u\n",
		       W, H, PITCH, var.bits_per_pixel, fix.smem_len, size, fix.accel);
		printf("caps: visual=%u type=%u\n", var.bits_per_pixel, fix.type);
		fflush(stdout);

		buf = calloc(1, size);
		if (!buf) {
			fprintf(stderr, "malloc %zu 失败\n", size);
			return 1;
		}

		/* 每块取 ≈64 KiB，但必须是 line_length 的整数倍（见下面分块写的注释） */
		lines_per_chunk = 65536 / PITCH;
		if (!lines_per_chunk)
			lines_per_chunk = 1;
		chunk = (size_t)lines_per_chunk * PITCH;
		printf("分块: 每块 %u 行 = %zu 字节，共 %zu 块\n",
		       lines_per_chunk, chunk, (size + chunk - 1) / chunk);

		size_t off = 0;
		int rc = 0;

		for (int t = 0; t < hold; t++) {
			memset(buf, 0, size);
			pix = buf;
			if (!strcmp(pattern, "white")) {
				rect(0, 0, W, H, 0xff, 0xff, 0xff);
			} else if (!strcmp(pattern, "red")) {
				rect(0, 0, W, H, 0xff, 0, 0);
			} else if (!strcmp(pattern, "count")) {
				draw_number(W / 2, H / 2 - 3 * (H / 8), (unsigned)t,
					    H / 8, 0xff, 0xff, 0xff);
			} else {
				draw_bars();
				rect(W / 8, H / 4, W - W / 4, H / 2, 0, 0, 0);
				draw_number(W / 2, H / 4 + H / 20, (unsigned)t,
					    H / 8, 0xff, 0xff, 0x00);
				uint32_t bw = W / 16, bx = (uint32_t)(t % 16) * bw;

				rect(bx, 0, bw, H / 24, 0xff, 0xff, 0xff);
			}

			/*
			 * 分块写：**按整行对齐**，不是按固定字节硬切。
			 * 2160×4=8640 B/行，若按 64 KiB 硬切，块边界会落在行中间，
			 * fbdev 的 dirty 区就不是整行，DRM 侧可能画出半帧/撕裂。
			 * 这里取每块 ≈64 KiB 但必须是 line_length 的整数倍。
			 */
			off = 0;
			rc = 0;
			while (off < size) {
				size_t n = size - off > chunk ? chunk : size - off;
				ssize_t w = pwrite(fd, buf + off, n, (off_t)off);

				if (w != (ssize_t)n) {
					rc = errno;
					printf("write 失败 chunk@%zu n=%zu ret=%zd %s\n",
					       off, n, w, strerror(errno));
					break;
				}
				if (off == 0 || off + n >= size)
					printf("write chunk@%zu n=%zu ret=%zd\n", off, n, w);
				off += n;
			}
			if (rc) {
				fflush(stdout);
				return 1;
			}
			if ((t % 5) == 0) {
				printf("t=%d 已写满一帧（%zu 字节，%zu 块）\n", t, size,
				       (size + chunk - 1) / chunk);
				fflush(stdout);
			}
			sleep(1);
		}
		return 0;
	}

	/*
	 * DRM 后端。卡号不能猜：这块板子上 simpledrm 先占了 card0，
	 * msm 只能拿 card1（B25 实测）。不指定时按 card1→card0→card2 顺序试，
	 * 并打印每个候选的打开结果，避免又出现"打了不存在的卡、什么都没做"。
	 */
	if (!*dev) {
		static const char *cands[] = {
			"/dev/dri/card1", "/dev/dri/card0",
			"/dev/dri/card2", "/dev/dri/card3",
		};

		fd = -1;
		for (unsigned i = 0; i < sizeof(cands) / sizeof(cands[0]); i++) {
			fd = open(cands[i], O_RDWR);
			printf("try %s → fd=%d%s\n", cands[i], fd,
			       fd < 0 ? strerror(errno) : "");
			if (fd >= 0) {
				dev = cands[i];
				break;
			}
		}
		if (fd < 0) {
			printf("/dev/dri 下没有可用卡\n");
			return 1;
		}
	} else {
		fd = open(dev, O_RDWR);
		printf("open(%s) → fd=%d%s\n", dev, fd,
		       fd < 0 ? strerror(errno) : "");
		if (fd < 0)
			return 1;
	}

	struct drm_mode_card_res res;
	uint32_t crtcs[8] = {0}, connectors[8] = {0}, encoders[8] = {0};
	uint32_t fbs[8] = {0};

	memset(&res, 0, sizeof(res));
	res.count_crtcs = 8;
	res.count_connectors = 8;
	res.count_encoders = 8;
	res.count_fbs = 8;
	res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
	res.connector_id_ptr = (uint64_t)(uintptr_t)connectors;
	res.encoder_id_ptr = (uint64_t)(uintptr_t)encoders;
	res.fb_id_ptr = (uint64_t)(uintptr_t)fbs;

	if (ioctl_ret(DRM_IOCTL_MODE_GETRESOURCES, &res, "GETRESOURCES"))
		return 1;

	printf("crtcs=%u connectors=%u\n", res.count_crtcs, res.count_connectors);

	uint32_t crtc_id = 0, conn_id = 0;
	struct drm_mode_modeinfo mode;
	int found = 0;

	for (uint32_t i = 0; i < res.count_connectors && !found; i++) {
		struct drm_mode_get_connector c;
		struct drm_mode_modeinfo modes[32];
		uint32_t encs[8];

		memset(&c, 0, sizeof(c));
		c.connector_id = connectors[i];
		c.count_modes = 32;
		c.modes_ptr = (uint64_t)(uintptr_t)modes;
		c.count_encoders = 8;
		c.encoders_ptr = (uint64_t)(uintptr_t)encs;
		if (ioctl_ret(DRM_IOCTL_MODE_GETCONNECTOR, &c, "GETCONNECTOR"))
			continue;

		printf("connector %u: type=%u status=%u modes=%u\n",
		       c.connector_id, c.connector_type, c.connection, c.count_modes);

		if (c.connection != 1 || !c.count_modes)
			continue;

		mode = modes[0];
		conn_id = c.connector_id;

		uint32_t enc_id = c.encoder_id;

		if (!enc_id && c.count_encoders)
			enc_id = encs[0];
		if (!enc_id)
			continue;

		struct drm_mode_get_encoder e;

		memset(&e, 0, sizeof(e));
		e.encoder_id = enc_id;
		if (ioctl_ret(DRM_IOCTL_MODE_GETENCODER, &e, "GETENCODER"))
			continue;
		crtc_id = e.crtc_id;
		found = 1;
	}

	if (!found) {
		fprintf(stderr, "没找到 connected 且带模式的 connector\n");
		return 1;
	}
	if (!crtc_id && res.count_crtcs)
		crtc_id = crtcs[0];

	printf("connector=%u crtc=%u mode=%ux%u\n",
	       conn_id, crtc_id, mode.hdisplay, mode.vdisplay);

	struct drm_mode_create_dumb dumb;

	memset(&dumb, 0, sizeof(dumb));
	dumb.width = mode.hdisplay;
	dumb.height = mode.vdisplay;
	dumb.bpp = 32;
	if (ioctl_ret(DRM_IOCTL_MODE_CREATE_DUMB, &dumb, "CREATE_DUMB"))
		return 1;

	struct drm_mode_map_dumb map;

	memset(&map, 0, sizeof(map));
	map.handle = dumb.handle;
	if (ioctl_ret(DRM_IOCTL_MODE_MAP_DUMB, &map, "MAP_DUMB"))
		return 1;

	pix = mmap(NULL, dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
	if (pix == MAP_FAILED) {
		fprintf(stderr, "mmap 失败: %s\n", strerror(errno));
		return 1;
	}
	W = dumb.width;
	H = dumb.height;
	PITCH = dumb.pitch;

	struct drm_mode_fb_cmd2 fbc;

	memset(&fbc, 0, sizeof(fbc));
	fbc.width = W;
	fbc.height = H;
	fbc.pixel_format = DRM_FORMAT_XRGB8888;
	fbc.handles[0] = dumb.handle;
	fbc.pitches[0] = PITCH;
	if (ioctl_ret(DRM_IOCTL_MODE_ADDFB2, &fbc, "ADDFB2"))
		return 1;

	/* 先试 connector 自己那条 crtc，失败再逐个 crtc 试（不同 SoC 的 encoder 绑定不一样） */
	if (set_crtc_on(crtc_id, fbc.fb_id, conn_id, &mode)) {
		int ok = 0;

		for (uint32_t i = 0; i < res.count_crtcs && !ok; i++) {
			if (crtcs[i] == crtc_id)
				continue;
			ok = (set_crtc_on(crtcs[i], fbc.fb_id, conn_id, &mode) == 0);
		}
		if (!ok) {
			printf("所有 crtc 都 SETCRTC 失败\n");
			return 1;
		}
	}

	/* SETCRTC 成功后，显式解除 connector 的 DPMS（fbdev 起来时是 POWERDOWN） */
	set_dpms_on(conn_id);

	printf("已上屏 pattern=%s %ux%u\n", pattern, W, H);
	fflush(stdout);

	int animate = !strcmp(pattern, "bars") || !strcmp(pattern, "count");

	for (int t = 0; t < hold; t++) {
		if (!strcmp(pattern, "white")) {
			rect(0, 0, W, H, 0xff, 0xff, 0xff);
		} else if (!strcmp(pattern, "red")) {
			rect(0, 0, W, H, 0xff, 0, 0);
		} else if (!strcmp(pattern, "green")) {
			rect(0, 0, W, H, 0, 0xff, 0);
		} else if (!strcmp(pattern, "blue")) {
			rect(0, 0, W, H, 0, 0, 0xff);
		} else if (!strcmp(pattern, "ramp")) {
			for (uint32_t y = 0; y < H; y++)
				for (uint32_t x = 0; x < W; x++)
					put(x, y, x * 255 / (W - 1),
					    y * 255 / (H - 1), 128);
		} else if (!strcmp(pattern, "noise")) {
			for (uint32_t y = 0; y < H; y++)
				for (uint32_t x = 0; x < W; x++) {
					uint32_t v = (uint32_t)rand();
					put(x, y, v, v >> 8, v >> 16);
				}
		} else if (!strcmp(pattern, "count")) {
			rect(0, 0, W, H, 0, 0, 0);
			draw_number(W / 2, H / 2 - 3 * (H / 8), (unsigned)t,
				    H / 8, 0xff, 0xff, 0xff);
		} else { /* bars */
			draw_bars();
			/* 中间挖一块黑底，数字才看得清 */
			rect(W / 8, H / 4, W - W / 4, H / 2, 0, 0, 0);
			draw_number(W / 2, H / 4 + H / 20, (unsigned)t, H / 8,
				    0xff, 0xff, 0x00);
			/* 顶部一个随时间向右走的白块：动 = 一定在扫描 */
			uint32_t bw = W / 16, bx = (uint32_t)(t % 16) * (W / 16);

			rect(bx, 0, bw, H / 24, 0xff, 0xff, 0xff);
		}

		if (!animate) {
			printf("静态画面已上屏，保持 %d 秒\n", hold);
			fflush(stdout);
			sleep(hold);
			return 0;
		}
		if ((t % 5) == 0) {
			printf("t=%d\n", t);
			fflush(stdout);
		}
		sleep(1);
	}
	return 0;
}
