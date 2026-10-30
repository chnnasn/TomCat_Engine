# Linux 构建、运行与导出

Linux 初步支持已于 2026-10-22 通过 [9a9a303a](https://github.com/chnnasn/TomCat_Engine/commit/9a9a303a56893d50eb02a6280603324a6741f9cc) 合入 `main`；新开发和构建使用当前主线。`dev_linux` 保留原开发历史，提交移植关系见[集成记录](PROJECT_HISTORY.zh-CN.md#linux-合入主线2026-10-22)。

当前目标为 Ubuntu 24.04、x86_64、GCC 13、X11/XWayland 和 OpenGL 4.5+。
已接入 Editor、Hub、Player、CLI、C#/.NET 10、OpenAL Soft 音频及 Linux 游戏导出。
尚不承诺所有发行版、GPU 或桌面环境与 Windows 功能等价。

## 构建

先安装 .NET 10 SDK，使 `dotnet --list-sdks` 能列出 10.x SDK。
源码开发允许使用 `DOTNET_ROOT_X64`、`DOTNET_ROOT` 或 PATH 中的 SDK；
已指定打包 Editor 根目录时，只使用该目录的 `DotNetSdk/dotnet`。

```bash
# 已检出 main；以下命令在仓库根目录执行
git submodule update --init --recursive \
  TomCat/vendor/GLFW TomCat/vendor/glm TomCat/vendor/spdlog \
  TomCat/vendor/Box2D TomCat/vendor/ImGuizmo
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config python3 \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libgl1-mesa-dev libvulkan-dev libshaderc-dev glslang-dev glslang-tools \
  spirv-tools libspirv-cross-c-shared-dev libopenal-dev zenity xvfb
bash Scripts/Linux_Build.sh
```

ImGui、yaml-cpp 等已作为普通源码纳入仓库。Linux 使用系统 Vulkan、ShaderC 库，CMake 下载并校验固定版本的
SPIRV-Cross 源码，不使用 `vendor/VulkanSDK` 的 Windows 库。
脚本编译原生程序和托管项目，生成 Player 模板，再运行 CTest。
默认两个编译进程；可设置 `TC_BUILD_JOBS`、`TC_BUILD_TYPE=Debug`、`TC_BUILD_DIR`。
额外参数传给 CMake。

Linux 创建 OpenGL 4.5 核心上下文。驱动不提供原生 SPIR-V specialization 时，
通过 SPIRV-Cross 转为 GLSL 450 后编译；CI 已在 Mesa llvmpipe GL 4.5 下运行带窗口的 Player。

```bash
TC_RENDERER=opengl ./build/linux/bin/Editor/TomCatInut /absolute/Project.tcproj
TC_RENDERER=opengl ./build/linux/bin/Hub/Manager
./build/linux/bin/CLI/TomCatCLI build --project /absolute/Project.tcproj \
  --template ./build/linux/bin/Editor/Packages/PlayerTemplates/linux-x64
```

Editor/Hub 的 Packages 和 Editor/CLI 的 Managed 自动复制到程序旁边。
Linux 构建还把仓库的 `Web/fonts/NotoSansSC-Regular.otf` 与 OFL `LICENSE`
复制到各程序的 `Packages/fonts/noto/`。Hub 与 Editor 使用这份字体补充中文及符号，
不依赖 `C:/Windows/Fonts` 或用户安装的中文语言包。分发程序时须保留整个 `Packages` 目录。

如果旧版 Hub 的中文显示为问号，更新到包含字体修复的源码后，在已有构建目录执行：

```bash
cmake --build build/linux --target Manager --parallel 2
TC_RENDERER=opengl ./build/linux/bin/Hub/Manager
```

单独构建 `Manager` 也会补齐字体和许可证；仅安装系统中文字体不能修复旧程序的 Windows 字体路径。
窗口图标在 Linux 直接解析 ICO 内的 PNG 或未压缩 32 位 DIB 帧，再将 RGBA 图像交给 GLFW；
Hub 的多尺寸 `HubLogo.ico` 与 Editor 的 `Logo.ico` 无需转换，原有 PNG 图标也可使用。
Editor 菜单导出默认选择旁边的 `Packages/PlayerTemplates/linux-x64`。
CLI 也支持 `cook --project ... --output ...`，导出默认编译 Production C#。
模板可单独重建：

```bash
python3 Scripts/Build-LinuxPlayerTemplate.py --build-dir build/linux
```

导出结果位于项目 `Build/<ProductName>/`。其中包括无 `.exe` 后缀的 ELF 程序、
`Game.tcpak`、Managed 和私有 `dotnet` 运行时，不需要目标机器安装 .NET SDK。
模板严格检查文件列表、SHA-256、版本、软链接及 ELF 架构，并保留执行权限。
运行时缺失会明确报错，不回退到机器上的其他 .NET。
这不是完全静态的发行包：目标系统仍需兼容的 glibc/libstdc++、OpenGL/Vulkan、
X11 与 OpenAL 系统库及 .NET 的系统依赖。优先在 Ubuntu 24.04 x86_64 验收。

## C# 与音频

Linux 通过 `dlopen(libhostfxr.so)` 托管 CoreCLR，保持 Native/Managed API 校验。
脚本编译使用直接 argv 和有超时的子进程，不通过 shell；保留 MSBuild 隔离属性、
失败构建保留 last-good、编译后元数据验证和导出前源码一致性校验。

默认音频后端为 OpenAL Soft，使用系统默认音频设备。支持 PCM/OGG 解码后的驻留
播放、有界队列流式播放、暂停/停止/重播、循环、音量、音调和空间声像；立体声
保留左右声道。设备不可用时降级为 NullAudioDevice，并报告原因。
CI 使用同一后端的 [OpenAL 回环扩展](https://openal-soft.org/openal-extensions/SOFT_loopback.txt)
读取实际混音样本，验证声道、播放状态和流式队列。回环通过不代表扬声器已实测。

## CI 与验收

[Linux native 工作流](https://github.com/chnnasn/TomCat_Engine/actions/workflows/linux.yml)
由 `main` / `dev_linux` push、PR 或手动触发，在 Ubuntu 24.04 分别构建 Debug/Release，每种配置运行 14 项 CTest。测试覆盖：

- InputRegression、SaveDataRegression 和产品版本/身份查询。
- LinuxPlatformRegression：XDG 路径、参数传递、子进程失败/超时、文件锁和崩溃释放。
- AudioRegression 与 LinuxAudioRegression：引擎音频逻辑及 OpenAL 的实际 PCM 输出。
- ManagerChineseFonts / TomCatInutChineseFonts：从打包后的程序目录构建真实字体图集，检查 Hub 源码中的中文在粗体/常规字体、Hub/Editor 字号和 DPI 重建后均有实际字形，防止静默回退成问号。
- LinuxWindowIcons：读取打包后的 Hub/Editor ICO，校验多尺寸图像、像素和透明度、无效输入处理；在 Xvfb/Mesa 下创建窗口并逐像素核对 X11 `_NET_WM_ICON` 属性。
- LinuxRuntimeRegression：中文/空格项目路径、C# 编译和元数据、MSBuild 隔离、失败
  构建恢复、CLI 导出；从无 SDK、源文件被移走的环境验证私有 .NET，执行 C#
  Awake/Update 与原生 Entity API，再在 Xvfb/Mesa 下运行带窗口的 Player。

2026-10-22 已核验的移植基线为 `2b6d2308`：[Debug job](https://github.com/chnnasn/TomCat_Engine/actions/runs/39789173074/job/119860283533)
和 [Release job](https://github.com/chnnasn/TomCat_Engine/actions/runs/39791001982/job/119866694708)
分别记录 11/11 项通过。两项来自不同 workflow 运行，整轮运行最终均取消；这里记录的是已完成 job 的 CTest 结果。
主线 `9a9a303a` 保留相同 Linux 补丁，其[合并后 Linux CI](https://github.com/chnnasn/TomCat_Engine/actions/runs/39818207868)
结果独立查询。跨平台验收汇总见[项目状态](PROJECT_STATUS.zh-CN.md#验收记录)。

每个配置成功完成测试后上传 `linux-player-Debug` 或 `linux-player-Release`：其中的 tar.gz 保留执行权限，内含本轮验收游戏。CTest 日志单独上传到 `linux-test-logs-Debug/Release`。
在 Actions 运行页面下载并解开 artifact 的 ZIP，再解开其中的 tar.gz：

```bash
tar -xzf linux-player-Release.tar.gz
TC_RENDERER=opengl './acceptance-game/Linux Acceptance'
```

该验收游戏执行三帧脚本后退出；它用于检查导出与启动链路，不是完整 Editor 发行包。

Linux 桌面验收仍需检查 Editor 操作、真实 GPU/显示器、键鼠输入、Zenity 对话框、
扬声器/耳机、设备断开重连，以及 PulseAudio/PipeWire/ALSA 的实际路由。
用户数据使用 `$XDG_DATA_HOME/TomCat`，无绝对 XDG 路径时回退到
`$HOME/.local/share/TomCat`。Hub 的 Editor 搜索目录可指向 `build/linux/bin/Editor`。

## 边界

- Vulkan 自动探测尚未完成 Linux 适配，当前桌面渲染使用 OpenGL。
- Wayland 原生后端、ARM64、Windows 单文件发布系统不在本轮范围。
- 输入法预编辑、系统字体回退、崩溃转储、外部 C# 编辑器启动和整套 Editor
  发行包还需要专门适配；原生模块的 Windows DLL 包不能直接在 Linux 使用。
- GitHub runner 的图形/音频验收不能代替真实桌面和硬件的兼容性测试。
