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
- `port/build.sh` 在首次构建时用 `smdhtool` 生成 `build/sts3ds.smdh`，并传给 `3dsxtool --smdh=`；`tools/preflight.py` 会检查它的 `SMDH` magic 和大小。改了图标或标题后删掉旧 `.smdh` 再构建。
- 已在 Azahar 上验证构建、启动自检、日志与覆盖层；**真机首次运行仍按 `docs/06-hardware-test.md` 走一遍**，先看 `sdmc:/3ds/sts3ds/log.txt` 的最后 `[stage]`。

## 第 1 幕遭遇池

第 1 幕普通怪不再由 `setup_encounter` 按层数挑固定组合，而是按 `MonsterHelper.java:832-847` 的两张表抽签（`battle.c` 的 `WEAK_POOL` / `STRONG_POOL` / `setup_act1_pool_encounter`）：

- 前 3 场普通怪只从弱池取：蓝奴隶贩子、邪教徒、颚虫、两只虱虫、两只小史莱姆；
- 第 4 场起从强池取：地精团、大史莱姆、掠夺者、Exordium Thugs、Exordium Wildlife、红奴隶贩子、三只虱虫、五只小史莱姆；
- 场次来自 `act1_monster_rooms()`，即地图上已经 `visited` 的 `NODE_MONSTER` 房间数（`map_enter` 在 `battle_start` 之前就标了 visited，所以当前这场本身也计入）；
- 抽签用 `g_map.map_seed` + 幕数 + 弱/强池做种子，对该池做 Fisher-Yates 洗牌后按下标取，因此同一张地图的同一场次总是同一组怪（可用 `battle_test_encounter_id(n)` 在测试里复现），相邻场次不会重复同一格。

`Enemy enemies[5]` 与 `main.c` 的五敌布局（48px 宽、`84 + i * 62`）就是为 Lots of Slimes 加的：它固定 3 只小尖刺 + 2 只小酸液（`MonsterHelper.spawnManySmallSlimes` 从 3+2 的池里不重复抽取），顺序随机。

已知缺口：`Mugger` 只出现在第 2 幕的 "2 Thieves"；第 2/3 幕仍是 `setup_act2` 的脚本化摆位。

## 模拟器输入的边界

Azahar 的 `GRenderWindow::mousePressEvent` 会直接忽略 `Qt::MouseEventSynthesizedBySystem`。从主机发 `PostMessage`/`SendMessage` 鼠标消息、`mouse_event` 或 `SendInput`，在 Qt 看来都是合成鼠标，因此无法用它们验收触屏拖牌；Windows 的 `InitializeTouchInjection`/`InjectTouchInput` 在当前会话也会返回 87/5（参数错误/拒绝访问）。触屏请在真机上用手写笔/手指验收。

为了在 CI 式截图里仍然覆盖这条路径，`CARD_TEST=1` 构建保留了合成触摸入口（仅测试构建编译，出货包不含）：

- 报告屏按 `B` 循环全部战斗/选牌演示（Automaton → Collector → Gremlin Leader → 第 3 幕背景 → 第 1 幕 Boss 与普通怪（含邪教徒、两只/三只虱虫、Small Slimes 组合、尖刺/酸液小史莱姆与尖刺中史莱姆单只、两只真菌怪与单只真菌怪、地精团、大酸液/大尖刺史莱姆、掠夺者、蓝/红奴隶贩子单只、Exordium Thugs、Exordium Wildlife、五只小史莱姆的 Lots of Slimes，以及第 2 幕城市阵容：打劫的、三只异鸟、双盗贼、天选者+异鸟、邪教徒+天选者、三只邪教徒、寄生兽+真菌怪、哨卫+球体守卫、蛇花、异蛇、百夫长+神秘术士）→ 剩余 Power 卡 → Purity / Forethought 选牌），`SELECT` 从演示回到报告；
- 战斗屏按 `X` 从第三张牌开始拖动并把目标切到 0 号敌人槽，按 `Y` 在该目标上松手出牌，按 `B` 把手牌放回扇形取消。

需要给奖励、篝火、升级这三个屏截图时，用同一套构建加 `UI_DEMO`（0 升级、1 奖励、2 篝火、3 True Grit+ 选牌、4 Searing Blow 升级屏、5 五张升级文本卡：Ghostly+/Forethought+/Searing+2/Discovery+/Burn+），启动后直接停在对应画面；标题屏用出货构建截：

```powershell
& 'E:/msys64/usr/bin/bash.exe' -lc 'CARD_TEST=1 UI_DEMO=1 bash /e/vibe_lab/sts-for-3ds/port/build.sh'
```

截图驱动先发 `WM_SYSCOMMAND / SC_RESTORE`，再 `SetWindowPos` 到 760×1100；如果窗口还停在最大化/全屏，`SetWindowPos` 不会改尺寸，抓到的是 1280×720 里的一小块原生画面。

这组按键走的是和硬件相同的 `handle_touch_fight()`/`touchPosition` 路径，能验证“牌跟随手指、目标框/文字随横移切换、放回手牌不扣能量”。
