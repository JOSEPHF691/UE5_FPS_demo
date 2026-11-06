# UE5 FPS Demo

基于 **Unreal Engine 5.7 / C++** 的多人回合制战术 FPS 原型。项目围绕最多 **3v3** 的小队淘汰对抗，串联选队准备、回合经济、武器与护甲、宝箱争夺、观战和断线恢复。

核心对局规则由服务器执行，蓝图与资源用于内容配置和表现。联机采用玩家托管的 **Listen Server**，接入 **Steam Lobby / Session**；本地编辑器测试使用 **OSS Null**。

**技术栈：** C++ · Enhanced Input · UMG · Actor Replication / RPC · OnlineSubsystemSteam · SteamSockets · AIController / NavigationSystem · Git LFS

## 功能概览

- **回合对抗**：两队各最多 3 人，支持以 1v1 / 2v2 进行开发测试；射击红蓝标靶完成选队与准备，全部候选玩家准备后开始对局。
- **武器与经济**：手枪、步枪、狙击枪，弹匣与备弹、换弹、ADS、爆头、轻重护甲和耐久；购买由服务器检查阶段、资格与余额，装备失败时退款。
- **装备流转**：生还者保留武器等级与护甲耐久；死亡角色可掉落非默认武器，供场上玩家拾取。
- **宝箱与守卫**：固定中央宝箱、两名守卫及限定战斗区域；击败守卫后持续交互领取，为队伍提供经济奖励，并为领取者恢复一定生命。
- **狩猎情报**：回合末期周期投递带误差的敌方位置情报，按队伍授权分发，随剩余时间缩短提高频率。
- **战斗界面与观战**：生命、弹药、护甲、命中和受击反馈，购买界面、队伍名册、队友观战及重连状态界面。
- **断线恢复**：服务器参赛记录独立于连接生命周期；非主机客户端断线时保留当前回合的存活角色，在同一主机和世界仍存活时尝试恢复控制。
- **Steam 会话**：原生创建、查找、加入、邀请、离开房间流程，以及会话状态和失败清理逻辑。

## 对局规则

```text
选队与准备 → 购买阶段 → 战斗阶段 → 回合结算 → 下一回合 / 比赛结算
```

| 阶段 | 默认时间 | 主要行为 |
| --- | --- | --- |
| `PreRound` | 等待全部候选玩家准备 | 移动、射击标靶选队；玩家伤害关闭 |
| `Buy` | 10 秒 | 购买装备；移动、射击与玩家伤害关闭 |
| `Combat` | 105 秒 | 正常战斗、宝箱争夺与狩猎情报 |
| `RoundEnd` | 6 秒 | 回合结果、经济结算与下一回合准备 |
| `MatchEnd` | 15 秒 | 比赛结果与重启等待 |

- **歼灭敌方全队**是唯一的回合获胜条件；宝箱影响资源获取。
- **先赢 5 回合**结束比赛，一场比赛最多进行 **9 回合**。
- 战斗时间结束时双方仍有人存活，判为回合平局；同时歼灭通过服务器判定窗口处理。
- 至少需要两名玩家，红蓝两队均有人，并且全部候选玩家完成选队与准备，才会开始比赛。

## C++ 架构

| 模块 | 主要职责 | 源码入口 |
| --- | --- | --- |
| `ATacticalGameMode` | 服务器权威的准备门槛、阶段切换、胜负判定、经济结算和准入 | [TacticalGameMode.cpp](Source/Tcamp_FPS/Variant_Shooter/TacticalGameMode.cpp) |
| `ATacticalGameState` | 同步回合、比分、阶段、服务器截止时间和公共事件状态 | [TacticalGameState.cpp](Source/Tcamp_FPS/Variant_Shooter/TacticalGameState.cpp) |
| `ATacticalPlayerState` | 队伍、准备和连接状态；拥有者可见的金钱与保存装备 | [TacticalPlayerState.cpp](Source/Tcamp_FPS/Variant_Shooter/TacticalPlayerState.cpp) |
| `ATacticalCharacter` / 武器组件 | 伤害、护甲、装备、换弹、ADS、死亡掉落和第一人称表现 | [TacticalCharacter.cpp](Source/Tcamp_FPS/Variant_Shooter/TacticalCharacter.cpp) · [Components](Source/Tcamp_FPS/Components/) |
| `UTacticalMatchRosterSubsystem` | 稳定参赛记录、队伍名额、重连准入和保留角色的恢复 | [TacticalMatchRosterSubsystem.h](Source/Tcamp_FPS/Variant_Shooter/Network/TacticalMatchRosterSubsystem.h) |
| `UTacticalSessionSubsystem` | Steam / Null 会话、房间状态、邀请、旅行和清理 | [TacticalSessionSubsystem.cpp](Source/Tcamp_FPS/Online/TacticalSessionSubsystem.cpp) |
| `UTacticalGameInstance` / `ATacticalPlayerController` | 网络故障恢复、菜单、输入、HUD、购买请求和观战 | [TacticalGameInstance.cpp](Source/Tcamp_FPS/TacticalGameInstance.cpp) · [TacticalPlayerController.cpp](Source/Tcamp_FPS/Variant_Shooter/TacticalPlayerController.cpp) |
| 宝箱与 AI | 守卫生命周期、区域约束、领取交互与奖励 | [Treasure](Source/Tcamp_FPS/Variant_Shooter/Treasure/) · [AI](Source/Tcamp_FPS/Variant_Shooter/AI/) |

网络同步按职责拆分：阶段和比分等公共状态由 `GameState` 发布，金钱和武器弹药等信息按拥有者同步，观战信息通过服务器校验的队友快照提供。客户端提交操作请求，服务器决定交易、伤害与对局结果。

## 获取与运行

### 环境

