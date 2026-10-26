# 总路线图与交接手册

> 最后更新：2026-10-27（提交 9a0d5a0）。每次完成一个里程碑就顺手更新这里，别让它变成历史文件。

给之后接手的人或 agent：先读完这一页，再按「开工前检查」跑一遍构建与自检，就能接上进度。细节文档在 `docs/01`–`06`，这里只讲**现在在哪、还差什么、怎么继续**。

---

## 1. 这个项目是什么

用 C + citro2d 把《Slay the Spire》的核心流程重写到 Nintendo 3DS 上：三幕地图、卡牌战斗、商店/营火/事件/宝箱、存档与音频。表现层（渲染、输入、音频、存档）全部重写，规则与数值照正版 jar 反编译源码复刻。

移植版用 C + citro2d 重写渲染、输入、音频与存档，卡牌、怪物、地图与音频全部塞进同一个 3dsx，在 3DS 的内存限制内运行。

**游戏名**：正版简中标题美术（`images/ui/title_logo/zhs.png`）写的是「尖塔奇兵」，标题屏已经照它显示；README 与历史文档里的「杀戮尖塔」是社区常用叫法。对外发 demo 时统一用哪个，需要在发布前定一次（建议画面用「尖塔奇兵」，搜索标签两个都写）。

**不是官方产品**：非官方、非营利、仅供学习与自有机型体验；仓库不含游戏资源，构建必须自备正版 jar。见 README 与 `THIRD_PARTY_NOTICES.md`。

---

## 2. 当前快照（2026-10-27）

| 项目 | 状态 |
| --- | --- |
| HEAD | main 与 origin/main 同步（本文件写作时 `9a0d5a0`，最新提交看 `git log`） |
| 自检 | CARD_TEST 构建 = **1197 cases, 0 failed / ALL PASS**（Azahar 模拟器实测） |
| 卡牌 | 133 张铁甲战士卡 + 状态/诅咒（诅咒按原版不可打出）；升级、固有、虚无、消耗、Searing +N 等级均已实现 |
| 药水 | 15 种（6 旧 + 9 新），稀有度 65/25/10，价格 50/75/100 |
| 遗物 | 16 种（`RELIC_COUNT`） |
| 怪物 | 37 个立绘槽位；第 1 幕 5 weak + 8 strong、第 2 幕 5 weak + 8 strong 的遭遇池已接真实抽签 |
| 事件 | 3 个（`STS_EVENT_COUNT`） |
| 幕 | 地图 1–3 幕都能进；**第 3 幕的怪物/Boss 还没做，当前复用第 2 幕内容**（见 §5-P0） |
| 音频 | BGM 流式播放 + 17 个音效（含战斗外界面点击）；触摸点按仍静音 |
| 存档 | `sdmc:/3ds/sts3ds/save.dat`，版本与校验和见 `docs/03-save.md` |
| 真机 | 自检、日志、preflight 已就绪（`docs/06-hardware-test.md`），尚无真机性能数据 |
| 打包 | 只产出 3dsx；CIA/SMDH 发布物还没做 |

工作区里的 `assets/`、`port/source/*_gen.h`、`tools/emu.py` 都是 **gitignore** 的：改贴图/字体不会产生二进制提交，换机器要按 `docs/01` 重新生成。

---

## 3. 玩家现在能玩到什么

- 标题 → 新游戏/继续游戏 → 三幕纸面地图，选路、精英、宝箱、商店、营火、事件、Boss。
- 战斗：抽/弃/消耗、能量、格挡、力量敏捷虚弱易伤脆弱、中毒/再生/荆棘/人工制品、多段攻击、召唤小怪、分裂、逃跑与偷金币、Curl Up、Malleable、Ritual、混乱（Confused）等机制。
- 133 张卡全部可打出；升级屏、牌堆浏览、药水面板、状态面板可用。
- 战斗胜利结算（金币/药水/三选一卡）、商店买卡/遗物/药水/删牌、营火休息或锻造、宝箱、幕间过渡。
- 存档自动写入；读档校验 magic/版本/checksum，用保存的种子重建地图。

---

## 4. 代码地图

