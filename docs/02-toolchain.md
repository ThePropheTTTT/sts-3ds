# 3DS 工具链：从裸机到 `.3dsx`

目标：在 Windows 上能编出可在真机 hbmenu 里跑的 `.3dsx`。**已达成**——`spike/hello3ds` 编出了 134 KB 的 `hello3ds.3dsx`（magic `3DSX`，ARM v6K，入口 `0x100000`）。

这一页记录踩过的坑，因为**下次重装或换机器时 90% 的坑会再来一遍**。

## 最终布局

```
C:\msys64\                          # MSYS2 根（pacman 在这里）
  opt\devkitpro\                    # = /opt/devkitpro
    devkitARM\                      # arm-none-eabi 工具链 (gcc 16.1.0 / binutils 2.46.0)
    libctru\                        # libctru 2.7.0 + citro3d 1.7.1 + citro2d 1.7.0
    tools\bin\                      # 3dsxtool / tex3ds / picasso / smdhtool ...
    examples\3ds\                   # 官方示例（graphics/gpu/2d_shapes 是 citro2d 参考）
  var\cache\pacman\pkg\             # 22 个 dkp 包的缓存
  etc\pacman.conf                   # 只保留 dkp-libs / dkp-windows
```

**为什么不装官方图形安装器**：`devkitProUpdater-3.0.3.exe` 是 NSIS 图形向导弹出，且会把一堆 msys2 包拉进来；本机网络下反而更不可控。实际走的是「MSYS2 base tarball + devkitPro 仓库」的手工路线。

## 步骤（可复现）

### 1. 装一个 MSYS2 根

```powershell
# 清华镜像，42 MB
python tools\dl.py https://mirrors.tuna.tsinghua.edu.cn/msys2/distrib/msys2-x86_64-latest.tar.xz .tmp\msys2-base.tar.xz
# 注意：Windows 自带 bsdtar 不能解 .xz（缺外部 xz），用 Python 的 lzma
python .tmp\unxz.py .tmp\msys2-base.tar.xz C:\
```

### 2. 只留 devkitPro 仓库

`C:\msys64\etc\pacman.conf` 追加（并把 `[msys]`/`[mingw*]` 段删掉，它们要 msys2 密钥而镜像又连不上）：

```ini
[dkp-libs]
SigLevel = Optional TrustAll
Server = https://pkg.devkitpro.org/packages

[dkp-windows]
SigLevel = Optional TrustAll
Server = https://pkg.devkitpro.org/packages/windows/x86_64/

XferCommand = /usr/bin/curl -L -f -s --retry 8 --retry-all-errors --retry-delay 2 --connect-timeout 20 -o %o %u
```

`XferCommand` 别加 `-C -`（pacman 传的 `%o` 带 `.part` 后缀，空文件续传会直接失败）。

### 3. 密钥环

```bash
# pacman-key 是 bash 脚本，没有 .exe，必须用 login shell 跑
bash -lc "pacman-key --init"
python tools\dl.py https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.zst .tmp\dkp-keyring.pkg.tar.zst
bash -lc "cd /path/to/sts-for-3ds/.tmp && pacman -U --noconfirm dkp-keyring.pkg.tar.zst && pacman-key --populate devkitpro"
```

### 4. 装 `3ds-dev`

```bash
bash -lc "pacman -Sy --noconfirm"      # 只拉两个 db，很快
```

然后**不要**指望 `pacman -S 3ds-dev` 能自己下载完：它在 `dkp-windows` 上会随机 `TLS connect error: error:0A000126:SSL routines::unexpected eof while reading`（本机网络对 dkp-windows 的 TLS 连接不稳定），一次失败就整批回滚。

可行做法是「Python 预取 + 离线安装」：

```powershell
python .tmp\prefetch.py     # 解析 dkp-libs.db / dkp-windows.db 的 FILENAME，把 18 个缺的包拉进 cache
```

```bash
bash -lc "cd /var/cache/pacman/pkg && pacman -U --noconfirm --needed *.pkg.tar.zst *.pkg.tar.xz"
```

`pacman -U` 不做仓库校验，只要本地包完整就能装，绕开了下载器。装完 22 个包、约 399 MB。

### 5. 环境变量

`/etc/profile.d/devkit-env.sh` 已经由包写好，login shell 自动生效：

```
DEVKITPRO=/opt/devkitpro
DEVKITARM=/opt/devkitpro/devkitARM
PATH=$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH
```

**关键坑**：直接 `C:\msys64\usr\bin\bash.exe script.sh`（非 login shell）时 `PATH` 是 Windows 的，`ls`/`gcc` 全部 command not found。所有脚本都必须用：

```powershell
& C:\msys64\usr\bin\bash.exe -lc "bash /path/to/sts-for-3ds/....sh"
```

### 6. 没有 `make`

MSYS2 base 自带 bash + coreutils + curl + pacman，但**没有 make**；而 `msys.db` 是 zstd 压缩、Python 3.12 的 `tarfile` 不认、Windows bsdtar 又要外部 `zstd.exe`——为了装一个 make 再折腾一轮不值得。

所以 `spike/hello3ds/build.sh` 直接调编译器，两条命令：

```
arm-none-eabi-gcc $CFLAGS -c source/main.c -o build/main.o
arm-none-eabi-g++ $LDFLAGS -o build/hello3ds.elf build/main.o $LIBS
3dsxtool build/hello3ds.elf build/hello3ds.3dsx
```

其中（抄自官方示例，已实测可链接）：

| 变量 | 值 |
|---|---|
| `ARCH` | `-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft` |
| `CFLAGS` | `-g -Wall -O2 -mword-relocations -ffunction-sections -fdata-sections $(ARCH) -D__3DS__ -I$DEVKITPRO/libctru/include` |
| `LDFLAGS` | `-specs=3dsx.specs -g $(ARCH) -L$DEVKITPRO/libctru/lib -Wl,--gc-sections` |
| `LIBS` | `-lcitro2d -lcitro3d -lctru -lm` |

## 验收结果

```
build/hello3ds.3dsx   134644 bytes   magic: 3DSX
build/hello3ds.elf    1264668 bytes  EXEC / ARM / entry 0x100000
主程序对象 Tag_CPU_arch: v6K          （libctru / citro2d / citro3d 全部 v6K）
```

## 编出来之后怎么跑

1. 把 `spike/hello3ds/build/hello3ds.3dsx` 拷到 SD 卡 `sd:/3ds/hello3ds.3dsx`；
2. 真机上进 hbmenu（Luma3DS 的 Homebrew Launcher），选中运行；
3. 退出按 START。若黑屏，先确认 Luma 里 **New 3DS CPU** 设为 `Clock + L2`。

屏幕内容：上屏用 citro2d 画一个简化的战斗画面（5 张手牌方块 + 橙色能量球 + 敌人方块和血条），下屏用 libctru console 实时显示帧数、CPU/GPU 占用、命令缓冲占用。A 复位、B 减一张牌、X 加一张牌。

## 已知遗留

- 还没有 `make`（也不影响当前构建，但正式工程建议补上，或改用已装好的 `3ds-cmake` 工具链文件）。
- `.3dsx` 没有 SMDH（hbmenu 里会显示成无图标/无标题的那一项）。正式发布前用 `smdhtool` 生成 `.smdh` 并加 `--smdh=` 参数。
- 只验证了「能编出合法 3dsx」，**没有在真机/Citra 上跑过**——第一次上机若崩，先怀疑 `consoleInit` 与 citro2d 共存、以及 `C2D_DrawCircleSolid` 的状态切换。
