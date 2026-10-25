# 音频：三幕 BGM 与音效

音乐从正版 `desktop-1.0.jar` 的 `audio/music/` 提取，音效从 `audio/sound/` 提取，全部转成 22050 Hz、单声道、16 位 PCM。构建时这些 PCM 会被打包进 3dsx 的 romfs；运行时优先从 romfs 读，SD 卡上的副本只作为覆盖/回退。音效在启动时整段读入线性内存，按需播放。

## 生成

需要 `ffmpeg` 在 `PATH` 里（或设置 `FFMPEG` 环境变量）。

```powershell
python tools/extract_audio.py --jar <正版 desktop-1.0.jar>
```

输出到 `assets/audio/`；`port/build.sh` 发现这些文件后会一并放进 3dsx 的 romfs。需要额外在 SD 卡上留一份回退时，再加 `--dest <sdmc>/3ds/sts3ds/audio`。

脚本会写出三个文件：

| 文件 | 对应曲目 |
|---|---|
| `act1.pcm` | `audio/music/STS_Level1_NewMix_v1.ogg` |
| `act2.pcm` | `audio/music/STS_Level2_NewMix_v1.ogg` |
| `act3.pcm` | `audio/music/STS_Level3_v2.ogg` |

每个文件约 9–10 MB，三首合计约 28 MB；加音效后 3dsx 会增大约 29 MB。`assets/audio/` 被 `.gitignore` 忽略，不随仓库分发。若 `assets/audio/` 不存在，构建仍然成功，但会得到一个没有音乐/音效的静音 3dsx（构建日志会提示）。

音效用同一脚本的 `--sfx` 模式生成：

```powershell
python tools/extract_audio.py --sfx --jar <正版 desktop-1.0.jar> --dest <sdmc>/3ds/sts3ds/audio
```

输出到 `assets/audio/sfx/`（`--dest` 时复制到 `<sdmc>/3ds/sts3ds/audio/sfx/`）。这些文件同样会由 `build.sh` 打进 romfs，SD 副本只是回退。文件名与 `port/source/audio.h` 的 `AudioSfx` 枚举一一对应：

| 文件 | 原版 SoundMaster 常量 | 用途 |
|---|---|---|
| `ui_click.pcm` | `UI_CLICK_1` | 菜单点击 |
| `card_select.pcm` | `CARD_SELECT` | 选牌/移动焦点 |
| `card_play.pcm` | `UI_Parchment_3` | 打出一张牌 |
| `card_reject.pcm` | `CARD_REJECT` | 不能打出 |
| `attack_fast.pcm` | `ATTACK_FAST` | 普通攻击 |
| `attack_heavy.pcm` | `ATTACK_HEAVY` | 重击（伤害 ≥ 15） |
| `hit.pcm` | `BLUNT_FAST` | 实际掉血 |
| `block.pcm` | `BLOCK_GAIN` | 获得格挡 |
| `buff.pcm` | `BUFF_1` | 能力牌 |
| `debuff.pcm` | `DEBUFF_1` | 给敌人上负面状态 |
| `end_turn.pcm` | `END_TURN` | 结束回合 |
| `battle_start.pcm` | `BATTLE_START_1` | 进入战斗 |
| `victory.pcm` | `VICTORY` | 战斗胜利 |
| `defeat.pcm` | `DEATH_STINGER` | 玩家死亡 |
| `exhaust.pcm` | `CARD_EXHAUST` | 消耗（预留） |
| `heal.pcm` | `HEAL_1` | 治疗（预留） |
| `gold.pcm` | `GOLD_GAIN` | 获得金币（预留） |

17 个文件合计约 1 MB。表中前 12 项已经接入；`ui_click`、`card_select`、`exhaust`、`heal`、`gold` 已生成但还没有调用点。

## 运行时

`port/source/audio.c` 先尝试 NDSP：

- 背景音乐按 1/3 秒一块、双缓冲从 romfs（或 SD 回退）读，DSP 播完一块再补下一块，整首循环。
- NDSP 需要 SD 卡上的 `/3ds/dspfirm.cdc`。这是用户自己机器的 DSP 固件，仓库不提供，也不能从游戏 jar 里取。
- 真机没有该文件会初始化失败，自动回退 CSND。

CSND 回退：

- CSND 不需要 DSP 固件，但只能一次给出一段连续内存，所以把整首 PCM 读进线性内存，再让硬件把这一整块循环播放。
- 老 3DS 的线性堆较小，约 9–10 MB 的曲目可能分配失败；失败时静音，不崩溃。
- 这条路径没有流式读取，属于“能响就行”的后备，不是首选。Azahar 的 CSND HLE 目前会把通道配置写入，但 `csndIsPlaying` 仍返回 0，所以模拟器里听不到这条路径；真机的 CSND 服务正常时才应出声。

