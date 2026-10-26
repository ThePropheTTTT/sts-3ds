# Mod 转译器：把 Steam 创意工坊的 Java mod 搬到 3DS

**状态：只写了文档，代码未开工。** 这是有意压后的活——本体（第 2/3 幕、其余 `use()` 卡牌效果、商店/事件面板、真机性能）先做完，再回来碰这里。写这一页的原因：调研已经做过一轮，结论不落盘就会重做。

本文的每个数字都来自本机实测（见「参考资料」），不是估计。

## 结论先行

1. **Java mod 不能在 3DS 上"直接跑"**。3DS 没有 JVM；mod 的 class 文件依赖整个 `com.megacrit.cardcrawl` 对象模型（`AbstractCard`/`AbstractMonster`/`AbstractPower`/`AbstractDungeon`），而这些对象在本体里是我们用 C 重写的等价物，不是同一套 API。Spine 特效、VFX、Shader 也不存在。
2. **不该"生成 C 源码"**。把 `cards/ShadeCrossSlash` 这种「读 4 个 Power 层数 → 三路分支 → 每路 3 次 AoE + 条件抽牌 + 挂 Power」的方法翻成 C，等于在 C 里重新实现一遍 Java 的语义（继承、虚调用、对象别名、动作队列），而且每次换 mod 都要从头再来。
3. **可行路线是「PC 端打包器 + 3DS 端栈式 VM + 原生 API 垫片」**：PC 上把 mod 的 class 解析成简化字节码 IR、把文本/图片/音频抽成我们的资源格式、出一份覆盖率报告；3DS 上跑一个只认识 IR 的小 VM，VM 调用的是我们的原生函数（`battle_play` 那一层的等价物）。垫片缺失的 API（VFX、音效）空实现或映射到现有音效。

## 目标与非目标

目标：给定一个 `ninja.jar`（或任意 BaseMod `CustomCard` 系 mod），产出一个 `mod.ninja.pk3` 之类的**内容包**，塞进 SD 卡 `sdmc:/3ds/sts3ds/mods/` 后，游戏里能：
- 角色选择页多一个角色（蕾忍），有起始牌组、起始遗物、最大生命；
- 卡牌/遗物/药水/事件按 mod 自己的文本、图片显示；
- 卡牌效果在战斗里按原逻辑生效（允许 VFX/音效降级或缺失）。

非目标：像素级复刻 VFX/动画；支持任意 Java 语法（只支持「卡牌效果那类代码」的子集）；在 3DS 上跑真 JVM。

## 总体架构

```
PC 侧（Python，跑在有 JDK 的机器上）
  ninja.jar
    ├─ unpack    解 jar → classes / images / audio / localization JSON
    ├─ decompile CFR（.tmp/cfr.jar 已验证可用）
    ├─ lift      javap/CFR 结果 → 栈式 IR（自定指令集，见下）
    └─ pack      → assets/mod/ninja/{code.bin, strings.json, art/, sfx/}
                 → 覆盖率报告（哪些类整体翻译成功、哪些动作是空垫片）

3DS 侧（C，port/source/mod.c 新文件）
  mod_load()  读 code.bin + strings.json，注册卡牌/遗物/药水/事件
  mod_vm_run() 栈式 VM：栈 64 槽、局部变量寄存器 16 个、无 GC（对象是稳定下标）
  垫片层      mod_api_*()：DamageAction → damage_enemy_typed()、GainBlockAction → 加格挡、
              ApplyPowerAction → 已有 Power 表、DrawCardAction → draw_cards()、
              VFX 类 → 空实现、SFX 类 → audio_sfx()
```

## IR 设计（草案）

一条指令 = `opcode(u8) + a(i16) + b(i16)`（4 字节），栈式求值：

| 类别 | 指令 | 说明 |
| --- | --- | --- |
| 常量/变量 | `PUSHI` `PUSHS` `LOAD` `STORE` | 整数、字符串表下标、局部槽 |
| 算术/比较 | `ADD` `SUB` `MUL` `DIV` `CMP*` `JMP` `JZ` | 只支持 int |
| 对象 | `GETFIELD` `SETFIELD` `NEW` | 字段是「对象表 + 偏移」，不做继承 |
| 调用 | `CALL_API` `CALL_MOD` | 前者进垫片表，后者进同包内的 mod 方法 |
| 特殊 | `RET` `FINALLY` `NOP` | |

关键简化：**卡牌效果方法里没有真并发、没有异常处理语义依赖、对象几乎都是单例**。凡是 IR 搬不动的结构（lambda、匿名类、反射、`instanceof` 链），打包器直接把它标成「不支持」，卡牌降级为「只做一部分效果 + 报告里列出」，而不是生成错误代码。

## 垫片映射（第一批要做的 10 个）

按实测出现频次排（见参考资料）：

| mod 动作 | 垫片 | 本体里已有的等价物 |
| --- | --- | --- |
| `ApplyPowerAction` ×66 | `mod_api_apply_power(target, powerId, amount)` | `apply_weak`/`apply_vulnerable` + 敌人字段（strength/plated/artifact…） |
| `NinjutsuAction` ×30 | 自定义：先按 mod 自己的 Power 数值处理 | 需要新 Power（见下） |
| `DamageAllEnemiesAction` ×14 | `mod_api_damage_all(amount, times)` | `damage_enemy_typed` 遍历 |
| `DrawCardAction` ×14 | `mod_api_draw(n)` | `draw_cards()` |
| `DamageAction` ×11 | `mod_api_damage(target, amount)` | `damage_enemy_typed` |
| `GainBlockAction` ×11 | `mod_api_block(target, n)` | `card_block()` 那一路 |
| `GainEnergyAction` ×7 | `mod_api_energy(n)` | `g_battle.energy` |
| `ScienceAction` ×8 | 自定义 | 需要 mod 自己的 Power |
| `BetterDiscardPileToHandAction`（StSLib） | `mod_api_discard_to_hand(n)` | `battle_find_card` + 手牌 |
| `VFX`/`AbstractGameEffect` | 空实现 | —— |

