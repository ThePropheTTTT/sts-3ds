# 音频：把三幕 BGM 放到 SD 卡上

移植版目前只做**背景音乐**，不做音效。音乐从正版 `desktop-1.0.jar` 的 `audio/music/` 提取，转成 22050 Hz、单声道、16 位 PCM，运行时从 SD 卡流式读取。

## 生成

需要 `ffmpeg` 在 `PATH` 里（或设置 `FFMPEG` 环境变量）。

```powershell
python tools/extract_audio.py --jar <正版 desktop-1.0.jar> --dest <sdmc>/3ds/sts3ds/audio
```

脚本会写出三个文件：

| 文件 | 对应曲目 |
|---|---|
| `act1.pcm` | `audio/music/STS_Level1_NewMix_v1.ogg` |
| `act2.pcm` | `audio/music/STS_Level2_NewMix_v1.ogg` |
| `act3.pcm` | `audio/music/STS_Level3_v2.ogg` |

每个文件约 9–10 MB。`assets/audio/` 被 `.gitignore` 忽略，不随仓库分发。

## 运行时

`port/source/audio.c` 先尝试 NDSP：

- 背景音乐按 1/3 秒一块、双缓冲从文件读，DSP 播完一块再补下一块，整首循环。
- NDSP 需要 SD 卡上的 `/3ds/dspfirm.cdc`。这是用户自己机器的 DSP 固件，仓库不提供，也不能从游戏 jar 里取。
- 真机没有该文件会初始化失败，自动回退 CSND。

CSND 回退：

- CSND 不需要 DSP 固件，但只能一次给出一段连续内存，所以把整首 PCM 读进线性内存，再让硬件把这一整块循环播放。
- 老 3DS 的线性堆较小，约 9–10 MB 的曲目可能分配失败；失败时静音，不崩溃。
- 这条路径没有流式读取，属于“能响就行”的后备，不是首选。Azahar 的 CSND HLE 目前会把通道配置写入，但 `csndIsPlaying` 仍返回 0，所以模拟器里听不到这条路径；真机的 CSND 服务正常时才应出声。

文件缺失时保持静音。切幕时 `audio_set_act()` 会停掉当前曲目，按 `g_run.act` 打开对应的 `act1/2/3.pcm`。

## Azahar 备注

Azahar 的 DSP 是 HLE，不需要真正的 DSP 固件；但 libctru 的 `ndspInit()` 会先找 `/3ds/dspfirm.cdc`，找不到就直接返回 `0xD880A7FA`。由于 Azahar 的 `LoadComponent` 不校验固件内容，可以在模拟器的 SD 卡根目录放一个 4 KB 全零的 `3ds/dspfirm.cdc` 作为占位，让 NDSP 路径跑起来。这个占位文件只适用于 HLE 模拟器，真机必须换成合法固件。

## 验证

`CARD_TEST` 构建的报告屏会显示一行音频状态，例如：

```
音乐 act1 3:21 PLAYING NDSP r=22
```

`r` 是已经补过的 NDSP 缓冲块数；它随时间增长，说明 DSP 确实在消费流。缺文件时显示 `音乐 act1.pcm 缺失`。

## 已知缺口

- 没有音效、没有音量/静音设置、没有淡入淡出。
- 没有 Boss 曲和菜单曲，只有三幕普通 BGM。
- CSND 后备一次性占用整首曲目的线性内存。
