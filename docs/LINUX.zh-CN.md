# Linux 原生移植（dev_linux）

当前目标为 Ubuntu 24.04、x86_64、GCC 13、X11/XWayland 和 OpenGL。
已接入 Editor、Hub、Player、CLI、C#/.NET 10、OpenAL Soft 音频及 Linux 游戏导出。
尚不承诺所有发行版、GPU 或桌面环境与 Windows 功能等价。

## 构建

先安装 .NET 10 SDK，使 `dotnet --list-sdks` 能列出 10.x SDK。
源码开发允许使用 `DOTNET_ROOT_X64`、`DOTNET_ROOT` 或 PATH 中的 SDK；
已指定打包 Editor 根目录时，只使用该目录的 `DotNetSdk/dotnet`。

```bash
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

ImGui、yaml-cpp 等已作为普通源码纳入仓库。Linux 使用系统 Vulkan、ShaderC、
SPIRV-Cross 开发库，不使用 `vendor/VulkanSDK` 的 Windows 库。
脚本编译原生程序和托管项目，生成 Player 模板，再运行 CTest。
默认两个编译进程；可设置 `TC_BUILD_JOBS`、`TC_BUILD_TYPE=Debug`、`TC_BUILD_DIR`。
额外参数传给 CMake。

```bash
TC_RENDERER=opengl ./build/linux/bin/Editor/TomCatInut /absolute/Project.tcproj
TC_RENDERER=opengl ./build/linux/bin/Hub/Manager
./build/linux/bin/CLI/TomCatCLI build --project /absolute/Project.tcproj \
  --template ./build/linux/bin/Editor/Packages/PlayerTemplates/linux-x64
```

Editor/Hub 的 Packages 和 Editor/CLI 的 Managed 自动复制到程序旁边。
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
在 Ubuntu 24.04 分别构建 Debug/Release。测试覆盖：

- InputRegression、SaveDataRegression 和产品版本/身份查询。
- LinuxPlatformRegression：XDG 路径、参数传递、子进程失败/超时、文件锁和崩溃释放。
- AudioRegression 与 LinuxAudioRegression：引擎音频逻辑及 OpenAL 的实际 PCM 输出。
- LinuxRuntimeRegression：中文/空格项目路径、C# 编译和元数据、MSBuild 隔离、失败
  构建恢复、CLI 导出；从无 SDK、源文件被移走的环境验证私有 .NET，执行 C#
  Awake/Update 与原生 Entity API，再在 Xvfb/Mesa 下运行带窗口的 Player。

成功后工作流上传 `linux-player-Debug/Release`：tar.gz 保留执行权限，内含本轮
验收游戏。下载解压后运行其中的 `Linux Acceptance`；该游戏执行三帧脚本后退出。
CTest 日志也作为 artifact 上传。

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