## 资源管线

1. **卡面**：现在卡图是烘焙进静态页的（`cards_gen.h:26` `STS_CARD_ART_PAGES 2`，`main.c:456` `s_art_sheets[p] = sheet_load(path)`）。mod 卡片数量未知，必须改成**动态页**：编译期页数只覆盖本体 133 张（`cards_gen.h:25` `STS_CARD_COUNT 133`），运行时按需 `sheet_load("mods/ninja/art/cards_<n>.t3x")`。
2. **字体**：`tools/bake_font.py` 按语料烘焙。mod 带新汉字时必须把 mod 文本并进语料重烘（否则显示成方框）。打包器要输出「缺字清单」。
3. **音频**：mod 的 `.ogg` → 我们已有的 PCM 流程（`assets/audio/sfx/*.pcm`，见 `docs/04-audio.md`）；BGM 缺省就不播（CSND 回退只支持 BGM）。
4. **文本**：mod 的 `localization/zhs/*.json` → `strings.json`，key 前缀 `ninja.`，运行时查表，不编译进二进制（这点和本体的 `l10n_gen.h` 不同——mod 文本不能静态编进固件）。

## 存档 v2

现在的存档是 `STS3` + `SAVE_VERSION 1`（`save.c:14`），条目校验 `CARD_INDEX(entry) < STS_CARD_COUNT`（`save.c:49`）。加载带 mod 的存档要：
- 版本升到 2，加一个 **内容包指纹**（包名 + 卡牌数 + 哈希）——指纹不符时拒绝加载，而不是读出一堆错位的卡；
- 卡牌 id 空间拆成「本体 0..132」+「mod 从 133 起、按包内顺序」，卸载 mod 时旧存档里的 mod 卡直接判为无效。

## 里程碑

- **S0 覆盖率报告（PC，1~2 天）**：`tools/mod_pack.py --report` 输出「92 张卡：4 张可机械翻译、16 张带状态判断、72 张依赖自定义动作/Power」。这一步不产出能跑的东西，但能让人知道「这个 mod 到底离能跑差多远」。
- **S1 最小 VM + 10 个垫片（3DS）**：让 `Strike_Ninja` / `Defend_Ninja` 两张纯标准动作的卡在 `CARD_TEST` 里跑通（伤害/格挡对得上）。这是唯一能证明 VM 方向成立的最小实验。
- **S2 角色入口**：角色选择页多一个角色、起始牌组、卡面动态页、缺字表。
- **S3 补齐 32 个 Power + 38 个 Action**：到这一步 mod 卡牌才真正"能玩"。
- **S4 存档 v2 + 打包器正式化**。

## 风险与已知坑

- **最大的坑不是 VM，是 72 张卡依赖自定义动作**：`NinjutsuAction`（30 次）这类东西本质是「本 mod 的私有机制」，垫片不可能通用。现实期望是**按 mod 写适配层**（一个 `mods/ninja/adapter.c`，注册 `ninja_apply_power` 之类的回调），而不是追求零适配。
- 3DS 内存：单个内容包别超过 ~20 MB（图片占大头，t3x 是 rgba4444 压过 LZ11 的）。
- 性能：VM 只跑「出牌瞬间」的几百条指令，不在每帧循环里，压力可控。
- 许可证：创意工坊 mod 的分发条款各不相同，内容包只落地到 SD 卡，不进固件。

## 参考资料（本机实测，2026-10 调研）

- `E:\SteamLibrary\steamapps\workshop\content\646570\3468618252\ninja.jar`：44.2 MB、726 条目、192 class、343 图片、154 音频、中英两套 localization JSON。
- 内容构成：92 个卡牌类、32 个 Power、38 个 Action、11 遗物、4 药水、6 事件、3 角色类、7 个 patch 类（`AbstractCardEnum`/`CardTagsEnum`/`LibraryTypeEnum` 等）；卡牌基于 BaseMod `CustomCard`，另用 StSLib 的 `BetterDiscardPileToHandAction`；自定义标签 `BLADE`/`NINJUTSU`、关键字 `ninjamod:...`。
- 动作频次：`ApplyPowerAction` 66、`NinjutsuAction` 30、`DamageAllEnemiesAction` 14、`DrawCardAction` 14、`DamageAction` 11、`GainBlockAction` 11、`GainEnergyAction` 7、`ScienceAction` 8。
- 反编译样例 `cards/ShadeCrossSlash`：读 `LexKela`/`DimDeadTreePower`/`AmoXilin`/`ScarePower` 层数后三路分支，每路 3 次 AoE + 条件抽牌 + 挂 Power。
- 本机可用的工具：CFR 2.16（`.tmp/cfr.jar`，2.16 MB）、JDK8（`C:\Program Files\Android\jdk\jdk-8.0.302.8-hotspot\jdk8u302-b08\bin`，`javap` 可用）、ffmpeg（Gyan.FFmpeg winget 包）。反编译产物在 `.tmp/ninja_src`（191 个 `.java`）。
- 接缝位置：`battle.c:2873` `int battle_play(int slot, int target)`（现状是按 id 大量 `strcmp` 手写效果，mod 卡必须走新的分发表而不是往这里加分支）。
