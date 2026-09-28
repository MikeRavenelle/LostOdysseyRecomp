# FreeSync / G-SYNC Compatible：应用侧VRR接入

日期：2026-09-28。开发分支：`feature/native-90-120fps`。

## 使用与边界

Graphics中新增`FreeSync / G-SYNC Compatible`开关；默认Off，选择On后保存，运行中生效，无需重启。配置项为`variable_refresh_rate=0/1`。旧配置没有此项时保留原行为。选项控制游戏的呈现与限帧策略，不会代替用户开启显示器或驱动的VRR，也不会宣称检测到显示器正在变频。

先在显示器菜单启用相应自适应同步功能，并在显卡驱动中启用FreeSync或G-SYNC Compatible。NVIDIA的窗口化使用还需匹配控制面板的“窗口和全屏”范围。首次验证建议使用无边框全屏、FG Off。非VRR屏幕或不支持的窗口系统开启此项可能出现撕裂；关闭该选项可恢复本分支原有同步策略（高于60的原生模式仍使用原有immediate策略）。

## 实现

`gpu/vrr_policy.h`统一宿主VSync请求、原生限帧和FG输出预算。普通游戏swap经`video::GetFramePacingTarget`得到有效宿主上限，保存的30/60/90/120目标不变。`PreparePresentation`在acquire前应用异步呈现请求，继续使用既有交换链重建、在途提交等待及FG资源交接流程。暂停时的host overlay单独使用不含FG倍数的限帧。

已识别游戏Present调用点`0x827B4A4C`的interval=2在VRR On时改为immediate，包括30/60档，避免57fps这类上限被60Hz整数VBlank量化。其他调用者、其他interval及标志位保持不变。PPC时基、内核/音频时钟、engine delta和60Hz虚拟VBlank均不缩放。

刷新率由窗口线程读取并通过原子快照交给呈现线程：Windows使用窗口所在显示器的当前显示模式，其他平台通过SDL2当前显示模式查询。每500ms刷新，并在移动窗口、显示器/尺寸变化和重新获得焦点后请求重查。渲染线程不访问SDL窗口；无有效刷新率时记录0，保留既有目标，不猜测为60Hz。这是当前模式刷新率，**不是显示器VRR范围或实际扫描频率检测**。

D3D12复用Plume已有的flip-discard、能力查询、创建/ResizeBuffers时的`ALLOW_TEARING`。此次补上Present时的实际全屏状态检查：只在窗口/无边框、允许tearing且SyncInterval=0时传入`DXGI_PRESENT_ALLOW_TEARING`，独占全屏不传该标志。Vulkan沿用Plume的异步模式选择；不支持immediate时接受现有后端回退并发出警告，不循环重建交换链。窗口系统或驱动仍可限制呈现节奏。

## 限帧与FG

本项目采用当前模式刷新率减3的输出预算，为整数刷新率报告、分数刷新率和调度误差留余量。这是工程策略，不是AMD/NVIDIA规定，也不是绝对不越界保证。

| 配置 | 有效宿主上限 |
|---|---:|
| 90fps，90Hz，FG Off | 87fps |
| 120fps，120Hz，FG Off | 117fps |
| 120fps，144Hz，FG Off | 120fps |
| 120fps，144Hz，固定2×FG | 70fps，名义总输出140fps |
| 120fps，144Hz，固定4×FG | 35fps，名义总输出140fps |

固定FG按包含原始帧的倍数分配预算，D3D12使用已应用且运行时支持的配置，Vulkan沿用当前固定2×会话。D3D12在FG已就绪但场景临时不能插帧时仍保守预留倍数预算，因此此类场景原生帧率也可能降低。Vulkan根据上一帧的会话可用性决定倍数；模式/场景过渡可能短暂采用上一次预算，不构成每帧物理显示上限保证。

动态DLSS MFG将有效SDK目标限制在输出预算以内，0（自动目标）变为该预算；较低显式目标保留。原生上限也不高于该有效动态目标，但不按最大MFG倍数机械相除。切屏、刷新率变化、VRR On/Off经既有FG reconfigure更新有效请求，磁盘中的FG偏好不被覆盖。SDK请求失败沿用原有失败/回退路径，不假装支持。

`LO_FPS=0`仍保持诊断性不限帧，会绕过VRR自动上限。普通验收应清除`LO_FPS`、`LO_GUEST_INTERVAL=0`和非必要FG环境覆盖。游戏帧率不足时不会用修改时钟补偿，显示器低帧率补偿由外部显示系统处理。

## 日志与验证

日志`VRR pacing`记录`requested`、`refresh_hz`、`native_target`、`host_cap`、`fg_multiplier`和`dynamic`。每秒至多报告一次变化；`hardware_vrr=unverified`始终明确真实VRR没有被检测。`native presentation`另记录请求/报告的VSync状态；`frame timing`中的target是有效宿主限帧值，不能当成显示器实际扫描率或插帧后FPS。

本次本地通过：三项独立CPU测试（VRR策略、真实配置解析/写入与preview隔离、FG请求选择）；现有菜单交互fixture的380项检查；菜单光栅fixture的21项检查；原生frame-pacer测试。菜单/配置fixture隔离平台和游戏服务，不代表运行了真实游戏。Plume补丁通过固定上游源码的apply检查。

```sh
cmake -S tools/tests/vrr -B out/vrr-tests
cmake --build out/vrr-tests --config Release
ctest --test-dir out/vrr-tests -C Release --output-on-failure
```

完整游戏构建、真实D3D12/Vulkan呈现、显示器实际VRR和FG组合仍由本地实机验收。先对同一场景FG Off测试VRR On/Off、60/90/120档和设置重新打开；再测固定FG/动态MFG、跨显示器移动、Alt+Enter、最小化/恢复与暂停设置。通过显示器/驱动指示器确认实际VRR，并比较等现实时间行走、Aim Ring及音画同步。本提交不宣称AMD或NVIDIA认证、稳定达到目标FPS或已完成硬件验收。

## 对照接口资料

- [Microsoft：Variable refresh rate displays](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays)
- [Microsoft：DXGI_PRESENT，独占全屏与ALLOW_TEARING限制](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-present)
- [SDL2：SDL_GetCurrentDisplayMode](https://wiki.libsdl.org/SDL2/SDL_GetCurrentDisplayMode)
- [Khronos：VkPresentModeKHR](https://docs.vulkan.org/refpages/latest/refpages/source/VkPresentModeKHR.html)
- [AMD：Enable AMD FreeSync](https://www.amd.com/en/resources/support-articles/faqs/DH3-013.html)
- [NVIDIA：Set up G-SYNC](https://www.nvidia.com/content/Control-Panel-Help/vLatest/en-us/mergedProjects/nvdsp/To_use_variable_refresh_rates.htm)
