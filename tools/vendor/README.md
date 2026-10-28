# tools/vendor

本目录存放 `build-initramfs.sh` 构建 initramfs 所需的静态 busybox 二进制，因体积较大（约 1.9 MB）未纳入仓库。

获取方式（Ubuntu / Debian）：

```sh
# 任选其一，得到 aarch64 静态 busybox 后放到本目录并命名 busybox-aarch64-static
apt download busybox-static
# 或
curl -LO https://github.com/...  # 从任意 busybox aarch64 static 发行版获取
```

`build-initramfs.sh` 中对应变量 `BB=tools/vendor/busybox-aarch64-static`。