- **Unreal Engine 5.7**。
- 匹配 UE 5.7 的 **Windows C++ 工具链、MSVC 和 Windows SDK**；组件需求可参考 [.vsconfig](.vsconfig)。
- **Git 与 Git LFS**。项目包含数 GB 的 UE 资产，首次获取需要相应的下载时间与磁盘空间。

### 克隆项目

```powershell
git lfs install
git clone https://github.com/JOSEPHF691/UE5_FPS_demo.git
cd UE5_FPS_demo
git lfs pull
```

工程入口为仓库根目录的 **`Tcamp_FPS.uproject`**。`.uasset`、`.umap` 和 `.png` 使用 Git LFS；资源下载完成后再打开工程。

### 编译与打开

1. 右键 `Tcamp_FPS.uproject`，生成 Visual Studio 工程文件。
2. 打开生成的解决方案，选择 **Development Editor / Win64**，编译编辑器目标 `Tcamp_FPSEditor`。
3. 打开 `Tcamp_FPS.uproject`，等待首次资源与着色器处理完成。

<details>
<summary>命令行构建与启动（PowerShell）</summary>

在仓库根目录执行，先将引擎路径改为本机 UE 5.7 的安装位置：

```powershell
$UnrealRoot = "D:\Epic Games\UE_5.7"
$ProjectFile = (Resolve-Path ".\Tcamp_FPS.uproject").Path

& "$UnrealRoot\Engine\Build\BatchFiles\Build.bat" Tcamp_FPSEditor Win64 Development "$ProjectFile" -WaitMutex
& "$UnrealRoot\Engine\Binaries\Win64\UnrealEditor.exe" "$ProjectFile"
```

</details>

### 本地双人测试

1. 在编辑器打开 `/Game/FirstPerson/Lvl_FirstPerson`。
2. 在多人 PIE 设置中配置：
   - `Number of Players = 2`
   - `Net Mode = Play As Listen Server`
   - `Run Under One Process = true`
   - `Run Dedicated Server = false`
3. 启动两个窗口，分别射击场景中的 **`JOIN RED`** 和 **`JOIN BLUE`** 标靶，完成选队与准备。
4. 满足准备条件后进入购买阶段，倒计时结束进入战斗。

编辑器启动地图为 `Lvl_FirstPerson`。独立游戏的默认启动地图为 `Entry`，由 `TacticalMenuGameMode` 显示多人菜单；创建房间后加载战斗地图。

## 默认操作

| 输入 | 功能 |
| --- | --- |
| `W / A / S / D` 或方向键 | 移动 |
| 鼠标 | 调整视角 |
| `Space` | 跳跃 |
| 鼠标左键 | 射击 |
| 按住鼠标右键 | 瞄准 / ADS |
| 鼠标滚轮 | 切换武器 |
| `1` / `2` | 主武器 / 副武器 |
| `R` | 换弹 |
| 按住 `E` | 持续交互；宝箱满足领取条件后持续 2 秒领取 |
| 按住 `Tab` | 查看队伍名册与比分 |
| `Esc` | 联机菜单 |

输入配置见 [基础输入](Content/Input/) 和 [武器输入](Content/Variant_Shooter/Input/) 中的 Enhanced Input 资产。

## 联机与当前验证边界

- **编辑器 / PIE**：使用 OSS Null 与 IP 网络驱动进行本地玩法诊断。
- **打包游戏**：采用 Steam Lobby / Session 和 SteamSockets，由一名玩家托管 Listen Server。跨机器测试需要两台电脑、两个不同的 Steam 账号。
- 当前配置的 **Steam App ID `480` 用于开发测试**。正式分发前需要项目自己的 App ID，以及相应的 `UE_PROJECT_STEAMSHIPPINGID` 配置。
- Steam 房间与恢复流程已有 C++ 实现；跨机器、跨网络的打包 Steam 验收仍有待完成。主机退出或丢失时比赛结束，当前没有主机迁移。
- 多窗口 ADS / 受击表现、实际断线重连、完整 2v2 / 3v3 场景、守卫 AI 和六人帧时间仍有手动验收事项。

## 自动化测试

测试源码位于 [Source/Tcamp_FPS/Private/Tests](Source/Tcamp_FPS/Private/Tests/)，覆盖回合规则、经济结算、重连准入与参赛记录、观战信息、UI、AI 区域和武器表现配置等。

编译编辑器目标后，在编辑器控制台执行：

```text
Automation RunTests Tcamp.Tactical
```

规则和配置测试用于检查确定性逻辑；真实 RPC、Steam 会话、渲染效果和多人性能还需要对应的游戏运行验收。

## 目录结构

```text
Config/                         项目、输入与网络配置
Content/                        关卡、蓝图、武器、角色和 UI 资产
Source/Tcamp_FPS/
  Components/                   武器库存等组件
  Online/                       会话与在线接入
  Variant_Shooter/               回合玩法、战斗、AI、经济与界面
  Private/Tests/                自动化测试
SourceArt/ShopIcons/             商店图标原图
scripts/                        素材处理与编辑器辅助脚本
ProblemAndSolution/             当前实现快照与玩法设计文档
docs/                           版本管理说明
Tcamp_FPS.uproject              Unreal 工程入口
```

项目使用 UE 第一人称模板、Manny、InfimaGames Realistic Assault Rifle、Paragon Murdock 等资源；第三方素材的使用条件以对应资源的原始说明为准。

## 进一步阅读

- [当前实现与待验收事项](ProblemAndSolution/Tactical_FPS_Current_Setup.md)
- [玩法规则、职责划分与执行计划](ProblemAndSolution/Tactical_FPS_Gameplay_Design_And_Plan.md)
- [Git / LFS 与工程恢复说明](docs/version-control.md)
