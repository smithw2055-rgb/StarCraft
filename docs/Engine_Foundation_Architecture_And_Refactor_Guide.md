# 现代游戏引擎改造与通用引擎底座架构指南 (Phase 31–48 全栈演进)

本文档基于最新上传代码库（`smithw2055-rgb/starcraft`）与 Google Drive 云端游戏资源目录 [StarCraft](https://drive.google.com/drive/folders/1-6rATljWabXQ3Rnhm2x4h1ZBeon6aOQJ)（包含现代化云端资产库 [assets-R](https://drive.google.com/drive/folders/1AwtMYu7okUhitYqKexHoVYTOBcC1dInK)、完整打包归档 [StarCraft_Assets_R_Complete_Package.zip](https://drive.google.com/file/d/1c7MNfNGfRZitK2xJp1BBwn6QihHpN34K/view?usp=drivesdk) 与原始重制版资源库 [assets](https://drive.google.com/drive/folders/1sQMTil7WzXO2zQEZoH6qRJADYGTwZNwJ)），系统总结了参考 **Godot 4**（Server 架构、Node2D/SceneTree、AnimationTree、AudioServer、PhysicsServer2D、ThemeDB）与 **Unity / Unity DOTS**（Data-Oriented ECS、RenderGraph、ScriptableObject、Netcode Rollback）所完成的**代码精简化（去冗余与移除过时代码）**及**通用化 2D/RTS 引擎底座抽象（`src/Engine/`）**架构设计。

## 一、 代码精简化与移除过时/冗余设计全景总结 (Phase 31–45)

在 Phase 31–42 已清理 22 个过时/碎片文件的基础上，**Phase 43–45** 进一步对 `src/Client/`、`src/UI/`、`src/Main/`、`src/Resource/` 与 `src/Game/` 进行了深度去冗余与死代码剥离，**本轮新增清理与合并删除 35 个冗余 `.h` / `.cpp` 文件**：

| 重构阶段 | 清理模块 | 移除 / 合并精简文件清单（共 35 个文件） | 核心重构说明与收益 |
| :--- | :--- | :--- | :--- |
| **Phase 43：客户端与 UI 死代码移除及碎片编译单元合并** | `src/Client/`<br>`src/UI/` | • **死文件删除**：`src/Client/ProjectileSystem.h` / `.cpp`、`src/UI/GuiHost.h` / `.cpp`、`src/Client/SelectionOverlayPresentation.h` / `.cpp`<br>• **微型状态头 3 合 1**：删除 `src/UI/HudResourceState.h`、`src/UI/HudSelectionState.h`、`src/UI/HudMinimapState.h`（统一并入 `src/UI/HudState.h`）<br>• **HUD 碎片 `.cpp` 5 合 2**：删除 `src/UI/HudConsole.cpp`、`src/UI/HudInput.cpp`、`src/UI/HudCommandPanelRender.cpp`（并入 `src/UI/Hud.cpp`）；删除 `src/UI/HudCommandButtons.cpp`、`src/UI/HudCommandActions.cpp`（并入 `src/UI/HudCommandPanel.cpp`） | 彻底移除全工程零引用的旧弹道系统 `ProjectileSystem`、已被 `GuiHudHost`/`GlueGuiHost` 取代的 `GuiHost` 以及空函数 `Hud::DrawMinimap(float,float,float,float)`；消除 `HudInfoPanel.cpp` 内部 4 处中部重复 `#include`，并将 `CmdButtonDef` 构造函数内联化。 |
| **Phase 44：`src/Main/` 微型单函数薄包装头文件清理** | `src/Main/` | • **战役与 GUI 头文件收敛**：删除 `src/Main/CampaignText.h`、`src/Main/CampaignPortrait.h`、`src/Main/CampaignAudio.h`（收敛至 `src/Main/CampaignPresentation.h` / `.cpp`）；删除 `src/Main/GuiRuntime.h`、`src/Main/GameGuiBootstrap.h`（收敛至 `src/Main/GameGuiPresentation.h`）<br>• **单调用点薄包装内联删除**：删除 `src/Main/GameHudBootstrap.h`、`src/Main/GameRuntimeBootstrap.h`、`src/Main/GameClientLifecycle.h`、`src/Main/GameStartRuntime.h`、`src/Main/GameGlueMenuActionRuntime.h`（内联至唯一调用方 `src/Main/GameController.cpp`）；删除 `src/Main/GameClientViewRuntime.h`（内联至 `src/Main/GameInputRuntime.h`）；删除 `src/Main/GameWorldPresentation.h`（并入 `src/Main/GameUiPresentation.h`） | 消除 `src/Main/` 下 12 个仅含 15–45 行单函数转发的过渡头文件，大幅缩短头文件包含链并提升 `GameController` 生命周期代码的可读性。 |
| **Phase 45：资源层微型类型头/解码器合并与死静态变量清理** | `src/Resource/`<br>`src/Game/` | • **资源内部状态头合并**：删除 `src/Resource/ArchiveSourceTypes.h`、`src/Resource/DataAssetTypes.h`、`src/Resource/SpriteAssetTypes.h`（并入唯一包含方 `src/Resource/ResourceCache.h`）；删除 `src/Resource/TilesetAssetTypes.h`（并入 `src/Resource/TilesetAssetCacheView.h`）；删除 `src/Resource/PcxRegion.h`（并入 `src/Resource/UIAssetTypes.h`）<br>• **PCX 与 RGBA 辅助编译单元合并**：删除 `src/Resource/PcxToGpu.h` / `.cpp`、`src/Resource/UIAssetImageHelpers.h` / `.cpp`（并入唯一调用方 `src/Resource/ResourceCacheUI.cpp`）<br>• **消除系统死静态变量与回滚冗余**：移除 `src/Game/Systems/CommandSystem.cpp` 中从未被读取却在每次 `SetMap` 时触发全图 Flood-Fill 的死静态变量 `sRegionPathfinder` 与 `RebuildRegionPathfinder`；精简 `src/Game/Networking/RollbackStateManager.h` 直接委托给 `Engine::ECS::GenericWorldSnapshot`。 | 彻底消除 `ResourceCache` 周边的碎片化单结构体头文件与旧版 PCX 独立包装层，消除 `CommandSystem::SetMap` 中的无效 O(W×H) 区域重建开销。 |

## 二、 通用化现代游戏引擎底座抽象 (`src/Engine/`, Phase 34–48)

为将本项目打造为通用 2D/RTS 现代游戏引擎的实验性标杆项目，`src/Engine/` 构建了完整的 **12 大核心引擎 Server 与序列化/场景模块矩阵**，并统一挂载在 `Engine::EngineContext` 与微内核宿主 `Engine::EngineApp`（`src/Engine/Engine.h`）之上：

| 阶段 | 引擎底座模块 | 头文件位置 | 对标现代引擎架构 | 核心职责与云端 `assets-R` 集成能力 |
| :--- | :--- | :--- | :--- | :--- |
| **Phase 34–37** | **类型擦除 ECS 快照、回滚与 CRC32 二进制序列化器** | `src/Engine/ECS/GenericWorldSnapshot.h`<br>`src/Engine/ECS/WorldSerializer.h` | Unity DOTS Serialization / Netcode | 基于 `IComponentPool` 二进制直拷与 `CustomComponentBinarySerializer<T>`，提供零游戏组件耦合的 `GenericWorldSnapshot`、`GenericRollbackBuffer<64>` 与带 `"SCWS"` 魔数头及 CRC32 校验的 `WorldSerializer`。 |
| **Phase 35 & 38** | **层次化 2D 场景树与声明式多通道渲染图** | `src/Engine/Node2D.h`<br>`src/Engine/Render/RenderGraph2D.h` | Godot 4 `Node2D` / `RenderingServer` & Unity URP `RenderGraph` | `Engine::Node2D` 与 `SceneTree2D` 支持父子层级变换、32 方向对称翻转（17 向图集镜像）与命名 2D Socket 挂载；`RenderGraph2D` 按拓扑顺序调度 `TerrainPass` -> `ShadowPass` -> `PBRSpritePass` -> `ParticlePass` -> `FogOfWarPass` -> `UICanvasPass`，对接云端 [PBR_Bundles_Manifest.json](https://drive.google.com/file/d/1WNXwJr1kO89Bvu_v_MvkQhlPWRtHzYMM/view?usp=drivesdk)、[Unity URP PBR Shader](https://docs.google.com/document/d/18fEcJZPi1nDFh-y9ZqrPGoGSJNkl8Y7JrPIQDnRm3v0/edit?usp=drivesdk&ouid=102422307773299752351) 与 [Godot 4 PBR Shader](https://docs.google.com/document/d/1Fp5kYiuei0Rjoz9IAsVBLRWm3eQSrOKpl1qUcGuCeb0/edit?usp=drivesdk&ouid=102422307773299752351)。 |
| **Phase 39** | **语义输入-确定性命令路由器** | `src/Engine/Input/CommandRouter.h` | Godot `InputMap` + RTS Lockstep Queue | 将 `Core::Input::InputActionMap` 语义动作按可配置帧延迟转换为带确定性 Tick 戳与玩家 ID 排序的帧命令包。 |
| **Phase 40 & 47** | **通用 2D 导航、高程斜坡连通域与流场服务** | `src/Engine/Navigation/NavigationServer2D.h` | Godot 4 `NavigationServer2D` / RTS FlowField | 内置 O(W×H) 动态规划真实通行裕度场（True Clearance Field，支持 1×1 至 4×4 体型）、**高程层级（`ElevationLevel`）与斜坡（`IsRamp`）通行判定**、**Flood-Fill 连通域预计算（`RebuildConnectedRegions` + O(1) `AreCellsConnected` 孤岛/高地可达性预判）**、8 向防切角 A* 寻路、Dijkstra 流场（Flow Field）与局部避障分离速度求解。 |
| **Phase 40** | **通用 AI 行为树与黑板服务** | `src/Engine/AI/BehaviorTreeServer.h` | Unreal / Godot `BehaviorTree` + `Blackboard` | 提供强类型 `Blackboard` 与组合节点，直接导入云端 [AIScriptBin_All_293_BehaviorTrees.json](https://drive.google.com/file/d/1dwkcbfIQvC7SBv5v9y9uDiCTsDuf8Emm/view?usp=drivesdk)（293 棵原版 AI 行为树）与 [BWScriptBin_All_156_BehaviorTrees.json](https://drive.google.com/file/d/1wwF8PI9wtwFtO8l9k0t3xepGyvDK2nN8/view?usp=drivesdk)（156 棵母巢之战 AI 行为树，详见 [BT_Showcase_And_Statistics_Manifest.json](https://drive.google.com/file/d/1eRWqNOatrPLZ2ojBDm323Rdxw9rsYcRF/view?usp=drivesdk)）。 |
| **Phase 41** | **声明式预制体与场景组装注册表** | `src/Engine/Scene/PackedScene2D.h`<br>`src/Engine/Scene/PrefabRegistry.h` | Godot `PackedScene` / Unity `Prefab` | 支持声明式定义父子 `Node2D` 树（主精灵、自动关联 `images.rel` 阴影子节点、挂载在 `TurretSocket` 上的子炮塔）及 ECS 组件初始化器，从云端 [UnitDataAssets_All_228.json](https://drive.google.com/file/d/1HUI_H6F_S6Q7G0t5Q3TyBaSDFnK8rsHz/view?usp=drivesdk) 与 [HD_Image_Render_Table_999.json](https://drive.google.com/file/d/12y6-UtvnWRrSLe65SqkpkhAB56-yKhE3/view?usp=drivesdk) 一键生成全部 **228 个单位/建筑预制体**。 |
| **Phase 42 & 48** | **确定性回滚会话、网络传输与遥测检视服务** | `src/Engine/Net/DeterministicSessionServer.h`<br>`src/Engine/Net/NetworkTransportServer.h`<br>`src/Engine/Debug/EngineTelemetryServer.h` | Godot `MultiplayerPeer` / Unity Netcode & Profiler | `INetworkTransport` 与 `LoopbackNetworkHub` 提供跨节点帧命令包（`FrameCommand`）与帧哈希校验包（`FrameHashVerify`）收发；`PumpTransportIntoSession` 自动将迟到网络包注入 `DeterministicSessionServer` 触发回滚与重演；`EngineTelemetryServer` 提供系统耗时剖析与 ECS 组件池内存检视。 |
| **Phase 46** | **通用声明式 6 轨道动画与 2D 骨骼挂点服务** | `src/Engine/Animation/AnimationServer2D.h` | Godot 4 `AnimationPlayer` / `AnimationTree` | 直接导入云端 [IScriptBin_All_398_Controllers.json](https://drive.google.com/file/d/1za0H6IgvqUk_0kljUWugY573dVVg7OCu/view?usp=drivesdk)（**398 个动画控制器、1,364 条声明式动画片段**，参见 [Godot4_Terran_SCV_AnimLibrary.tres](https://docs.google.com/document/d/14x-8N3nv6Do5asFk28clJ0yR1rKY1lvsQs8zC3nSfAM/edit?usp=drivesdk&ouid=102422307773299752351)）与 [Sockets_2D_MountTracks.json](https://drive.google.com/file/d/1ypSJ-K2xfayTlHNMdzNlYfHdd53aD2mG/view?usp=drivesdk)（2D Socket 骨骼挂载轨迹），按 Tick 求值全部 6 条轨道（`SpriteFrame`、`AudioNotify`、`CombatNotify`、`SocketVfxSpawn`、`RootMotion`、`FsmSignal`），支持 `nobrkcodestart` 霸体锁帧与 32 朝向自动镜像的 HD 4x Socket 坐标采样（`SampleSocketOffsetHD`）。 |
| **Phase 46** | **通用 2D 空间音频、同帧去重合并与总线混音服务** | `src/Engine/Audio/AudioServer2D.h` | Godot 4 `AudioServer` / `AudioStreamPlayer2D` | 直接导入云端 [Sound_Catalog_1144_SFX.json](https://drive.google.com/file/d/1OvqWatngjC4Y7xiDcIU0hB_hwOfNC3LE/view?usp=drivesdk)（**1,144 条音效资源索引**），实现**同帧空间音频去重合并（Spatial Sound Coalescing）**（当集群单位在同一帧于 `CoalesceRadiusPx` 半径内齐射时自动合并为单一声道并施加对数增益补偿），结合 2D 距离衰减、立体声声像（Stereo Pan）与 `AudioBusMixer` 语音闪避（Voice Ducking）。 |
| **Phase 47** | **通用确定性 2D 物理、射线检测与集群分离服务** | `src/Engine/Physics/PhysicsServer2D.h` | Godot 4 `PhysicsServer2D` / Unity DOTS Physics | 基于 `DeterministicSpatialGrid2D` 宽相网格，提供分层碰撞掩码（`GroundUnit`、`AirUnit`、`Building`、`Resource`、`Projectile`、`TriggerZone`）、`OverlapCircle`、`OverlapAABB`、解析式 2D 射线检测（`Raycast2D`，精确返回命中点、距离与表面法线）、触发器区域重叠检测（`EvaluateTriggerOverlaps`）以及基于定点数 `Core::Math::fp8` 的确定性集群拥挤分离速度求解（`SolveDeterministicSeparation`）。 |
| **Phase 48** | **通用 2D 声明式 UI 控件树、主题库与云端场景加载服务** | `src/Engine/UI/UIServer2D.h`<br>`src/Engine/Scene/SceneLoaderServer.h` | Godot 4 `Control` / `ThemeDB` / `ResourceLoader` | `UIServer2D` 提供三族控制台样式表 `ThemeDB`、3×3 命令面板构建器（`BuildCommandGrid3x3`）、屏幕点击动作路由（`RoutePointerClick`）、`RenderQueue2D` 批次提取，以及从云端 [Terrain_CHK_And_HD_Effects_Manifest.json](https://drive.google.com/file/d/1EMvHlQqmTKHPHJ-Nd2voHDSxhEycYrkZ/view?usp=drivesdk) 自动导入全部 **12 套 HD UI 与特效图集**（含 [T_UI_Command_Icons_HD_Atlas_390Frames.png](https://drive.google.com/file/d/1IHKvsqta3I6I2V87XNCTyk62Lr4Lj_Na/view?usp=drivesdk)、[T_UI_Group_Wireframes_HD_Atlas_131Frames.png](https://drive.google.com/file/d/1J_fmyh9HqlKqcnxZ1SxBKDe7zMe_JKFa/view?usp=drivesdk)、[T_UI_Transport_Wireframes_HD_Atlas_106Frames.png](https://drive.google.com/file/d/1b1tDCxLjx5_uBA7vf5Mix1d9suovSMhD/view?usp=drivesdk)）；`SceneLoaderServer` 直接解析云端 [Byways_Chk_Full_Scene.json](https://drive.google.com/file/d/1IoMR_iP1DMWSUvv-2kH2AY4hyD-5RlkH/view?usp=drivesdk)、8 套 GPU 地形 LUT 与 [Foliage_HD_Manifest.json](https://drive.google.com/file/d/1kNQYljeaYh-RCWJHywAjqz8aQ-MRrfQO/view?usp=drivesdk)（395 条动态植被记录），一键初始化导航网格、物理碰撞体并在 ECS 世界与 `SceneTree2D` 中生成全部 34 个地图实体。 |

## 三、 通用引擎底座最小接入与全栈插件示例

借助 `src/Engine/Engine.h`，无论是运行《星际争霸：重制版》还是基于该底座开发全新的 2D/RTS 游戏，均可通过统一的 `Engine::EngineApp` 与 `Engine::IPlugin` 接口驱动：

```cpp
#include "Engine/Engine.h"
#include "Game/StarCraftEnginePlugin.h"

int main() {
    // 1. 初始化通用 2D/RTS 引擎微内核宿主（24Hz 确定性固定步长 + 自动回滚快照）
    Engine::EngineApp App(1.0f / 24.0f, /*MaxSubSteps=*/4);
    App.SetAutoRecordRollbackSnapshots(true);

    // 2. 挂载星际争霸游戏插件（自动从云端 assets-R 注册 228 个单位预制体、398 个动画控制器、
    //    1,144 条音效、12 套 HD UI 图集与 449 棵 AI 行为树）
    App.AddPlugin<Game::StarCraftEnginePlugin>("assets-R");
    App.Initialize();

    // 3. 一键加载云端 Byways_Chk_Full_Scene.json 场景（自动配置 96x96 导航网格、物理宽相并生成 34 个场景实体）
    App.Context().SceneLoader().LoadSceneFromAssetsR(
        "assets-R",
        App.Entities(),
        App.Context().SceneTree(),
        App.Context().Prefabs(),
        &App.Context().Navigation2D(),
        &App.Context().Physics2D());

    // 4. 推进一帧（内部自动调度 FixedUpdate、AudioServer2D 空间混音、SceneTree2D 矩阵传播与表现层更新）
    App.StepFrame(1.0f / 60.0f);

    App.Shutdown();
    return 0;
}
```

## 四、 Phase 43–48 专项测试与全量回归验证结果

新增 `tests/Regression/Phase43To48EngineGeneralizationAndSimplificationTest.cpp` 并在 `CMakeLists.txt` 中注册构建目标，与仓库现有的全部回归及现代化阶段测试套件一起执行了全量编译与运行验证（**全部测试套件 100% 通过**）：

1. **`TestPhase43To45StreamlinedCodebaseAndRollbackAdapter`**：验证 35 个冗余/死代码文件已彻底从仓库移除、合并后的 `UI::HudState`（含 `HudResourceState`、`HudSelectionState`、`HudMinimapState`）正常工作，以及精简后的 `RollbackStateManager` 通过 `GenericWorldSnapshot` 完成帧回滚与重演。
2. **`TestPhase46AnimationServer2DAndAudioServer2D`**：验证 `AnimationServer2D` 导入云端 398 个动画控制器（1,364 个动画片段）与 2 条 2D Socket 轨迹，驱动 `IScript_198` 死亡动画完成关键帧推进与 `SoundID=53` 音效帧事件派发，并验证 32 朝向镜像下 SCV 焊枪火花挂点坐标翻转（`+44.0` vs `-44.0`）；验证 `AudioServer2D` 导入全部 1,144 条音效并将同帧 12 次密集射击音效合并为 1 个空间声道。
3. **`TestPhase47PhysicsServer2DAndNavigationElevationRegions`**：验证 `PhysicsServer2D` 的 `OverlapCircle`、解析式 `Raycast2D`（精确命中建筑 AABB 距离 `220.0` 与法线 `(-1, 0)`）、触发器区域重叠检测与 `fp8` 定点数集群分离推力；验证 `NavigationServer2D` 高程悬崖阻断、连通域分割（2 个孤立区域）以及添加斜坡（`IsRamp=true`）后连通域合流与跨高地 A* 寻路。
4. **`TestPhase48UIServer2DSceneLoaderAndNetworkTransport`**：验证 `UIServer2D` 加载云端 12 套 HD UI 图集（含 390 帧命令图标图集）、构建 3×3 命令面板并路由鼠标点击至 `"Command.StimPack"`；验证 `SceneLoaderServer` 加载 `Byways_Chk_Full_Scene.json`（96×96 地图、34 个生成实体、8 套 GPU 地形 LUT、395 条动态植被记录）；验证 `LoopbackNetworkHub` 与双端 `DeterministicSessionServer` 在网络延迟下投递迟到帧命令、自动回滚重演并达成位级 FNV-1a 状态哈希一致。