```
port/source/
  main.c        屏幕、输入、渲染、HUD、各界面绘制（最长的文件）
  battle.c      战斗规则、卡牌效果、怪物 AI、遭遇池、药水/遗物结算
  battle.h      Battle/Enemy/CardDef 等结构与测试钩子声明
  run.c         地图生成、节点推进、商店/事件/奖励/存档流程
  cardtest.c    全部自检（CARD_TEST 构建时在开机跑，报告屏显示结果）
  save.c        存档读写与校验
  audio.c       NDSP/CSND、BGM 流、音效通道
  font.c        字体渲染（多字号、描边）
  diag.c        自检与日志（sdmc:/3ds/sts3ds/log.txt）
  *_gen.h       由 tools 生成的卡表/怪物/UI/HUD/视觉资源（gitignored）
tools/
  extract_*.py  从正版 jar 抽资源（卡面、怪物 Spine、UI、HUD、音频、视觉）
  gen_cards.py / gen_l10n.py 等  生成 *_gen.h 与文本
  bake_font.py  按语料烘中文字库（默认字号 10,14,18；本项目必须 --sizes 12,16,20）
  bake_spine.py 怪物 Spine 立绘光栅化
  to_t3x.sh     所有 PNG 页 → tex3ds .t3x（重烘后必须跑）
  preflight.py  真机前/打包前的资源与产物检查
  emu.py        本机模拟器驱动（gitignored；run/keys/shot/stop）
assets/         生成资源（gitignored）：cards*.png、monsters.png、ui.png、hud.png、
                relic_icons.png、act*.png、audio/*.pcm、font/
port/build.sh   3dsx 构建、romfs 打包、资源拷贝
docs/           01 数据管线、02 工具链、03 存档、04 音频、05 mod 转译器、06 真机测试
```

数据流：`STS_JAR` → `tools/extract_*.py`/`gen_*.py` → `assets/*.png` + `port/source/*_gen.h` → `tools/to_t3x.sh` → `port/build.sh` → `port/build/sts3ds.3dsx`。

---

## 5. 路线图（按优先级）

### P0 — 发 demo 前必须收掉

1. **第 3 幕内容**（当前最大缺口）
   - `battle.c:2257` 的 `setup_encounter()` 是 `act >= 2 → setup_act2()`，所以第 3 幕打的还是城市怪与城市 Boss。
   - 需要：第三幕普通怪池（Darkling、Orb Walker、Spiker、Repulsor、Spire Growth、Transient、Maw、Writhing Mass 等按原版池）、精英（Giant Head、Nemesis、Reptomancer）与三个 Boss（Awakened One、Time Eater、Donu & Deca）、第三幕房间配比与背景已就绪但遭遇未接。
   - 顺带修**通关结局**：第 3 幕 Boss 后 `run_advance_act()`（`run.c:315`）会把 act 加到 4，幕间屏显示「第 4 幕」，存档又只接受 act 1–3（`save.c:41`）→ 需要「胜利/通关」屏并停止推进。
2. **真机验证一次**：`docs/06-hardware-test.md` 的 preflight、启动自检、日志、帧率与内存；模拟器 60fps 不代表真机。
3. **发布物**：CIA 或至少 SMDH（标题/图标/描述）+ 说明文档；确定 demo 的进度范围（建议：第 1 幕完整 + 第 2 幕可玩）。
4. **视觉统一**（为视频观感，按性价比排序）
   - 已完成：标题官方 logo、战斗背景、卡面、怪物立绘、能量球/结束回合/营火按钮、事件面板与插画、商店地毯/售罄/删牌/标签、地图纸面。
   - 待做：奖励结算面板背景（`images/ui/rewardScreenSheet.png` 612×716 深蓝面板）、奖励列表行（`rewardListItemPanel.png` 464×98）、拿取全部按钮（`takeAll.png`）、商店底屏详情、升级/宝箱/幕间、药水图标（现在是彩色瓶子色块，正版有瓶身美术）、顶部栏（`topPanel` 73 张资源）。
   - 注意：`ui.png` 已排满，没有 612×716 的空位 → 要么新开一页 UI 图集，要么把奖励面板单独做一张 `reward.png` 进 romfs。

### P1 — 内容完整度

