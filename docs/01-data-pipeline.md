# 数据管线：从 `desktop-1.0.jar` 到 `cards.json`

目标：**不手抄任何数值、不靠命名猜关系**——所有卡牌数据都从正版安装目录的 jar 里机械提取。跑完之后，任何一张卡的"名字/费用/数值/卡图/中文描述"都能追溯到 jar 里的具体字节。

## 为什么不能只读 `cards.json`

jar 里有两个 `cards.json`，都不是完整数据源：

- `localization/zhs/cards.json`：423 条，键是**卡的显示名**（无色卡用名字，红/绿/蓝卡用类名 ID，两种混在一起），值是 `{"NAME", "DESCRIPTION"}`。没有费用、没有数值、没有卡图路径。
- `cards/*.class`：每张卡一个 Java 类，构造函数里用 `ldc` + `putfield` 把 `cost / type / rarity / damage / block / magicNumber / imagePath / descriptions` 写进字段。

卡图路径（`imagePath`）**只存在于字节码里**，而卡图区域名又是 `颜色/类型/卡图文件名` 的三段式（例如 `red/attack/bash`）。所以「卡 ID ↔ 卡图」这个映射必须从 `.class` 里挖，命名约定靠不住（`A Thousand Cuts` 这种带空格的显示名和 `AThousandCuts` 这种类名对不上）。

## 模块

| 文件 | 职责 |
|---|---|
| `tools/sts_assets.py` | `Jar`（zip 读取、`names()`、`read()`、`image()`）、`Region`、`parse_atlas()`（libGDX `.atlas` 文本格式）、`tokenize_desc()`（把 `!D!`/`#y`/`NL`/`[R]` 切成 token 流）、`read_card_meta()`、`card_class_paths()` |
| `tools/javaclass.py` | 极简 `.class` 解析器：常量池、字段表、方法表。`ClassFile`、`CardMeta` |
| `tools/bytecode.py` | JVM 指令遍历（`iter_insns`）、操作数解码（`u1`/`u2`）、`push_int` 常量折叠 |
| `tools/extract.py` | 驱动：遍历 119 个卡类 → 读元数据 → 匹配图集区域 → 裁图重打包 → 写 `cards.json` / `cards.csv` / `cards.atlas` |
| `tools/preview.py` | 把 `cards.json` 渲染回 PNG，肉眼验收 |

`read_card_meta()` 的做法：找到 `<init>`，扫描指令，维护一个"操作数栈上的常量"小栈，遇到 `putfield <CardName>.<field>` 就把对应的值落到 `CardMeta` 上。够用且对 sts 的代码风格鲁棒，不需要完整栈帧模拟。

## 用法

```powershell
# 默认从 Steam 安装目录读 jar，输出到 ./assets
python tools/extract.py --out assets

# 只抽部分卡（调试用），或换语言
python tools/extract.py --out assets --groups red,colorless --lang zhs --limit 20

# 渲染 12 张卡的预览拼图
python tools/preview.py --assets assets --sheet 12
# 只渲染指定卡
python tools/preview.py --assets assets --ids Strike_R,Bash,Defend_R
```

`assets/` 与 `port/source/*_gen.h` 都已加入 `.gitignore`：它们完全由 jar 推导得出，和正版资源同源，不随仓库分发。干净克隆需要先按本页重新生成，否则 `port/build.sh` 会提示缺少生成头文件。

## 输出

```
assets/
  atlas/cards_000.png, cards_001.png   # 重打的新图集（0.5 倍缩放的卡图，2 张 1024²）
  atlas/cards.atlas                    # 新图集的区域表（libGDX 格式，可直接喂给解析器）
  data/cards.json                      # 每张卡一行，diff 友好
  data/cards.csv                       # 数值快查
  fonts/SourceHanSerifSC-Medium.otf    # 中文正文字体（23 MB，运行时不能直接塞进 ROM）
  fonts/04b03.ttf                      # 像素数字字体
  preview/cards_sheet.png
```

一条 `cards.json` 记录：

