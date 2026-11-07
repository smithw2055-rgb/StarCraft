# 现代游戏引擎改造与通用引擎底座架构指南 (Phase 31–36)

本文档基于最新上传代码库（`smithw2055-rgb/starcraft`）与云端游戏资源目录 [StarCraft](https://drive.google.com/drive/folders/1-6rATljWabXQ3Rnhm2x4h1ZBeon6aOQJ)（包含现代化云端资产库 [assets-R](https://drive.google.com/drive/folders/1AwtMYu7okUhitYqKexHoVYTOBcC1dInK) 与原始重制版资源库 [assets](https://drive.google.com/drive/folders/1sQMTil7WzXO2zQEZoH6qRJADYGTwZNwJ)），总结了参考 **Godot 4** 与 **Unity / Unity DOTS** 所完成的**代码精简化（去冗余与移除过时代码）**及**通用化 2D/RTS 引擎底座抽象（`src/Engine/`）**架构设计。

## 一、 精简化与去冗余重构总结 (Phase 31–33)

### 1. 移除死代码与过时遗留模块 (Phase 31)

| 清理类别 | 移除/精简文件 | 重构说明 |
| :--- | :--- | :--- |
| **未引用遗留文件删除** | `src/inspect_portrait.cpp`<br>`src/Resource/PcxDecoder.h` / `.cpp`<br>`src/Client/Entity.cpp`<br>`tests/Regression/WebmAssetCoverageTest.x`<br>`tests/Regression/WebmVideoDecodeTest.x` | 彻底移除未接入构建系统的调试小程序、已被 `WpePalette` + `GrpDecoder` + `DdsDecoder` 取代的 `PcxDecoder`、空白源文件以及废弃的 `.x` 测试残留。 |
| **Pre-ECS 客户端实体层裁剪** | `src/Client/Entity.h`<br>`src/Client/EntityRenderer.h` / `.cpp` | 移除早期非 ECS 架构遗留的 `EntityImageNode`、`Game::Client::Entity` 与 `Game::Client::EntityManager` 类，以及 `EntityRenderer` 中约 350 行死代码（`ResolveFrames`、`UpdateAnimation`、`DrawSelectionMarker(const Entity&)`、`DrawImageNode`、`DrawImageNodeTree`、`Render(const EntityManager&)`），使渲染管线 100% 运行在 ECS 查询与 `RenderServer2D` 之上。 |
| **碎片化编译单元合并** | `src/Main/GameController.cpp`<br>`src/Resource/ResourceCache.cpp` | 将原本拆散的 5 个 `GameController*.cpp` 碎片合并为单一内聚的 `src/Main/GameController.cpp`；将 5 个 `ResourceCache*.cpp` 碎片合并入 `src/Resource/ResourceCache.cpp`，大幅降低维护负担与跨文件跳转成本。 |

### 2. 消除跨世界全局静态状态与统一云端资产桥接 (Phase 32)

* **消除系统层静态全局变量**：
  * `src/Game/Systems/CombatSystem.cpp`：新增 `ResolveCombatMap(EM)`，优先从 `Game::SimulationContext::TryGet(EM)` 读取每个 `World` 独立的 `GameMap`，并在护盾恢复遍历中切换至 `EM.GetEntitiesWithCached<UnitComponent>()` 零分配视图。
  * `src/Game/Systems/BulletSystem.cpp`：移除全局静态容器 `sActiveHaloRockets`，改为存储在 `EM.Context()` 中的 `BulletWorldStateResource`，彻底解决多 `World` 并行仿真或回滚时的状态串扰；同时将投射物遍历切换为 `EM.GetEntitiesWithCached<TransformComponent, BulletComponent>()`。
* **统一云端 `assets-R` 数据资产桥接（`src/Game/Systems/SystemAssetBridge.h`）**：
  * 提供 `Game::Systems::SystemAssetBridge::BindModernAssetRegistry(const Resource::ModernAssetRegistry&)` 与 `UnbindAll()`，一行代码即可将云端 [assets-R](https://drive.google.com/drive/folders/1AwtMYu7okUhitYqKexHoVYTOBcC1dInK) 中的 [UnitDataAssets_All_228.json](https://drive.google.com/file/d/1HUI_H6F_S6Q7G0t5Q3TyBaSDFnK8rsHz/view?usp=drivesdk)（228 个单位数据资产）、[IScriptBin_All_398_Controllers.json](https://drive.google.com/file/d/1za0H6IgvqUk_0kljUWugY573dVVg7OCu/view?usp=drivesdk)（398 个动画状态机控制器）、[HD_Image_Render_Table_999.json](https://drive.google.com/file/d/12y6-UtvnWRrSLe65SqkpkhAB56-yKhE3/view?usp=drivesdk)（999 项 HD 渲染表）、[PBR_Bundles_Manifest.json](https://drive.google.com/file/d/1WNXwJr1kO89Bvu_v_MvkQhlPWRtHzYMM/view?usp=drivesdk)、[AIScriptBin_All_293_BehaviorTrees.json](https://drive.google.com/file/d/1dwkcbfIQvC7SBv5v9y9uDiCTsDuf8Emm/view?usp=drivesdk)、[Byways_Chk_Full_Scene.json](https://drive.google.com/file/d/1IoMR_iP1DMWSUvv-2kH2AY4hyD-5RlkH/view?usp=drivesdk) 与 [Terrain_CHK_And_HD_Effects_Manifest.json](https://drive.google.com/file/d/1EMvHlQqmTKHPHJ-Nd2voHDSxhEycYrkZ/view?usp=drivesdk)（详见架构规范 [Modern_Engine_Asset_Refactoring_Spec.md](https://docs.google.com/document/d/1qppinUl3_CLOeF5Y4_VulkNIvJ98LKSJFOUDtE-xKCY/edit?usp=drivesdk&ouid=102422307773299752351)）一次性注入 `ProductionSystem`、`BuildingSystem`、`CommandSystem`、`NukeAndSpiderMineSystem`、`SpellSystem`、`ZergSystem`、`AnimationSystem` 与 `BulletSystem` 八大系统。

### 3. CMake 构建脚本精简化 (Phase 33)

* 在 `CMakeLists.txt` 中引入 `StarCraftCoreObj` 与 `StarCraftSimulationObj` 共享 `OBJECT` 库，以及 `starcraft_configure_test_target()` 和 `starcraft_add_modern_phase_test()` 两个辅助函数，消除 32 处重复的编译器与头文件目录配置块，将 `CMakeLists.txt` 从 **2,309 行缩减至 1,796 行（净减 513 行，降幅 22.2%）**。

## 二、 通用化现代游戏引擎底座抽象 (`src/Engine/`, Phase 34–36)

为将本项目作为通用 2D/RTS 现代游戏引擎的实验性标杆项目，本次重构在 `src/Engine/` 与 `src/Core/ECS/` 下抽象出完全零 `Game/` 依赖的通用引擎层：

| 引擎底座模块 | 头文件位置 | 借鉴引擎理念 | 核心职责与设计亮点 |
| :--- | :--- | :--- | :--- |
| **类型擦除 ECS 快照与回滚内核** | `src/Core/ECS/EntityManager.h`<br>`src/Engine/ECS/GenericWorldSnapshot.h` | Unity DOTS / Netcode | 在 `IComponentPool` 中内置 `CapturePoolBinary` / `RestorePoolBinary`（针对 Trivially Copyable 组件直接 `std::memcpy` 整块稠密数组）与 `ClonePoolState` / `RestoreFromClonedPool`（针对含 `std::vector` 等动态容器的组件深拷贝）。`Engine::ECS::GenericWorldSnapshot` 与 `GenericRollbackBuffer<N>` 无需任何游戏特定组件列表即可对任意 ECS `World` 执行确定性快照、回滚与重放。 |
| **确定性固定步长与插值时钟** | `src/Engine/EngineTime.h` | Godot `SceneTree` / Unity `FixedUpdate` | `Engine::TimeStepAccumulator` 解耦可变帧率渲染与固定步长逻辑仿真（如 StarCraft 24Hz Tick），内置防死亡螺旋（Spiral-of-Death）最大步数钳制与渲染插值系数 `InterpolationAlpha`（`0.0 ~ 1.0`）。 |
| **层次化 2D 场景树与挂点系统** | `src/Engine/Node2D.h` | Godot `Node2D` / `SceneTree` | 提供 `Engine::Transform2D`、`Engine::Node2D`（支持父子层级、脏标记 `DirtyTransform` 局部到世界矩阵传播、命名挂点 `Socket` 如炮塔/枪口/尾焰锚点）以及对象池化的 `Engine::SceneTree2D`。 |
| **泛型数据资产注册表** | `src/Engine/DataAssetRegistry.h` | Unity `ScriptableObject` / Godot `Resource` | `Engine::DataAssetTable<AssetT>` 支持数值 ID `O(1)` 索引、语义名称哈希查找、带代数（Generation）校验的强类型 `AssetHandle<AssetT>` 以及运行时热重载（`BumpGeneration`）。 |
| **微内核引擎宿主与插件架构** | `src/Engine/EngineApp.h`<br>`src/Engine/Engine.h` | Godot Server 架构 / Bevy & Unity 插件系统 | `Engine::EngineContext` 聚合 `JobSystem`、`AssetServer`、`RenderServer2D`、`AudioBusMixer`、`InputActionMap`、`SceneManager`、`SceneTree2D` 与 `TimeStepAccumulator`；`Engine::EngineApp` 提供 `AddPlugin<T>()`、`CreateWorld()`、自动回滚快照捕获以及 `StepFrame(dt)` 全生命周期调度。 |
| **统一音频事件路由系统** | `src/Core/Audio/ModernAudioEventRouter.h` | Godot `AudioServer` | `Core::Audio::ModernAudioEventRouterSystem` 同时订阅 `AnimAudioNotifyEvent` 与 `SoundRequestEvent`，统一通过 `AudioBusMixer`（Master / SFX / Voice / Music / UI 总线）做优先级裁剪、距离衰减与立体声声像计算。 |
| **星际争霸引擎插件挂载层** | `src/Game/StarCraftEnginePlugin.h` | 模块化游戏挂载层 | `Game::StarCraftEnginePlugin` 实现 `Engine::IPlugin`，将星际争霸的 `SystemPipeline`、确定性命令队列 `DeterministicCommandQueue` 与云端 `ModernAssetRegistry` 作为标准插件挂载到通用引擎底座 `Engine::EngineApp` 上。 |

## 三、 最小通用引擎接入示例

借助全新的 `src/Engine/Engine.h`，无论是运行《星际争霸》还是孵化全新的 2D/RTS 游戏，都可以直接使用统一的微内核插件接口启动：

```cpp
#include "Engine/Engine.h"
#include "Game/StarCraftEnginePlugin.h"

int main() {
  Engine::EngineApp App;
  App.ConfigureFixedStep(1.0f / 24.0f, 8);
  App.SetAutoRecordSnapshots(true);

  // 挂载 StarCraft 游戏插件（也可挂载任意自定义 IPlugin 游戏模块）
  auto& SCPlugin = App.AddPlugin<Game::StarCraftEnginePlugin>(/* LocalPlayerID = */ 0);
  (void)SCPlugin;
  App.Initialize();

  // 推进一帧（内部自动按固定步长调度 FixedUpdate、记录 GenericRollbackBuffer 快照并计算渲染插值 Alpha）
  App.StepFrame(1.0f / 60.0f);

  App.Shutdown();
  return 0;
}
```

## 四、 回归与引擎底座验证结果

新增 `tests/Regression/Phase31To36EngineFoundationAndSimplificationTest.cpp`，并与现有全部回归及现代化阶段测试套件一起完成了全量编译与执行验证（共 **38/38** 个测试套件全部通过）：

1. **Test 1 (`TimeStepAccumulator`)**：验证固定步长累加、渲染插值 `InterpolationAlpha` 与防死亡螺旋钳制。
2. **Test 2 (`Node2D` & `SceneTree2D`)**：验证父子层级变换传播、旋转复合与命名挂点（如 `Muzzle_Left`）世界坐标解算。
3. **Test 3 (`DataAssetTable<T>`)**：验证泛型数据资产表的 ID/名称双索引、句柄代数校验与热重载失效检测。
4. **Test 4 (`GenericWorldSnapshot` & `GenericRollbackBuffer`)**：验证零游戏依赖的类型擦除 ECS 快照捕获与回滚恢复（同时覆盖 Trivially Copyable 组件与非平凡容器组件）。
5. **Test 5 (`SystemAssetBridge` & Per-World State)**：验证云端 `ModernAssetRegistry` 一键桥接与多 `World` 独立 `BulletWorldStateResource` 零串扰。
6. **Test 6 (`EngineApp` + `StarCraftEnginePlugin`)**：验证星际争霸作为 `IPlugin` 挂载在 `Engine::EngineApp` 上完成多 Tick 确定性仿真与通用快照回滚。
