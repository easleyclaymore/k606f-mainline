/*
 * i2cdump8 -- 极简寄存器读取工具（PSA8331 eDP 桥调试用）
 *
 * 为什么自己写：探针 initramfs 里只有 busybox，没有 i2c-tools；Android 侧
 * 也没有可用的 i2c 读写命令。而"把工作正常状态（Android）和失败状态（自制内核）
 * 的桥寄存器全量 dump 出来做 diff"是目前唯一还没试过、且信息量最大的手段。
 *
 * 静态编译后可以：
 *   1) 放进 initramfs 的 /bin/，在探针系统里跑；
 *   2) 用 adb push 进 Android（同架构）跑。
 *
 * 用法： i2cdump8 <bus> <addr7> [reg_from] [reg_to]
 *   例： i2cdump8 1 0x33          # 读 bus1 上 0x33 的 0x00..0xFF
 *        i2cdump8 1 0x33 0 0x1f   # 只读前 32 个
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

/* 用户态没有 i2c_smbus_read_byte_data()，直接发 I2C_SMBUS ioctl */
static int smbus_read_byte(int fd, unsigned char reg, int *out)
{
	union i2c_smbus_data data;
	struct i2c_smbus_ioctl_data args = {
		.read_write = I2C_SMBUS_READ,
		.command = reg,
		.size = I2C_SMBUS_BYTE_DATA,
		.data = &data,
	};

	if (ioctl(fd, I2C_SMBUS, &args) < 0)
		return -1;
	*out = data.byte;
	return 0;
}

static int smbus_write_byte(int fd, unsigned char reg, unsigned char val)
{
	union i2c_smbus_data data;
	struct i2c_smbus_ioctl_data args = {
		.read_write = I2C_SMBUS_WRITE,
		.command = reg,
		.size = I2C_SMBUS_BYTE_DATA,
		.data = &data,
	};

	data.byte = val;
	return ioctl(fd, I2C_SMBUS, &args);
}

int main(int argc, char **argv)
{
	char path[64];
	int fd, i, reg;
	int bus = 1, from = 0x00, to = 0xff;
	unsigned long addr = 0x33;

	if (argc < 3) {
		fprintf(stderr,
			"用法:\n"
			"  %s <bus> <addr7> [from] [to]      读寄存器\n"
			"  %s w <bus> <addr7> <reg> <val>    写一个寄存器\n",
			argv[0], argv[0]);
		return 2;
	}

	if (!strcmp(argv[1], "w")) {
		int fd, bus, reg, val;
		char path[64];

		if (argc < 6) {
			fprintf(stderr, "写模式需要: w <bus> <addr7> <reg> <val>\n");
			return 2;
		}
		bus = atoi(argv[2]);
		snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
		fd = open(path, O_RDWR);
		if (fd < 0) {
			fprintf(stderr, "打开 %s 失败: %s\n", path, strerror(errno));
			return 1;
		}
		if (ioctl(fd, I2C_SLAVE, strtoul(argv[3], NULL, 0)) < 0 &&
		    ioctl(fd, I2C_SLAVE_FORCE, strtoul(argv[3], NULL, 0)) < 0) {
			fprintf(stderr, "选中 0x%lx 失败: %s\n",
				strtoul(argv[3], NULL, 0), strerror(errno));
			close(fd);
			return 1;
		}
		reg = (int)strtoul(argv[4], NULL, 0);
		val = (int)strtoul(argv[5], NULL, 0);
		if (smbus_write_byte(fd, (unsigned char)reg, (unsigned char)val) < 0) {
			fprintf(stderr, "写 0x%02x=0x%02x 失败: %s\n",
				reg, val, strerror(errno));
			close(fd);
			return 1;
		}
		printf("写 0x%02x = 0x%02x\n", reg, val);
		close(fd);
		return 0;
	}
	bus = atoi(argv[1]);
	addr = strtoul(argv[2], NULL, 0);
	if (argc > 3)
		from = (int)strtoul(argv[3], NULL, 0);
	if (argc > 4)
		to = (int)strtoul(argv[4], NULL, 0);

	snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
	fd = open(path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "打开 %s 失败: %s\n", path, strerror(errno));
		return 1;
	}
	if (ioctl(fd, I2C_SLAVE, addr) < 0 &&
	    ioctl(fd, I2C_SLAVE_FORCE, addr) < 0) {
		fprintf(stderr, "选中地址 0x%02lx 失败: %s\n", addr, strerror(errno));
		close(fd);
		return 1;
	}

	for (i = 0; i < 4; i++)
		printf("# i2cdump8 bus=%d addr=0x%02lx range=0x%02x-0x%02x\n",
		       bus, addr, from, to);

	for (reg = from; reg <= to; reg++) {
		int v;

		if (smbus_read_byte(fd, (unsigned char)reg, &v) < 0)
			printf("%02x: -- (%s)\n", reg, strerror(errno));
		else
			printf("%02x: %02x\n", reg, v);
	}

	close(fd);
	return 0;
}
