# TomCat Engine 项目管理系统

## 概述

已成功实现类似 Unity Hub + Unity Editor 的项目管理系统，Builder 和 Editor 共用 TomCat 库。

## 架构设计

### 核心组件

1. **Project.tomcat** - 项目配置文件（YAML格式）
   - 项目名称、版本、作者、描述
   - 资源目录、场景目录、脚本目录配置
   - 启动场景设置

2. **Project 类** ([Project.h](file:///e:/Github/TomCat_Engine/TomCat/src/TomCat/Project/Project.h))
   - 项目数据结构
   - 项目创建、加载、保存
   - 路径管理

3. **ProjectManager 类** ([ProjectManager.h](file:///e:/Github/TomCat_Engine/TomCat/src/TomCat/Project/ProjectManager.h))
   - 单例模式的项目管理器
   - 项目扫描、创建、加载、删除
   - Builder 到 Editor 的项目传递

### Builder 功能（类似 Unity Hub）

- **项目列表显示**：显示所有已创建的项目
- **创建新项目**：通过对话框创建新项目
- **添加现有项目**：导入已存在的项目
- **删除项目**：从列表中移除项目
- **打开项目**：启动 Editor 并传递项目路径
- **设置管理**：配置项目目录等偏好设置

### Editor 功能（类似 Unity Editor）

- **项目加载**：从命令行参数或菜单加载项目
- **场景管理**：新建、打开、保存场景
- **项目信息**：显示当前项目信息
- **资源浏览器**：基于项目路径的资源管理
- **自动加载启动场景**：打开项目时自动加载配置的启动场景

## 使用方式

### 1. 创建新项目

1. 启动 Builder（Manager.exe）
2. 点击 "Projects" 标签
3. 点击 "New" 按钮
4. 填写项目信息（名称、作者、描述）
5. 选择项目位置
6. 点击 "Create"

### 2. 打开项目

**方式一：从 Builder**
1. 在 Builder 的项目列表中找到项目
2. 点击 "Open" 按钮
3. Editor 会自动启动并加载项目

**方式二：从 Editor**
1. 启动 Editor（TomCatInut.exe）
2. 菜单 File → New Project
3. 选择 Project.tomcat 文件

### 3. 项目文件结构

```
MyProject/
├── Project.tomcat          # 项目配置文件
├── Assets/                 # 资源目录
│   ├── Scenes/            # 场景文件
│   ├── Scripts/           # 脚本文件
│   └── ...
└── ...
```

### 4. Project.tomcat 示例

```yaml
Project:
  Name: "My Game"
  Version: "1.0.0"
  Description: "A sample game project"
  Author: "Developer Name"
  AssetDirectory: "Assets"
  SceneDirectory: "Assets/Scenes"
  ScriptDirectory: "Assets/Scripts"
  StartScene: "MainScene.tomcat"
```

## 技术实现

### 文件格式
- 使用 YAML 作为配置文件格式
- 通过 yaml-cpp 库进行序列化/反序列化

### 进程间通信
- Builder 通过 CreateProcess 启动 Editor
- 通过命令行参数传递项目路径

### 路径管理
- 使用 std::filesystem 进行跨平台路径处理
- 相对路径和绝对路径的自动转换

## 文件清单

### 新增文件
- [TomCat/src/TomCat/Project/Project.h](file:///e:/Github/TomCat_Engine/TomCat/src/TomCat/Project/Project.h)
- [TomCat/src/TomCat/Project/Project.cpp](file:///e:/Github/TomCat_Engine/TomCat/src/TomCat/Project/Project.cpp)
- [TomCat/src/TomCat/Project/ProjectManager.h](file:///e:/Github/TomCat_Engine/TomCat/src/TomCat/Project/ProjectManager.h)
- [TomCat/src/TomCat/Project/ProjectManager.cpp](file:///e:/Github/TomCat_Engine/TomCat/src/TomCat/Project/ProjectManager.cpp)

### 修改文件
- [Builder/Manager/src/ExampleLayer.h](file:///e:/Github/TomCat_Engine/Builder/Manager/src/ExampleLayer.h)
- [Builder/Manager/src/ExampleLayer.cpp](file:///e:/Github/TomCat_Engine/Builder/Manager/src/ExampleLayer.cpp)
- [Editor/TomCatInut/src/TomCatInputApp.cpp](file:///e:/Github/TomCat_Engine/Editor/TomCatInut/src/TomCatInputApp.cpp)
- [Editor/TomCatInut/src/EditorLayer.h](file:///e:/Github/TomCat_Engine/Editor/TomCatInut/src/EditorLayer.h)
- [Editor/TomCatInut/src/EditorLayer.cpp](file:///e:/Github/TomCat_Engine/Editor/TomCatInut/src/EditorLayer.cpp)
- [Editor/TomCatInut/src/panels/ContentBrowserPanel.h](file:///e:/Github/TomCat_Engine/Editor/TomCatInut/src/panels/ContentBrowserPanel.h)
- [Editor/TomCatInut/src/panels/ContentBrowserPanel.cpp](file:///e:/Github/TomCat_Engine/Editor/TomCatInut/src/panels/ContentBrowserPanel.cpp)
- [TomCat/src/TomCat/Utils/PlatformUtils.h](file:///e:/Github/TomCat_Engine/TomCat/src/TomCat/Utils/PlatformUtils.h)
- [TomCat/src/platform/Window/WindowsPlatformUtils.cpp](file:///e:/Github/TomCat_Engine/TomCat/src/platform/Window/WindowsPlatformUtils.cpp)

## 下一步建议

1. **项目模板**：支持不同类型的项目模板（2D、3D 等）
2. **版本控制集成**：与 Git 等版本控制系统集成
3. **项目设置**：更详细的项目配置选项
4. **资源导入**：自动资源导入和管线
5. **构建系统**：项目打包和发布功能
