# Runtime UI 控件与文本

在 Canvas 下创建实体，使用 Inspector 的 **Add Component → UI** 添加控件。Canvas 需要启用 UIEventSystem。所有新增组件通过 ComponentRegistry 保存到 Scene/Prefab，并可由 C# 的 `entity.GetComponent<T>()` / `AddComponent<T>()` 访问；主题和语言切换直接修改对应组件属性。

| 组件 | 使用方式 |
| --- | --- |
| UISlider | 自绘轨道和填充；设置 Minimum、Maximum、Value、Step。鼠标按下后拖出边界仍保持捕获并限制范围。聚焦时方向键/手柄方向键修改数值，WholeNumbers 使用整数。 |
| UIScrollView | RectTransform 是视口，ContentSize 是 Canvas 参考像素下的内容尺寸。Offset 是从左上角起的滚动距离。滚轮滚动并限制到内容范围；子内容会自动裁剪。与 UILayoutGroup 组合成滚动列表；运行时增删普通子实体即可更新列表内容。 |
| UIInputField | 点击或 Tab 聚焦。支持平台提交的 Unicode 文本、左右/Home/End 光标、Shift 选区、Ctrl+A/C/X/V、Backspace/Delete 和按键重复、密码显示、只读和 Unicode 字符数限制。密码字段禁止复制/剪切，只读字段禁止修改；粘贴同样经过单行过滤和字符限制。Escape 或窗口失焦释放输入；编辑中屏蔽游戏输入。UIText 可配置字体/大小/颜色。 |
| UITheme | 作用于自身和子层级，最近的启用主题优先。ImageColor/TextColor 乘以已有颜色，FontScale 缩放字号，Font 非零时替换字体，AccentColor 控制滑条填充。 |
| UILocalization | 在 Canvas 或子树根提供 Locale、FallbackLocale 和 Table。Table 接受 YAML 或 JSON 的“语言 → 文本键 → 文本”字典，最多 65536 UTF-8 字节；重复语言/文本键会被拒绝。 |
| UILocalizedText | 与 UIText 同实体，Key 选择语言表中的文本。切换语言后下一次渲染生效；当前语言缺键时查 FallbackLocale，仍缺失时显示 UIText.Text。 |

例如 `UILocalization.Table`：

```yaml
en:
  menu.play: Play
  menu.exit: Exit
zh-CN:
  menu.play: 开始游戏
  menu.exit: 退出
```

```csharp
var localization = canvas.GetComponent<UILocalization>();
localization.Locale = "zh-CN";
var volume = volumeEntity.GetComponent<UISlider>();
float currentVolume = volume.Value;
var name = nameEntity.GetComponent<UIInputField>().Text;
```

Tab/Shift+Tab 按布局顺序在按钮、输入框、滑条间切换。顶层可射线命中的 UI 会阻止点击穿透；滚轮在嵌套滚动视图中优先由内层消费，到达边缘后可交给外层。

当前范围：输入框为单行控件，Unicode 编辑按码点边界处理。Windows GLFW 字符回调接收操作系统输入法**提交后的文字**，没有引擎内预编辑文本、候选窗光标定位、字素簇编辑或双向文字排版；这不等同于完整 IME 组合态支持。滚动列表使用普通实体，尚未虚拟化，也不包含触摸惯性和拖动滚动条。本地化表提供语言回退，不执行复数规则或格式化参数。主题是层级组件，不是独立主题资源资产。

回归覆盖在 `Tests/PhysicsRegression/src/RuntimeUIRegression.cpp` 的 `TestProductUIControls`：滑条捕获与限制、Tab 导航、中文/emoji 输入与删除、剪贴板/密码/只读规则、重复帧提交保护、失焦、滚动裁剪、语言回退、Scene 保存加载及临时状态重置、平台字符快照。OpenGL 像素回归还验证普通/密码输入的选区宽度、96/192 DPI、水平滚动和自身/祖先裁剪。真实输入法候选交互仍需实机验收。