文件缺失时保持静音。切幕时 `audio_set_act()` 会停掉当前曲目，按 `g_run.act` 先找 `romfs:/audio/act1/2/3.pcm`，找不到再找 `sdmc:/3ds/sts3ds/audio/act1/2/3.pcm`。音效同理（`romfs:/audio/sfx/` 优先）。

### 音效

NDSP 初始化成功后，`audio.c` 把 `audio/sfx/*.pcm`（优先 romfs，其次 SD）逐个读入线性内存（单文件上限 600 KB），并给 NDSP 通道 1–23 设好格式；通道 0 留给音乐。`audio_sfx(id)` 找一条空闲通道，清掉旧队列，挂上整段 PCM 后立即播放；所有通道都忙时直接丢弃这次请求，不做排队。CSND 回退只支持 BGM，音效静音。

播放点：

- `battle_play`：攻击牌按伤害选 `attack_fast`/`attack_heavy`，能力牌 `buff`；`main.c` 的 `play_card` 成功后 `card_play`、失败 `card_reject`。
- `enemy_take_turn`：敌人出手前按意图伤害选 `attack_fast`/`attack_heavy`，所以敌我双方都有挥击声，之后才接 `hit`。
- `damage_enemy_typed` / `damage_player_raw`：实际掉血时 `hit`。
- `gain_block`：获得格挡时 `block`。
- `debuff_enemy`：负面状态真的挂上时 `debuff`。
- `exhaust_card`：牌进入消耗堆时 `exhaust`；回血药水与营火休息 `heal`；战斗金币奖励与金币加成 `gold`。
- 战斗中的手牌/目标光标移动 `card_select`，B 取消目标 `card_reject`；地图上移动光标与进入房间 `ui_click`。
- `battle_start` / `battle_end_turn` / `win_battle` / 玩家死亡：`battle_start` / `end_turn` / `victory` / `defeat`。

自动测试会打几百张牌，所以 `card_test_run()` 期间用 `audio_sfx_mute(1)` 静音，跑完再打开，避免测试构建一启动就连续响。

## Azahar 备注

Azahar 的 DSP 是 HLE，不需要真正的 DSP 固件；但 libctru 的 `ndspInit()` 会先找 `/3ds/dspfirm.cdc`，找不到就直接返回 `0xD880A7FA`。由于 Azahar 的 `LoadComponent` 不校验固件内容，可以在模拟器的 SD 卡根目录放一个 4 KB 全零的 `3ds/dspfirm.cdc` 作为占位，让 NDSP 路径跑起来。这个占位文件只适用于 HLE 模拟器，真机必须换成合法固件。

## 验证

`CARD_TEST` 构建的报告屏会显示两行音频状态，例如：

```
音乐 act1 3:21 PLAYING NDSP r=22
音效 17/17 放 0 最近 card_play 已发 3
```

`r` 是已经补过的 NDSP 缓冲块数；它随时间增长，说明 DSP 确实在消费流。第二条依次是已加载音效数、当前占用通道数、最近播放的 clip、累计播放次数。缺文件时分别显示 `音乐 act1.pcm 缺失`、`音效 0/17`。

截图 `.tmp/shots/1400_sfx_report.png`（17/17、已发 0）和 `1404_sfx_report_played.png`（打出打击后已发 3，最近 `card_play`）是 SD 路径的证据；`.tmp/shots/sfx_hit.png` 是在第 2 幕奴隶贩子战斗里按 Y 之后截的报告屏（`最近 hit 已发 4`），说明敌人挥击声与命中声都走了通道；打包后的构建用 `.tmp/shots/1600_audio_pack_report.png` 验收：`音乐 act1 3:21 PLAYING NDSP r=32`、`音效 17/17`，且 `log.txt` 写有 `audio act1: romfs` / `audio sfx: romfs`。Azahar 的 NDSP HLE 不暴露实际发声，能验收的是加载、通道与缓冲推进。

## 已知缺口

- 没有音量/静音设置、没有淡入淡出；音效只在 NDSP 路径可用。
- 17 个片段都有调用点了，但 `ui_click` 目前只覆盖地图；商店、营火和标题屏的点击仍是静音。
- 没有 Boss 曲、菜单曲和角色语音，只有三幕普通 BGM。
- CSND 后备一次性占用整首曲目的线性内存，且不支持音效。
- 三幕 BGM 全打包后 3dsx 约 32 MB；Old 3DS 的内存/加载器可能吃不消，目标机型仍是 New 3DS。若只是快速迭代，可先不放 `assets/audio/`，改用 SD 副本。
