# 新一代现代化引擎改造与代码精简独立重构方案 (Phase 78–85 演进蓝图)

> **基线版本说明**：本方案完全独立于既往阶段文档，直接以最新导入的工程代码库（`smithw2055-rgb/starcraft`，当前共 **207 个源码文件：53 个 `.cpp` + 154 个 `.h`**）及云端 [StarCraft](https://drive.google.com/drive/folders/1-6rATljWabXQ3Rnhm2x4h1ZBeon6aOQJ) / [assets-R](https://drive.google.com/drive/folders/1AwtMYu7okUhitYqKexHoVYTOBcC1dInK) 资源库（主清单 [Master_Refactor_Manifest.json](https://drive.google.com/file/d/1Q9-Wy8d3E6drxAkCsS7WdULBAEPRh11-/view?usp=drivesdk)、完整资源归档 [StarCraft_Assets_R_Complete_Package.zip](https://drive.google.com/file/d/1c7MNfNGfRZitK2xJp1BBwn6QihHpN34K/view?usp=drivesdk)）为唯一起点，聚焦**下一阶段（Phase 78–85）**的深度去冗余、双轨旧代码退役与通用引擎底座闭环。

## 一、 最新工程代码基线体检与四大核心痛点诊断

通过对当前代码库全部 207 个源码文件的静态依赖扫描与行数审计，当前工程在“精简化”与“通用引擎+游戏项目解耦”两个维度上存在以下 **4 个最突出的结构性痛点**：

| 维度 | 当前代码库实测现状 | 核心冗余或架构痛点分析 |
| :--- | :--- | :--- |
| **1. `src/Engine/` 对外层模块的反向头文件耦合尚未清零** | `src/Engine/` 下 31 个头文件中，仍有 **15 处 `#include`** 跨出 `Engine/` 与 `Core/`，直接包含了 `src/Render/`（`RenderServer2D.h`、`RenderQueue2D.h`、`SceneNode2D.h`）与 `src/Resource/`（`ModernAssetRegistry.h`、`AssetServer.h`、`IArchiveReader.h`）。 | 导致通用引擎静态库 `EngineFoundationObj` 仍被迫编译游戏资源层的 `src/Resource/IArchiveReader.cpp`（898 行）、`src/Resource/Palette.cpp`（159 行）与 `src/Resource/ModernAssetRegistry.cpp`（883 行），尚未实现真正的**纯 Header-Only / 独立微内核引擎库**。 |
| **2. 仍残留 30+ 个微型/孤立头文件与碎片 `.cpp` 编译单元** | 全工程仍有 **22 个不足 120 行的微型 `.h` 文件**（如 16 行的 `ProtossPower.h`、20 行的纯别名头 `BehaviorTreeRuntime.h`、24 行的 `SokolGpuInterop.h`、32 行且在 `src/` 中零引用的 `EcsSync.h`、111 行且在 `src/` 中零引用的 `CursorTargeting.h`）以及 **4 个不足 200 行的碎片 `.cpp` 文件**（如 65 行的 `Logger.cpp`、159 行的 `Palette.cpp`、160 行的 `SystemManager.cpp`、198 行的 `SokolPBRDispatcher.cpp`）。 | 造成 `CMakeLists.txt` 中每个测试目标仍需手动拼接 `Palette.cpp`、`IArchiveReader.cpp` 等零散编译单元；同时 `src/Main/GameLoop/` 下残留多个无运行时调用方或仅单点调用的过渡头文件。 |
| **3. `src/Game/` 与 `src/Engine/` 存在严重的“双轨制”重复实现** | `src/Game/` 仍高达 **72 个文件（约 38,500 行代码）**：<br>• 寻路层：`NavigationGrid.h`（1,010 行）+ `NavigationService.h`（1,518 行）+ `AStar.cpp`（530 行）与 `Engine::Navigation::NavigationServer2D`（843 行）并存。<br>• 网络/录像层：`LockstepSync.h`（750 行）+ `SyncTransport.h`（342 行）+ `Replay.h`（501 行）与 `Engine::Net::DeterministicSessionServer` 并存。 | 游戏层仍保留大量早期的独立网格寻路、帧同步队列与二进制录像逻辑，未完全收敛为对 `src/Engine/` 对应 Server 的薄委托层（Thin Adapter），存在双份状态维护开销。 |
| **4. 核心 RTS 玩法规则仍硬编码在超大巨石 `.cpp` 中** | `src/Game/AI/AIBrain.cpp`（**6,353 行**）、`AnimationSystem.cpp`（**3,422 行**）、`MovementSystem.cpp`（**3,291 行**）、`BuildingSystem.cpp`（**3,058 行**）、`HarvestSystem.cpp`（**2,781 行**）、`CombatSystem.cpp`（**2,772 行**）、`UI/HudCommandPanel.cpp`（**2,514 行**）。 | 建筑放置网格校验、科技/升级树 DAG、技能冷却与 Buff/Debuff 状态机、载具驻军容器、命令面板声明式绑定尚未抽象到 `src/Engine/`，导致这些通用 RTS 机制无法被其他实验游戏项目复用。 |

## 二、 下一阶段独立重构方案总览 (Phase 78–85 八大里程碑)

围绕 **Godot 4 Server 微内核架构** 与 **Unity DOTS 数据驱动设计**，制定以下 **Phase 78–85 独立演进路线图**：

| 阶段 | 里程碑名称 | 核心目标与对标设计 | 预计精简/收敛规模 |
| :--- | :--- | :--- | :--- |
| **Phase 78** | **引擎层 100% 零外层依赖闭环 (`Engine` 彻底脱离 `Render/` 与 `Resource/`)** | 将 `RenderServer2D`、`RenderQueue2D`、`SceneNode2D`、`AssetServer` 下沉收编入 `src/Engine/Render/` 与 `src/Engine/Asset/`；将 `Logger.cpp`（65 行）、`Palette.cpp`（159 行）、`SystemManager.cpp`（160 行）内联为 Header-Only，使 `src/Engine/` 达到 **0 外部 `#include`**。 | 消除 15 处跨层 `#include`，退役 6 个过渡文件 |
| **Phase 79** | **24 个微型碎片头文件与零引用过渡文件深度清理** | 清理 `src/Game/`、`src/Main/GameLoop/`、`src/Client/`、`src/Render/`、`src/Resource/`、`src/UI/` 下 24 个微型或仅被测试引用的过渡文件，将 `src/Main/GameLoop/` 7 个零散头收敛为单一 `GameLoopRuntime.h`。 | 净减少 **24 个源码文件**（总文件数从 207 降至 ~183） |
| **Phase 80** | **双轨寻路与双轨网络/录像系统合一 (`Game` -> `Engine` 薄适配化)** | 退役 `NavigationGrid.h`（1,010 行）与 `NavigationService.h`（1,518 行）中的重复网格/流场存储，直接桥接至 `Engine::Navigation::NavigationServer2D`；将 `LockstepSync.h`（750 行）、`SyncTransport.h`（342 行）、`Replay.h`（501 行）收敛为 `Engine::Net` 的薄包装。 | 削减约 **2,200 行重复寻路与网络代码** |
| **Phase 81** | **抽象通用网格建造与电力/菌毯领域场服务 (`BuildingPlacementServer2D`)** | 参考 Godot `TileMap` 占位层与 Unity Grid Placement，在 `src/Engine/Gameplay/BuildingPlacementServer2D.h` 中抽象多尺寸建筑占地校验、矿区距离约束、附属建筑挂点（Add-on Socket）与半径能量场/菌毯扩散场（Pylon/Creep Aura Field），瘦身 `BuildingSystem.cpp`（3,058 行）与 `ZergSystem.cpp`（1,993 行）。 | 抽象 1 个核心 Server，瘦身 `BuildingSystem` / `ProtossSystem` / `ZergSystem` |
| **Phase 82** | **抽象通用科技树、升级 DAG 与属性修改器服务 (`TechAndUpgradeServer`)** | 参考 Unity DOTS `ScriptableObject` 科技树，在 `src/Engine/Gameplay/TechAndUpgradeServer.h` 中构建通用科技研发队列、多级升级（Level 1–3）前置条件 DAG 校验与属性增益计算器，对接云端 [Master_Refactor_Manifest.json](https://drive.google.com/file/d/1Q9-Wy8d3E6drxAkCsS7WdULBAEPRh11-/view?usp=drivesdk) 中的科技与升级表，瘦身 `UpgradeSystem.cpp`（1,133 行）。 | 抽象 1 个核心 Server，消除硬编码升级分支 |
| **Phase 83** | **抽象通用技能释放、能量池与 Buff/Debuff 状态机服务 (`AbilityAndStatusServer2D`)** | 参考 Unreal GAS（Gameplay Ability System），在 `src/Engine/Gameplay/AbilityAndStatusServer2D.h` 中统一管理能量恢复、技能冷却、目标/AoE 释放校验及持续状态效果（StimPack、Lockdown、Irradiate、Plague、Defensive Matrix、Ensnare），瘦身 `SpellSystem.cpp`（1,394 行）。 | 抽象轻量级 2D GAS 服务，统一三族技能与状态效果 |
| **Phase 84** | **抽象通用载具/掩体驻军容器服务 (`GarrisonAndTransportServer2D`) 与数据驱动 HUD 命令卡** | 在 `src/Engine/Gameplay/GarrisonAndTransportServer2D.h` 中抽象运输机/地堡/航母容量槽位与装卸队列；同时用声明式 JSON/表驱动命令卡替代 `UI/HudCommandPanel.cpp`（2,514 行）中的巨型 `switch-case` 按钮绑定。 | 抽象 1 个核心 Server，将 `HudCommandPanel.cpp` 压缩 60%+ |
| **Phase 85** | **巨石系统解耦（`AIBrain.cpp` 6,353 行与 `AnimationSystem.cpp` 3,422 行瘦身）与第二实验项目升级** | 将 `AIBrain.cpp` 中的硬编码战术分支全面迁移至 `Engine::AI::BehaviorTreeServer`（对接云端 [AIScriptBin_All_293_BehaviorTrees.json](https://drive.google.com/file/d/1dwkcbfIQvC7SBv5v9y9uDiCTsDuf8Emm/view?usp=drivesdk) 与 [BWScriptBin_All_156_BehaviorTrees.json](https://drive.google.com/file/d/1wwF8PI9wtwFtO8l9k0t3xepGyvDK2nN8/view?usp=drivesdk)）；将 `AnimationSystem.cpp` 的过程式状态切换收敛至 `Engine::Animation::AnimationServer2D`（对接 [IScriptBin_All_398_Controllers.json](https://drive.google.com/file/d/1za0H6IgvqUk_0kljUWugY573dVVg7OCu/view?usp=drivesdk)），构建完整的 **23-Server 通用引擎底座**。 | `AIBrain.cpp` 与 `AnimationSystem.cpp` 合计削减 3,500+ 行冗余分支 |

## 三、 代码精简化专项：24 个待移除/合并的碎片化文件清单 (Phase 78–79)

基于当前 207 个源码文件的实测引用关系，下一轮可安全移除并内联收敛以下 **24 个碎片化 `.cpp` / `.h` 文件**，进一步降低编译单元数量与头文件嵌套深度：

| 序号 | 待移除/合并文件路径 | 当前行数 | 当前引用数 | 目标收敛位置与具体改造动作 |
| :--- | :--- | :--- | :--- | :--- |
| **1** | `src/Core/Utils/Logger.cpp` | 65 | 1 | 内联至 `src/Core/Utils/Logger.h`（使用 `inline static` 成员变量），彻底消除 `Logger.cpp` 编译单元。 |
| **2** | `src/Resource/Palette.cpp` | 159 | 1 | 内联至 `src/Resource/Palette.h`，消除 `CMakeLists.txt` 中各测试目标对 `Palette.cpp` 的重复显式挂载。 |
| **3** | `src/Core/ECS/SystemManager.cpp` | 160 | 1 | 内联至 `src/Core/ECS/SystemManager.h`，使 ECS 系统调度器完全 Header-Only 化。 |
| **4** | `src/Game/Map/ChkParser.cpp` | 252 | 1 | 内联至 `src/Game/Map/ChkParser.h`，消除 `CMakeLists.txt` 中各测试目标对 `ChkParser.cpp` 的重复显式挂载。 |
| **5** | `src/Render/Renderer.h` | 21 | 3 | 仅含函数声明，直接并入 `src/Render/Draw2D.h`。 |
| **6** | `src/Render/Private/SokolGpuInterop.h` | 24 | 6 | 并入 `src/Render/GpuTextureHandle.h`，同时删除单文件子目录 `src/Render/Private/`。 |
| **7** | `src/Render/SceneNode2D.h` | 121 | 4 | 收编并入 `src/Engine/Node2D.h`，消除 `src/Engine/Scene/PrefabRegistry.h` 对 `src/Render/` 的反向依赖。 |
| **8** | `src/Game/AI/BehaviorTreeRuntime.h` | 20 | 4 | 仅含 11 行 `using` 别名，直接内联至 `src/Game/AI/AIBrain.h` 与 `src/Game/Trigger/TriggerVM.h`。 |
| **9** | `src/Game/ProtossPower.h` | 16 | 4 | 仅含 `PylonRadiusTiles = 6` 与圆域判断函数，直接并入 `src/Game/Systems/ProtossSystem.h`。 |
| **10** | `src/Game/PlayerColor.h` | 30 | 3 | 仅含 `PlayerColorForID(int)` 单个函数，直接并入 `src/Game/DataEnums.h`。 |
| **11** | `src/Game/Systems/GeyserState.h` | 39 | 9 | 仅含气矿占领状态判断辅助函数，直接并入 `src/Game/Systems/HarvestSystem.h`。 |
| **12** | `src/Game/Events/GameEvents.h` | 58 | 3 | `src/` 下仅被 `DeclarativeAnimationPlayer.h` 包含，直接并入 `src/Game/Systems/DeclarativeAnimationPlayer.h`（并删除单文件子目录 `src/Game/Events/`）。 |
| **13** | `src/Client/Entity.h` | 45 | 6 | 早期客户端过渡结构体，直接并入 `src/Client/EntityRenderer.h`。 |
| **14** | `src/Client/SelectionFootprint.h` | 214 | 3 | 仅服务于选框与建筑占地计算，直接并入 `src/Client/Selection.h`。 |
| **15** | `src/Main/GameLoop/EcsSync.h` | 32 | 2 | 在 `src/` 下已无独立调用方，并入 `src/Main/GameLoop/CursorManager.h`。 |
| **16** | `src/Main/GameLoop/HudResourceSync.h` | 47 | 4 | 仅含 2 个简短同步函数，并入 `src/Main/GameRuntimeFrame.h`。 |
| **17** | `src/Main/GameMatchOutcomeRuntime.h` | 106 | 2 | 仅被 `GameRuntimeFrame.h` 调用，直接并入 `src/Main/GameRuntimeFrame.h`。 |
| **18** | `src/Main/GameLoop/CursorTargeting.h` | 111 | 1 | 在 `src/` 下零调用，直接并入 `src/Main/GameLoop/CursorManager.h`。 |
| **19** | `src/Main/GameLoop/CursorTypes.h` | 269 | 7 | 直接并入 `src/Main/GameLoop/CursorManager.h`，统一光标类型与管理器。 |
| **20** | `src/Main/GameLoop/InputHandler.h` | 114 | 2 | 仅被 `GameCommandDispatchRuntime.h` 包含，直接并入 `src/Main/GameCommandDispatchRuntime.h`。 |
| **21** | `src/Resource/GrpToGpu.h` | 86 | 4 | 直接并入 `src/Resource/GrpDecoder.h`。 |
| **22** | `src/UI/MainGuiSelectionPresentation.h` | 63 | 2 | 仅被 `MainGui.cpp` 使用，直接并入 `src/UI/MainGui.h`。 |
| **23** | `src/UI/HudMinimapGeometry.h` | 100 | 4 | 直接并入 `src/UI/HudLayout.h`。 |
| **24** | `src/Core/Math/Rect.h` | 33 | 4 | 直接并入 `src/Core/Math/Vec2.h`（统一基础 2D 数学原语头）。 |

## 四、 通用引擎底座演进：从 19-Server 迈向 23-Server 完整 RTS/2D 引擎底座 (Phase 81–84)

在现有 19 个引擎 Server 的基础上，针对 `src/Game/Systems/` 中尚未抽象的四大核心玩法领域，新增 **4 个通用 Gameplay Server**，使 `Engine::EngineContext` 形成覆盖 **2D 渲染、物理、导航、视野、音频、动画、AI、网络、UI、经济、战斗、建造、科技、技能、驻军** 的 **23-Server 完整闭环**：

### 1. `Engine::Gameplay::BuildingPlacementServer2D` (`src/Engine/Gameplay/BuildingPlacementServer2D.h`)
* **职责定位**：通用 2D 网格建造占位、地形可建性校验、能量场/领域场（Power / Creep Matrix）覆盖判定与施工进度服务。
* **核心接口**：
  * `CanPlaceBuilding(TileX, TileY, FootprintW, FootprintH, PlacementFlags, FactionID)`：统一校验瓦片可通行/可建造掩码、高地边缘禁建区、气矿（Vespene Geyser）重叠要求、主基地与矿区最小安全距离。
  * `RebuildPowerAndCreepFields(Providers)`：维护神族水晶塔圆域能量矩阵（替代原 `ProtossPower.h` 与 `ProtossSystem.cpp` 局部逻辑）及虫族菌毯扩散网格（替代 `ZergSystem.cpp` 中分散的菌毯网格更新）。
  * `AttachAddonSocket(MainBuildingID, AddonUnitID)`：通用主建筑与附属建筑（如兵营/重工/机场科技球）2D 网格拼接管理。

### 2. `Engine::Gameplay::TechAndUpgradeServer` (`src/Engine/Gameplay/TechAndUpgradeServer.h`)
* **职责定位**：通用多阵营科技树解锁、多级属性升级（Level 1–3+）DAG 依赖校验与研发计时队列服务。
* **核心接口**：
  * `ImportFromModernRegistry(const Resource::ModernAssetRegistry&)`：从云端 `assets-R` 自动导入全部科技与升级项的矿气造价、研发帧数及前置建筑/科技 ID。
  * `CanResearchTech(PlayerID, TechID, ActiveBuildingTypes)` / `CanStartUpgrade(PlayerID, UpgradeID, ActiveBuildingTypes)`：O(1) 校验玩家当前等级、前置建筑状态与资源余额。
  * `StepResearchQueues(DeltaTicks)`：确定性步进所有正在研发的科技与升级槽位，完成后自动更新玩家 `UpgradeLevel[PlayerID][UpgradeID]` 与 `TechUnlocked[PlayerID][TechID]` 位掩码。

### 3. `Engine::Gameplay::AbilityAndStatusServer2D` (`src/Engine/Gameplay/AbilityAndStatusServer2D.h`)
* **职责定位**：参考 Unreal GAS 的轻量级 2D 技能释放、能量（Mana/Energy）池恢复与定时 Buff/Debuff/DoT 状态机服务。
* **核心接口**：
  * `StepEnergyRegeneration(DeltaTicks)`：按固定点数速率（如星际争霸每帧 `8/256` 能量点）推进施法单位能量上限（200 / 250）恢复。
  * `CastAbility(CasterID, AbilityID, TargetEntityID, TargetPos)`：统一处理能量扣除、冷却计时、施法距离校验与投射物/瞬发 AoE 触发。
  * `ApplyStatusEffect(TargetID, StatusKind, DurationTicks, Magnitude)` & `StepStatusEffects(DeltaTicks)`：统一驱动 `StimPack`（攻速/移速翻倍）、`Lockdown`（机械瘫痪）、`DefensiveMatrix`（250 点临时吸收盾）、`Irradiate`（范围辐射 DoT）、`Plague`（持续扣血至 1 HP）、`Ensnare`（减速）等状态效果，大幅精简 `SpellSystem.cpp`（1,394 行）。

### 4. `Engine::Gameplay::GarrisonAndTransportServer2D` (`src/Engine/Gameplay/GarrisonAndTransportServer2D.h`)
* **职责定位**：通用载具装载（Dropship / Shuttle / Overlord）、掩体驻军（Bunker 内驻兵向外射击）与母舰机库（Carrier / Reaver 子单位容量）管理服务。
* **核心接口**：
  * `CanLoadPassenger(ContainerID, PassengerID, PassengerSpaceCost)`：校验 8 格容量上限、单位体型占位（1 / 2 / 4 格）与同盟归属。
  * `LoadPassenger` / `UnloadNextPassenger` / `OnContainerDestroyed`：统一管理装载隐藏、逐个卸载冷却与载具坠毁时的乘客连带结算，大幅精简 `TransportSystem.cpp`（799 行）。

## 五、 实施步骤与回归验证计划

为保证重构过程零功能回退、每一步均可独立编译验证，建议按以下两阶段批次推进实施：

1. **第一批次（Phase 78–80：架构解耦、24 文件精简与双轨代码合一）**：
   * 完成 `Logger.cpp`、`Palette.cpp`、`SystemManager.cpp`、`ChkParser.cpp` 的 Header-Only 内联及全部 24 个微型文件的合并清理。
   * 将 `src/Render/SceneNode2D.h` 等下沉至 `src/Engine/`，实现 `src/Engine/` 目录零 `src/Render/` 外部头文件依赖。
   * 将 `NavigationGrid.h` / `NavigationService.h` 及 `LockstepSync.h` / `Replay.h` 重构为 `Engine::Navigation` 与 `Engine::Net` 的薄委托层。
2. **第二批次（Phase 81–85：4 大新 Gameplay Server 抽象、巨石 `.cpp` 瘦身与多项目验证）**：
   * 依次实现 `BuildingPlacementServer2D`、`TechAndUpgradeServer`、`AbilityAndStatusServer2D`、`GarrisonAndTransportServer2D`，并接入 `EngineContext`（升级为 23-Server 矩阵）。
   * 将 `BuildingSystem`、`UpgradeSystem`、`SpellSystem`、`TransportSystem` 绑定至上述 4 大引擎 Server，并推进 `AIBrain.cpp`、`AnimationSystem.cpp`、`HudCommandPanel.cpp` 的数据驱动瘦身。
   * 新增 `tests/Regression/Phase78To85EngineZeroCouplingAndGameplayServersTest.cpp` 专项回归测试套件，全面校验 24 文件退役、引擎零外部耦合、4 大新 Gameplay Server 与多项目宿主运行。