- **遗物 16 → 更多**：图标图集现在正好 4×4=16 格，扩容必须重烘 `relic_icons.png` 并改 `RELIC_SHEET` 布局；优先做通用型（战斗开始/回合开始/受击触发）。
- **药水 15 → 30+**：`battle.h` 的 `POTION_*` 只能追加 id（旧存档存的是 id）；新增要补 `POTION_NAMES/DESC`、图标颜色、效果与 `_POTION_WORDS` 字体语料。
- **事件 3 → 更多**：`run.c` 的 `STS_EVENT_COUNT` 与事件效果 switch 都按 id；每个事件要文本、立绘（`sts_ui_event_ill` 目前 4 张）、选项结算。
- **进阶（Ascension）**：目前固定 A0；敌人血量/伤害的 A 值散落在各 setup 里，建议先做 A1–A5 的全局倍率与精英词缀，再逐条补。
- **角色**：只有铁甲战士；第二角色需要卡池、起始遗物、能量球皮肤、卡面页扩容，工作量大，放长线。
- **卡牌边角**：133 张里 7 个 id 没有专属分支（纯伤害/诅咒，走通用数据），16 个 id 缺断言；补测试而不是重写。
- **触摸**：`handle_touch` 现在不出声，也没有拖拽滚动等手势；真机前补上。

### P2 — 产品化

- 设置页：音量、语言（目前只有简中）、按键说明、返回标题。
- 存档扩展：内容包/版本迁移（`docs/05` 的 save v2 设计已写）。
- 多语言：正版 jar 自带 eng/zhs 等文本，若做英文需要第二套字体与 `l10n` 切换。
- 性能：真机上的绘制批次、音频通道数、内存峰值；必要时降低粒子/背景层。

### P3 — 长线：mod 转译器

设计已定稿在 `docs/05-mod-transpiler.md`：PC 端打包器（class → IR + 抽资源 + 覆盖率报告）+ 3DS 端栈式 VM + 原生 API 垫片，不生成 C 源码。里程碑 S0 打包器 → S1 最小 VM + 10 个垫片让 Strike_Ninja/Defend_Ninja 在 CARD_TEST 跑通 → S2 角色入口 → S3 32 Power + 38 Action → S4 存档 v2。**等本体内容稳定后再动**。

---

## 6. 开工前检查（复制粘贴用）

```powershell
# 1) 状态
git status --short; git log --oneline -5

# 2) 构建（长构建必须后台 + 落盘日志，别让工具输出撑爆宿主）
& 'E:/msys64/usr/bin/bash.exe' -lc 'bash /e/vibe_lab/sts-for-3ds/port/build.sh > /e/vibe_lab/sts-for-3ds/.tmp/build_ship.log 2>&1'

# 3) 自检构建
& 'E:/msys64/usr/bin/bash.exe' -lc 'CARD_TEST=1 bash /e/vibe_lab/sts-for-3ds/port/build.sh > /e/vibe_lab/sts-for-3ds/.tmp/build_card.log 2>&1'
Select-String -Path .tmp\build_card.log -Pattern 'error:'

# 4) 模拟器验收
python tools\emu.py stop; python tools\emu.py run; Start-Sleep 10
python tools\emu.py shot --name check
```

- 自检报告屏应显示 **1197 cases 0 failed**；数字会随新增断言增长，改测试后记得同步本节与提交说明。
- 截图前把 `port/build/sts3ds_test.3dsx` 拷到 `$env:APPDATA/Azahar/sdmc/3ds/sts3ds.3dsx`（`port/build.sh` 不装 SD 卡），否则模拟器跑的还是旧二进制。
- 模拟器状态跨调用保留：换 demo 前先 `stop` 再 `run`，从开机态重来。

### demo 导航（CARD_TEST 报告屏）

- 开机进报告屏；按 **B** 播放 `s_boss_demo`（7–49 号，跳过 16/17）并前进一号。
- 战斗屏按 **SELECT** 回报告屏；报告屏的 **SELECT** 是「选牌 demo」入口，不是回退，导航时别按。
- 选牌演示（19/20）按 B 取消后落在空白战斗屏，需再按 SELECT 回报告屏。
- 第 k 次 B = 7+(k-1) 号；46–49 是城市四个强阵容。

