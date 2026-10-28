# source 这个文件进入项目环境
export W="$HOME/Desktop/YT-K606F-mainline"
export PATH="$W/toolchain/bin:$PATH"
export CCACHE_CONFIGPATH="$W/out/ccache.conf"
export CCACHE_DIR="$W/out/ccache"
export http_proxy=http://127.0.0.1:7897 https_proxy=http://127.0.0.1:7897
export no_proxy=localhost,127.0.0.1,192.168.0.0/16,mirrors.tuna.tsinghua.edu.cn
echo "env ready: W=$W  clang=$(clang --version|head -1|cut -d' ' -f3)  ccache=$(ccache --version|head -1|cut -d' ' -f3)"
