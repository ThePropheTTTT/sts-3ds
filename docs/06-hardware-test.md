# 真机测试与诊断

这份文档针对 **真机**（New 3DS / 2DS 系列）。模拟器（Azahar）仍然是最快的迭代环境，但模拟器不会暴露真机的 DSP 固件、SD 写入和内存碎片问题，所以每次发版前至少要跑一次这里的流程。

## 1. 拷贝文件

SD 卡根目录应至少有以下内容：

```
/3ds/sts3ds.3dsx
/3ds/sts3ds/audio/act1.pcm
/3ds/sts3ds/audio/act2.pcm
/3ds/sts3ds/audio/act3.pcm
/3ds/sts3ds/audio/sfx/*.pcm
```

- `sts3ds.3dsx` 可以放在 `/3ds/` 下任意位置；同目录的 `.smdh` 用于 Homebrew Launcher 的标题和图标。
- 音频是可选的：缺少 `act1.pcm` 只会得到一条 WARN，游戏静音继续。SFX 目录同理。
- 首次启动会自动创建 `/3ds/sts3ds/`，并写入日志。

## 2. 主机侧预检

在仓库根目录运行：

```powershell
python tools/preflight.py
python tools/preflight.py --sdmc E:\
```

第一条检查 `port/build/sts3ds.3dsx`、SMDH、romfs 和生成的全部 `.t3x` 是否齐全；第二条额外读取 SD 卡上的 `3ds/sts3ds/log.txt` 和 `crash.txt`，汇总 `[FAIL]` / `[WARN]` 与崩溃寄存器。

- 退出码 0：包完整，SD 上没有阻塞问题。
- 退出码 1：缺文件、日志有 FAIL 行，或 `crash.txt` 里记录了崩溃。
- 警告（音乐/音效缺失）不会让退出码变成 1。

## 3. 启动自检

启动时日志会逐阶段写入：

```
[stage] romfsInit
[stage] gfxInitDefault
[stage] C3D_Init
[stage] C2D_Init
[stage] C2D_Prepare
[stage] font_init
[stage] load_art
[stage] audio_init
[stage] save_load
[stage] boot checks
```

之后会检查字体、场景、怪物、卡面、UI、地图、遗物、视觉特效等 `.t3x`，并核对 `sheet_load()` 的真实解码结果（“资源加载”一行）；再检查 SD 可写性、线性内存和应用内存，并记录固件/内核版本。失败行会排在最上面，不会被“还有 N 项”挡住。

- **没有失败**：直接进入游戏。
- **有失败**：上屏自动停在“STS 3DS 启动自检”，每行标 `X`（失败）、`!`（警告）、`+`（通过）；下屏显示日志尾部。按 A 或 START 继续。
- **强制查看**：启动时按住 SELECT，也会停在自检屏（即使全部通过）。

## 4. 游戏中查看日志

任意界面按 **L + R + SELECT**：

- 上屏：当前阶段、失败/警告数、所有检查项。
- 下屏：最近 8 行日志和日志路径。
- 按 B（或 START / SELECT）关闭。

这个覆盖层不依赖当前场景的按键逻辑，即使某个界面卡住也能打开。覆盖层打开期间触摸输入被暂停，避免误触。

## 5. 崩溃记录

`diag_init()` 安装 libctru 的异常处理器。未捕获异常时：

1. 把异常类型、FSR/FAR、FP 寄存器、r0-r12、sp/lr/pc/cpsr 写入
   `sdmc:/3ds/sts3ds/crash.txt`；
2. 关闭文件；
3. 交给系统默认处理器（通常显示“发生错误”界面）。

下次启动会把上一份日志/崩溃记录轮换为 `log.prev.txt` / `crash.prev.txt`，所以不会覆盖证据；如果轮换前的 `crash.txt` 里有 `*** crash ***`，这次启动会多出一条 `上次运行崩溃` 的 FAIL 并停在自检屏。该提示只在崩溃后的下一次启动出现一次；确认旧记录无用后删除 `crash.prev.txt` 即可清理证据。

注意：Azahar 默认不会把 CPU 异常交给 `threadOnException`（需要 Kernel11 的 UNITINFO），所以崩溃转储要在真机/Luma 环境验证；模拟器里能验证的是日志轮换、自检和覆盖层。

## 6. 常见失败

| 现象 | 含义 | 处理 |
| --- | --- | --- |
| `load_art` 后黑屏/缺图 | 某个 `.t3x` 没打进 romfs，或 SD 兜底也不存在 | 重跑 `tools/to_t3x.sh`，确认 `port/build/romfs/*.t3x`；看日志里 `sheet X: MISSING` |
| `romfsInit` FAIL | 3dsx 不是用 `--romfs=build/romfs` 打包的 | 重新执行 `port/build.sh` |
| `SD 可写` FAIL | SD 写保护/卡坏/路径不存在 | 检查卡、`/3ds/sts3ds` 权限；没有日志时优先查这个 |
| `线性内存` FAIL | 音频或临时分配吃掉了 linear heap | 先移除 `audio/*.pcm` 排除音频；看 `log.txt` 的 `music/sfx` 行 |
| `应用内存` FAIL | 资源/存档占用过高 | 记录日志并对比上次构建；不要在这个状态下继续测试 |
| 音乐 WARN / `ndspInit` 失败 | 真机缺少 DSP 固件或音频文件 | 游戏会静音继续；补 `audio/*.pcm`，或确认机器的 DSP 固件可用 |
| 有 `crash.txt` 但没有系统报错 | 进程被强制结束或异常处理器没跑到 | 看最后一条 `[stage]` 和日志时间戳，缩小到具体阶段 |

## 7. 自检构建（CARD_TEST）

```powershell
& 'E:/msys64/usr/bin/bash.exe' -lc 'CARD_TEST=1 bash /e/vibe_lab/sts-for-3ds/port/build.sh'
```

产物是 `port/build/sts3ds_test.3dsx`，启动后直接跑断言并显示
`CARD TEST <n> cases 0 failed`。这个构建**不**显示启动自检屏，也不读写自动存档；它仍会写 `sdmc:/3ds/sts3ds/log.txt`，方便把断言失败带回来。
