# Linux 源码适配（dev_linux）

此分支增加 Linux x86_64 原生构建入口，目标为 Ubuntu 24.04、GCC 13、
X11/XWayland 和 OpenGL 4.6。它是首轮移植，尚未完成 Linux 编译和图形验收，
不能视为与 Windows 功能等价的发行版。

## 构建

在仓库根目录执行。Linux 使用发行版的 Vulkan、ShaderC、SPIRV-Cross 开发包，
不使用仓库中的 `vendor/VulkanSDK` Windows 二进制库。

```bash
git submodule update --init --recursive \
  TomCat/vendor/GLFW TomCat/vendor/ImGui TomCat/vendor/glm \
  TomCat/vendor/spdlog TomCat/vendor/yaml-cpp TomCat/vendor/Box2D \
  TomCat/vendor/ImGuizmo

sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libgl1-mesa-dev libvulkan-dev libshaderc-dev glslang-dev glslang-tools \
  spirv-tools libspirv-cross-c-shared-dev zenity

bash Scripts/Linux_Build.sh
```

脚本完成 CMake 配置、构建和 CTest。默认使用两个编译进程以控制内存占用；
可设置 `TC_BUILD_JOBS=4`、`TC_BUILD_TYPE=Debug`、`TC_BUILD_DIR=/absolute/path`。
额外参数传给 CMake，例如 `bash Scripts/Linux_Build.sh -DBUILD_TESTING=OFF`。

```bash
TC_RENDERER=opengl ./build/linux/bin/Editor/TomCatInut Samples/PhysicsPlayground/Project.tcproj
TC_RENDERER=opengl ./build/linux/bin/Hub/Manager
./build/linux/bin/Player/TomCatPlayer --version
```

Editor/Hub 的 Packages 会复制到各自可执行文件旁边。Editor 的相对项目参数
先按启动目录解析，再切换到资源目录；Player 保留启动目录。
Hub 设置中的 Editor 搜索目录可指向 `build/linux/bin/Editor`。
日志、设置、存档使用 `$XDG_DATA_HOME/TomCat`，未设置或不是绝对路径时，
使用 `$HOME/.local/share/TomCat`。

## 本轮代码范围

- Linux 平台识别和调试断点；修正大小写敏感的头文件路径。
- GLFW 桌面窗口、键鼠输入与现有 OpenGL 渲染路径复用。
- UTF-8 程序入口、可执行文件定位、产品身份查询、用户数据目录。
- Zenity 文件/目录选择；直接传递进程参数，不经过 shell 解释文件名。
- Hub 的 Editor 扫描、启动和打开目录。
- 编辑器自动化服务的 POSIX socket 分支，仍仅绑定 loopback 并要求 token。
- 使用 `flock` 的项目排他锁；进程崩溃后内核释放锁，可再次打开项目。
  Linux 保留锁文件 inode，避免删除并重建锁文件造成并发双重持锁。

## 尚未支持或验收

- **C# 编译和 CoreCLR 托管仍为 Windows 实现**；含 C# 脚本的项目不能视为
  Linux 可运行项目，Play 与 Player 的托管启动链路尚需移植。
- 音频使用现有 NullAudioDevice，没有 Linux 硬件音频输出。
- Vulkan 自动探测仍仅实现 Windows，Linux 当前回退到 OpenGL；强制 Vulkan
  会明确失败。仍需系统 Vulkan 库用于链接共享渲染代码。
- Player 导出模板、私有 .NET 打包、CLI、Windows 单文件发布没有迁移。
  CMake 的 Player 目标不代表已能导出完整 Linux 游戏。
- 输入法预编辑、系统字体回退、崩溃转储、进程内存统计及外部 C# 编辑器启动
  尚未达到 Windows 功能对等。Wayland 原生后端和 ARM64 不在本轮范围。
- 实机窗口、资源、渲染、输入、文件对话框和自动化服务需在 Linux 桌面验收。
  无头测试通过也不代表图形功能通过。

## 验证入口

`.github/workflows/linux.yml` 在 Ubuntu 24.04 分别构建 Debug/Release，并运行：
InputRegression、SaveDataRegression、LinuxPlatformRegression、Player 版本查询、
Editor/Hub 产品身份查询。平台测试覆盖 XDG 路径、参数原样传递、启动失败、
子进程超时、锁互斥和进程退出后的锁恢复。

本地环境目前只有 Windows，没有 WSL；新增 Linux 测试和 CI 需要在 Linux
环境实际运行，不能将添加工作流等同于 CI 已通过。

本轮本地验证（2026-10-22）：Windows Release Editor、Hub、Player 构建通过；
InputRegression、SaveDataRegression、EditorRecoveryRegression 通过；
Bash 脚本语法、工作流 YAML 解析和 `git diff --check` 通过。
