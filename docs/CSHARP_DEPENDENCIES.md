# C# 项目依赖

将 [TomCat.Dependencies.csproj 模板](../Managed/Templates/TomCat.Dependencies.csproj)复制到游戏项目根目录，与 `.tcproj` 同级。它是可由 `dotnet` 管理的标准 SDK 项目，不会被编辑器覆盖。不要修改 `Library/ScriptProject` 中的生成文件。

在该文件中添加依赖，路径相对这个 `.csproj`：

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
  </PropertyGroup>
  <ItemGroup>
    <!-- 用实际包 ID 和固定版本替换示例 -->
    <PackageReference Include="Your.Package" Version="1.2.3" />
    <ProjectReference Include="Dependencies/GameLogic/GameLogic.csproj" />
    <Reference Include="YourLibrary">
      <HintPath>Dependencies/YourLibrary.dll</HintPath>
      <Private>true</Private>
    </Reference>
  </ItemGroup>
</Project>
```

`EnableDefaultCompileItems=false` 防止项目根目录下的游戏脚本被重复编译。公共库源码放到单独的 `ProjectReference` 项目，或者用显式 `Compile Include` 加入依赖工程。游戏脚本仍放在 `Assets` 中。

停止 Play 后，编辑器会通过 `dotnet build` 自动还原并编译依赖。也可以在项目根目录运行 `dotnet restore TomCat.Dependencies.csproj` 检查还原结果。包源、认证和缓存沿用 NuGet 配置，可在项目根目录放置 `NuGet.Config`。PackageReference 的[还原与传递依赖规则](https://learn.microsoft.com/en-us/nuget/concepts/dependency-resolution)由 NuGet 处理。

成功构建时，运行所需的托管 DLL 被嵌入 `Assembly-CSharp.dll`。编辑器 Metadata / Play 域分别加载各自的依赖副本；停止后重编译不会复用上一版依赖。Cook 和 Player 携带同一个程序集，不需要玩家机器上的 NuGet 缓存、源码项目或旁置的第三方 DLL。`TomCat.Managed` 仍使用引擎共享实例，避免组件类型身份冲突。

项目内 `.csproj`、`.props`、`.targets`、`.cs`、本地 DLL、`.nupkg`、NuGet 配置和锁文件的变化会使脚本构建失效。`Library`、`bin`、`obj`、`Builds` 和隐藏目录不参与监听。引用项目外部文件或这些输出目录中的 DLL 时，更新后使用 **Assets → Compile C# Scripts** 显式重编译。建议固定包版本；远程浮动版本更新不会触发本地文件监听。

还原、编译或程序集验证失败时，Console 保留具体诊断，最后成功程序集不被替换。修正依赖声明后会再次编译。

## 支持范围

- Windows x64、.NET 10、纯托管 NuGet 包、传递包依赖、C# 项目引用和本地托管 DLL。
- 依赖跟随可卸载脚本域加载；依赖 DLL 的磁盘路径不作为运行时探测目录。依赖代码如果要求 `Assembly.Location` 非空或自行查找旁置文件，需要改用程序集资源。
- 原生库、包内容文件不在本次范围内，构建会对检测到的对应资产报告 `TCSP0023` / `TCSP0024`。不能据此宣称所有 NuGet 包均兼容。
- 通过项目显式引入的包可以使用其还原生成的 MSBuild props/targets。隐式 `Directory.Build.*`、集中版本 `Directory.Packages.props` 和用户目录的通用构建钩子仍关闭；版本直接写在 `PackageReference` 中。
- 不提供内置 NuGet 管理界面，也不执行 PlayMode 热替换。

自动回归使用本地离线包源，覆盖还原、传递依赖、项目引用、本地 DLL、依赖更新、独立程序集加载及还原失败保留旧版本；运行时回归还会从 Cook 包中加载并调用封装的依赖。