```json
{"id":"<CardId>","name":"<localized name>","cost":2,"type":"ATTACK","rarity":"BASIC",
 "group":"red","color":"red","target":"ENEMY",
 "damage":8,"block":null,"magic":2,
 "art_region":"<color>/<type>/<image>",
 "art":{"page":"cards_000.png","px":[0,0,0,0],"uv":[0,0,0,0]},
 "art_size":[0,0],
 "text":[["t","..."],["v","damage"],["t","..."]]}
```

`text` 的 token 类型：`t` 文本、`v` 数值槽（`damage`/`block`/`magic`）、`c` 颜色（`yellow`/`green`/`blue`/`red`/`purple`/`white`）、`n` 换行、`i` 图标（`R`/`G`/`B`/`P`/`E`）。运行时把这些 token 喂给排版器，数值槽在渲染时按当前状态（力量、易伤、升级）代入。

`cost` 的取值：`-1` = X 费，`-2` = 无法打出（诅咒/状态）。

## 已知缺口

> 2026-10-20 更新：下面第 1、3、4 条已经关掉，其余仍然成立。

1. ~~只抽了 4 组~~ → 现在是 5 个包（`red,colorless,curse,curses,status` = 133 张）。注意诅咒的包名是复数 `curses`，`--groups` 默认值里漏过它，一度只出 119 张。绿/蓝/紫三组改 `--groups` 即可，`bake_art` 只处理 `cards/` 下的图集页。
2. **排版**：源文本里有作者手工断行的痕迹（例如"神化"描述里的多余空格），并且中文标点禁则（标点不能出现在行首）还没做，所以预览图里偶尔有"。"单独成行。真机排版器要补这两条规则。
3. ~~字体没烘焙~~ → 已烘焙成 `assets/font/font_zhs.h` + `font_12/16/20.t3x`（字形图集 + 度量表，见 `tools/bake_font.py` 与 `tools/mockup.py`）。
4. ~~卡框美术没上~~ → 已用游戏的 `1024/cardui.atlas` 底图/插画窗/边框/名字条分层合成，图层顺序和边界见 `port/source/main.c` 的卡面绘制函数与 `tools/extract_visuals.py`。
5. 敌人立绘、遗物图标、三幕背景（见第 10 条）已抽成 t3x；事件文本与地图数据仍是从 JSON 读，规则本身还是手写的。
6. 战斗 HUD 资源走单独的 tools/extract_combat.py：格挡盾取 images/ui/combat/block.png，状态图标取 **powers/powers.atlas**（不是 images/powers/，那目录在 jar 里是空的）的 48px 区域，按区域的 orig/offset 还原回 48×48 画布（否则图标在画布里的偏心会丢，数量文字就对不准）；目前十二枚：strength/dexterity/vulnerable/weak/frail/armor（MetallicizePower.loadRegion("armor")）/flight/platedarmor/artifact/barricade/hex/intangible，intangible 单独放在第三行第 1 格（64,144）。玩家图用 bake_spine.py 烘 **images/characters/ironclad/idle** 的 **Idle 动画第 0 帧**（setup pose 是深蹲，不能直接用），裁 alpha 后拟合到 PLAYER_BOX 96×136，实际烘成 96×68。产物是 assets/combat.png（512²）+ port/source/combat_gen.h，再由 tools/to_t3x.sh 转成 combat.t3x（当前 17029 字节）。
7. 玩家角色本体是 Spine 动画（images/characters/ironclad/idle/），jar 里没有静态整身立绘。现在烘的是 **Idle 动画 t=0** 的持剑站姿，比 shoulder.png 金盔特写完整；仍是静止帧，接 Spine 运行时之前这一格不算完全还原。main.c 把这张 96×68 的图按底部对齐放进 68×64 的血框区域，别再用旧的 96×120 setup 图。
8. UI 面板资源走单独的 tools/extract_ui.py：节点图标取 images/ui/map/`<kind>.png` 与 `<kind>Outline.png` 的 128px 区域，选择环与连线取 circle5.png / dot1.png。citro2d 不能给子纹理染色，所以"可用 navy / 未走 gray"两套颜色在抽取时就烘成两份贴图。产物是 assets/ui.png（1024²）+ port/source/ui_gen.h，再由 tools/to_t3x.sh 转成 ui.t3x；**port/build.sh 的 romfs 拷贝列表要显式加 ui.t3x**（漏掉会静默退回占位画面）。
    地图纸面单独走 tools/extract_map_paper.py：读 images/ui/map/mapTop.png、mapMid.png、mapBot.png（各 1920×1080），按三张 alpha bbox 的共同左缘 239 与纸面右缘 1601 取水平 crop，再向右补到对称的 1682（1443 宽，纸面两侧各 18px 场景），各自按自身 y bbox 裁切后等比缩到 320 宽，得到 top 320×224 / mid 320×240 / bot 320×225，拼成一张 320×689 的纸。产物 assets/map_paper.png + port/source/map_paper_gen.h（含 PAGE_W/H、CROP_W、PAPER_LEFT/W 和三段矩形），再由 tools/to_t3x.sh 转成 map_paper.t3x；**build.sh 也必须显式拷 map_paper.t3x**。三段在游戏里 1:1 叠放（y=0/224/464），不再把一张 mapMid 纵向拉伸。
   同一个脚本还抽事件屏：886 纸面板（`GenericEventDialog` 的 1763×806 画布）、610 插图框、`LargeDialogOptionButton` 的 890×77 选项条（**烘两份**：原色 + boxColor 0.4 灰，同样因为不能染色），以及三张事件插图（`images/events/fishing.jpg`、`cleric.jpg`、`goopPuddle.jpg`，顺序按 `l10n_gen.h` 的 `sts_events[]` 事件名映射，别按数组下标硬编码）。图集从 y=790 起，不覆盖地图那批 offset。新增 UI 层后 `assets/ui.png` → `ui.t3x` → `port/source/ui_gen.h` 三者必须一起重生成，否则静默用旧图。
   商店屏也在同一个脚本里：地毯 `images/npcs/rug/zhs.png` 按 400×240 重采样（原版直接拉伸铺满 1920×1080），删牌位 / 卖光位取 `images/npcs/purge/zhs.png` 与 `sold_out/zhs.png`（**按 alpha bbox 裁切后 2 倍**烘，下屏详情按原尺寸画），半价签取 `images/npcs/sale_tag/zhs.png`，金币取 `images/ui/topPanel/gold.png`（同样裁 alpha bbox）。这五张放在图集右侧 x≥404、y≤250 的空地，不碰地图与事件那两批 offset。注意 `C2D_DrawImageAt` 的 y 是**上边缘**，而游戏里 `GOLD_IMG_OFFSET` 给的是左下角，换算时别忘减图高（商店价格一整排下移 11px 就是这么来的）。
   遗物图标由 `tools/extract_relics.py` 单独出（`relic_icons.png` = 4×4 页），烘焙尺寸 64px/枚：游戏原图是 128，但 64 已经够下屏 72px 详情与 16px 顶栏用，省 3/4 显存；`port/source/main.c` 的 `RELIC_ICON`/`RELIC_SHEET` 必须跟着改，否则子纹理 UV 会错位。