---

## 7. 约定与坑（都是踩过的）

1. **一切以正版 jar 为据**：数值、AI、卡牌文本、怪物中文名都从 `desktop-1.0.jar`（反编译在 `.tmp/reference/src`）取；不猜、不用 wiki 数字。怪物名照 `localization/zhs/monsters.json`。
2. **编译通过 ≠ 验收**：每个可见改动都要模拟器截图确认；纯数据改动要有 `cardtest.c` 断言。截图放 `.tmp/shots/`，提交说明里点名文件名。
3. **重烘资源必须跑 `bash tools/to_t3x.sh`**：`build.sh` 只拷贝现成 `.t3x`，不重转 PNG。漏了这一步的表现是「新槽位/新字静默变空灰框」，而 `.t3x` 字节数可能一模一样，不能拿大小当证据。
4. **字体语料是白名单**：UI 里新写的中文如果不在 `tools/bake_font.py` 的语料里就会画成空白；药水新词记得加进 `_POTION_WORDS`。重烘必须 `--sizes 12,16,20`（脚本默认 10,14,18，构建会报 `sts_font_16 undeclared`）。
5. **图集页是 2 的幂**（PICA200）：加图先看页面有没有空位；`monsters.png` 是 1024²/8×96 格，`ui.png` 已排满。
6. **注释里不要写 TODO/FIXME**（仓库约定，`codex_guard` 会盯），未完成的事写进本文件或 `docs/`。
7. **提交风格**：`Area: sentence`，例如 `Enemies: give the city its monster pools and the Mugger`；正文写数值依据与验证方式；一次提交只做一件事；`docs/` 与测试跟代码同提交。
8. **PowerShell 不要 heredoc**：`git commit -F -` 会报「缺少文件规范」，先写 `.tmp/msg_*.txt` 再 `git commit -F`。
9. **长构建/长日志只在后台跑**：`run_in_background` + 落盘日志 + 只 `Select-String` 关键行；单次工具输出控制在几十 KB。历史上一次 7000 行并行读取直接把宿主 Node 撑到 OOM。
10. **别用 `subagent_fork` 做长上下文审计**：子会话拿到的是未折叠的完整上下文，会越读越爆；只读小任务可以，长审计留在主会话用定向 grep + 小段读。
11. **模拟器截图会被遮挡**：`tools/emu.py` 先试 `PrintWindow`，失败才前台 `ImageGrab`；用户窗口盖住时可能抓到别的程序，拍到奇怪画面先查前台窗口。
12. **第 1/2 幕的深度夹具**：`battle_test_spawn_act1(2/3/4)` 是固定遭遇（双中号酸液/单颚虫/单邪教徒），别把它们改回随机池，否则大量断言会飘。

---

## 8. 交接清单

- [ ] 读 README、本文件、`docs/01`（资源管线）与 `docs/02`（工具链），确认 devkitPro/msys2/STS_JAR 路径。
- [ ] 跑一次 CARD_TEST 构建并亲眼看到 1197 全过。
- [ ] 从 §5 挑一个 P0/P1 条目，先在 `cardtest.c` 或截图里定义「做完是什么样」。
- [ ] 改代码 → 后台构建 → 截图/断言 → 改 `docs/` 与测试 → 提交推送。
- [ ] 把本文件的「当前快照」与对应条目更新（测试数、提交号、已完成项）。
- [ ] 会话结束前压缩上下文；不要把大段日志/源码留在对话里。

---

## 9. 最近提交（便于回溯）

```
9a0d5a0 Title: draw the game's own Chinese logo
59f1318 Audio: give the out-of-combat screens their clicks
8c2c151 Potions: bring the port to fifteen bottles
48e270a Enemies: use the game's own Chinese monster names
c335ab8 Enemies: finish the city's strong line-ups
f6a354f Enemies: give the city its monster pools and the Mugger
c045bfb Docs: record the mod transpiler plan before it is built
ae4b2b9 Enemies: add the Lots of Slimes fight
a50cc10 Enemies: draw the act 1 encounters from the real pools
e543f1d HUD: put the enemy debuff icon by the end-turn button
```