9. 敌人 Spine 立绘由 `tools/extract_monsters.py` + `tools/bake_spine.py` 烘成 8 列×96px 的 `assets/monsters.png`（1024×1024，8×8 格中已用 37 格，`monsters.t3x` 146433 字节）。页面从 1024×512 扩到 1024×1024 之后，`main.c` 算子纹理 UV 的纵向分母必须跟着从 512 改成 1024，否则整页立绘会纵向错位。槽位顺序：0–15 第 1 幕与被拣选者/扎人的书等，16–18 奴隶贩子三人组，19 铜制机械人偶，20 收藏家，21 火炬头，22 地精首领，23 火大地精，24 卑鄙地精，25 胖地精，26 持盾地精，27 地精法师，28 铜球（来自 `images/monsters/theCity/automaton/orb.png` 整图），29 虱虫（取 `images/monsters/theBottom/louseGreen` 的 Spine 整身；`tools/extract_monsters.py` 的 `SPINE_EXTRA` 只额外烘这一只用到的配色，红色虱虫仍借用同一格并按名字区分），30 小酸液史莱姆（`theBottom/slimeS`）、31 小尖刺史莱姆（`theBottom/slimeAltS`）、32 抢劫的（取 `images/monsters/theBottom/looter` 的 Spine 整身）、33 打劫的（取 `images/monsters/theCity/looterAlt` 的 Spine 整身，Slavers 与 Mugger 的立绘同形不同色）、34 哨卫（`theBottom/sentry`）、35 蛇花（`theCity/snakePlant`）、36 异蛇（`theCity/reptile`）；百夫长与神秘术士复用槽位 15「坦克」与 14「牧师」的立绘，不占新格。怪物名一律照 `localization/zhs/monsters.json` 的 `NAME`：抢劫的（Looter）、被拣选者（Chosen）、带壳寄生怪（Shelled Parasite）、圆球守护者（Spheric Guardian）、真菌兽（Fungi Beast）、大颚虫（Jaw Worm）、扎人的书（Book of Stabbing）、第一勇士（Champ）都是按官方中文改过的；小/中史莱姆仍共用「酸液史莱姆」「尖刺史莱姆」，靠 `Enemy.art` 区分立绘。小史莱姆与中史莱姆的本地化名相同，因此这两格只能经 `Enemy.art` 引用，HUD 查槽位时用 art 而不是 name。收藏家的火炬头（`draw_x=160/216`、`draw_w=48`）与地精首领的三个小怪（`draw_x=120/188/256`、`draw_w=54`）都是战斗中动态生成的小怪，所以 UI 必须支持 4 个敌人槽，不能按普通遭遇的 3 敌上限裁剪。`bake_spine.py` 现在按 mesh 顶点光栅化：无权重顶点直接乘槽位骨骼的世界矩阵，有权重顶点按 `(boneIndex,x,y,weight)` 混合；三角绕序有正负，重心坐标要乘 `sign(den)`，否则一半三角形会被丢掉（表现正是“碎片”）。mesh 的 UV 是相对 attachment 的 atlas region 而不是整页；atlas 的 `rotate: true` 区域存储矩形是 `(h,w)`，采样时 rotate 用 `sx=V*tw, sy=(1-U)*th`、非 rotate 用 `sx=U*tw, sy=V*th`（libGDX 的 mesh UV 原点在左上，写成 `1-V` 会把整条手臂/腿采成碎片）；region 附件则要先裁 `(rx,ry,rx+rh,ry+rw)` 再 `rotate(-90, expand=True)` 转回。骨骼世界矩阵先用本地 x/y/rotation/scale 逐级相乘得到 rest pose；如果存在 `STS_JJS`，或 jar 旁的 `jre/bin/jjs.exe`，再调用 `tools/spine_bones.js` 让游戏自带的 Spine 运行时求一次 setup pose（含 IK/path/transform 约束），结果按 skeleton 的 SHA1 缓存在 `.tmp/spine_poses`（可用 `STS_SPINE_CACHE` 改目录）。这修好了地精首领的金角、奴隶贩子的法杖等大量 rest pose 错位；没有 jjs 时自动回退 rest pose。jjs 对带点号的 `-D` 属性支持不好，脚本用 `-Dspine_skeleton=...` / `-Dspine_out=...`，并在脚本内部做矩阵转置。区域的锚点按骨架区分：tools/extract_monsters.py 的 CENTER_ANCHOR 把火大地精与胖地精改成按 region 中心对齐（这两只的附件就是按中心原点摆的，腿与牌子因此贴合身体），其余骨架仍走底边中心；邪教徒的长袍在中心锚点下会把脸埋进领口，所以不能整页统一切换。已知残留：地精法师的法杖、持盾地精的造型、卑鄙地精的闪电在 setup pose 下仍偏，换 idle 帧也不改善，属于游戏里靠动画与遮挡才成立的摆位，不是槽位串图。页面缓存必须用 **完整路径**（`atlas_dir + "/" + page`）做键，因为所有怪物共用一个 `pages` 字典，同名页（如各目录的 `*.png`）会互相串图。`port/source/main.c` 的 `MON_KEYS[37]` 现已全部启用；**重烘 `assets/monsters.png` 之后必须跟着跑 `tools/to_t3x.sh`（或单独 `tex3ds -f rgba4444 -o assets/t3x/monsters.t3x assets/monsters.png`）**——`port/build.sh` 只负责把 `assets/t3x/monsters.t3x` 拷进 romfs，不会自己从 png 转页；漏掉这一步新槽位会静默画成空灰框，而且 t3x 的字节数可能完全不变（页尺寸没改，`115469` 那类数字不能当作是否重跑过的证据）。蓝/红奴隶贩子的本地化名相同，立绘槽由 `Enemy.art` 决定。
10. 三幕战斗背景走 `tools/extract_visuals.py`：不再只读各场景根目录的 `scene.png`（那只是 mod 层的页），而是按 `scene.atlas` 的层列表逐层裁切、alpha 合成到 1920×1136 的黑底画布。第 1 幕取 13 层（`bg`、`mod/mg`、`mod/midWall`、`mod/mod1`、`mod/mod2`、`mod/ceiling`、`mod/ceilingMod1..6`、`mod/fg`），第 2 幕取 17 层（`mod/bg1`、`mod/bg2`、`mod/bg1Glow`、`mod/bg2Glow`、`mod/floor`、`mod/ceiling`、`mod/wall`、`mod/chains`、`mod/mg1`、`mod/mg2`、`p1..p5`、`mod/throne`、`mod/fg`），第 3 幕取 18 层（`mod/floor`、`mod/ceiling`、`mod/bg1`、`mod/bg2`、`mod/mod1..4`、`mod/c1..c4`、`mod/s1..s5`、`mod/fg`）；第 2/3 幕的 `mod/floor` 就是原版地板。`tools/sts_assets.py` 的 atlas 页判断现在接受 `.png/.jpg/.jpeg`，否则第 1 幕 `scene.jpg` 里的 `bg` 拿不到页。合成时 PIL 的 top 用 `canvas_h - offset_y - region_h`（libGDX 的 offset 是左下角），再按原逻辑等比缩到 400 宽、center-crop 400×240，输出 `assets/act1.png` / `act2.png` / `act3.png`，由 `tools/to_t3x.sh` 转成 `act1.t3x`(40161) / `act2.t3x`(48413) / `act3.t3x`(59613)；`port/build.sh` 必须显式拷这三张，漏掉会退回无地板的旧页。
11. 战斗 HUD 的原创贴图走 `tools/extract_hud.py`：能量球取 `images/ui/topPanel/red/layer1..6.png` 与空球 `layer1d..5d.png`（各 128²），结束回合取 `images/ui/topPanel/endTurnButton.png`、`endTurnButtonGlow.png`、`endTurnHover.png`（各 256²），篝火取 `images/ui/campfire/sleep.png`、`smith.png`、`outline.png`、`buttonShadow.png`（各 256²），能量变化闪光 `energyRedVFX.png` 也一并入图集但本批还没接触发逻辑。产物是 1024² 的 `assets/hud.png` + `port/source/hud_gen.h`（`StsHudRect`、`STS_HUD_END_*`、`STS_HUD_CAMP_*`），`tools/to_t3x.sh` 再出 `hud.t3x`（193009 字节）；`port/build.sh` 必须把 `hud_gen.h` 列入必需头并把 `hud.t3x` 拷进 romfs，缺资源时 `main.c` 会退回旧手绘 HUD。图集布局：亮层 `x=i*128,y=0` 六枚，暗层 `x=i*128,y=128` 五枚，结束回合三张 `x=i*256,y=256`，VFX `(768,256)`，篝火四张 `x=i*256,y=512`。运行时按 `EnergyOrbRed` 的层序堆叠，layer6 金属外环固定，layer1..5 按 360/40/-40/20/-20 °/s 旋转（空能量改用暗层 72/8/-8/5/-5 °/s）；球体画在 `(28,30)`、54px，`%d/%d` 用 FS_MID 加 1px 深色描边。结束回合按钮 88px、中心 `(270,26)`，按下时先画 hover 再画 base 并下移 2px；篝火两枚 96px，焦点从 0.9 缓动到 1.0，金色 `outline` 按 1.075 画在按钮下方只露边，标签用原作中文“休息 / 锻造”。原版 `EnergyPanel` 的字体缩放弹跳（2.0→1.0）和 VFX 闪烁留待后续。标题屏的官方中文 logo 取 `images/ui/title_logo/zhs.png`（900×320），等比缩到 `TITLE_BOX=(0,768,720,256)` 放进图集最后一行（原本空着），运行时以 320px 宽画在 `(40,22)`，`hud_gen.h` 同时导出 `sts_hud_title_logo`；zhs logo 上的字是官方译名「尖塔奇兵」，标题屏底屏文字也照此写。
12. 卡牌升级数据来自 `tools/extract_upgrades.py`：它把 `upgradeBaseCost(I)` 这类“设为绝对值”的 helper 直接写进 `deltas["cost"]`，所以降费升级（Body Slam 1→0、Barricade 3→2 等）不会因为增量 0 被丢掉。`tools/gen_cards.py` 的 `CardDef` 新增 `cost_up`，并保留升级后的 `text_up`；`TEXT_UP_OVERRIDES` 负责那些中文升级文本与基础文本相同、或需要改写措辞的卡（例如 Ghostly 去掉“虚无”、Forethought+ 改为“任意张”、Perfected Strike 的“打击”计数），其余卡用 `render_text()` 按 `damage/block/magic` 的 delta 替换。`CARD_ENTRY` 的升级位改成 8 位以上的 **level**（`CARD_ENTRY_LEVEL`/`CARD_LEVEL`），Searing Blow 因此可以连续升级：伤害 `12 + 4*level + level*(level-1)/2`（12/16/21/27…），`card_can_upgrade` 对它会一直返回 true。`is_innate_entry()` 让 Mind Blast/Dramatic Entrance/Pride/Writhe 恒固有、Brutality 仅升级后固有，`battle_start` 在洗牌后用 `apply_innate_cards()` 把它们移到抽牌堆顶；`is_ethereal_entry()` 让 Clumsy/Carnage/Ghostly Armor/Dazed/Void/AscendersBane 恒虚无、Apparition（id `Ghostly`）仅未升级时虚无，回合末用 entry 判定而不是按卡名硬编码。Perfected Strike 现在统计手牌+抽牌堆+弃牌堆中所有名字含“打击”的牌并乘 `magic`（2/3），Burn+ 用 entry 的 magic（4 点）。Forethought+ 的多选顺序按原版 `addToBottom`：最后选中的牌最深（draw[0]），第一张选中的最先抽到；`battle_pick_confirm_multi` 用 `chosen[chosen_count - 1 - i]` 写回。自检在 batch18 覆盖这些路径，当前 `CARD TEST 775 cases 0 failed`。卡名上的等级标记也按 level 画：`draw_card_at()` 多带一个 `level` 参数，`level > 1` 出「卡名+N」（升级屏右侧预览就是 `level + 1`），`level > 0` 出「卡名+」，0 出原名；手牌与牌库分别取 `battle_hand_level()`/`battle_deck_level()`，商店、奖励、出牌特效与选牌屏传 0（这些位置的牌不会是 +N 卡）。`draw_pick_top()` 目前只拿到卡号与升级位、拿不到 level，所以 True Grit+ 这类从手牌里选的屏遇到 Searing Blow 会只显示「+」，属于已知缺口。
