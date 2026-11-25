# HarmonyOS 节奏游戏兼容层

[English](README.md) | [简体中文](README.zh-CN.md)

这是一个开源的 HarmonyOS 节奏游戏兼容层与原型工程，包含独立维护的谱面解析、
播放时间轴、渲染和平台集成代码。

本仓库**不包含** Arcaea 游戏安装包、音乐、谱面、曲绘、角色、字体、模型、
提取资源、账号数据或解锁机制，也不授予这些内容的任何权利。Arcaea 及相关名称
归其各自权利人所有。本项目与 lowiro 无关联，也未获得其认可或赞助。

## 功能范围

- AFF 谱面解析与播放时间轴
- 音符判定与 Recollection Rate 计算
- ArkUI 选曲、游玩与结算界面
- HarmonyOS NAPI、OpenGL 和音频集成
- 版本描述规范与兼容性文档
- 导入使用者依法有权使用资源的工具

## 仓库边界

以下内容禁止提交到仓库：

- APK、HAP、ZIP 或提取后的游戏资源包
- 官方音频、谱面、曲绘、字体、模型或其他游戏资源
- 第三方专有原生库或反编译源码
- 存档、账号标识、设备标识或崩溃转储
- 绕过授权、防篡改检查或付费解锁的代码

运行任何导入工具前，请先阅读 [docs/ASSETS.md](docs/ASSETS.md)。

## 环境要求

- 安装 HarmonyOS SDK API 26 的 DevEco Studio
- DevEco Studio 自带的 Node.js
- Hvigor 6.26.x
- 用于运行测试的 arm64 HarmonyOS 真机或模拟器

## 构建

使用 DevEco Studio 打开仓库并安装本地 SDK 依赖。在
`File > Project Structure > Signing Configs` 中配置你自己的本地调试签名；
仓库不会提供任何签名密钥。然后运行：

```powershell
.\build.ps1
```

如果 DevEco Studio 不位于 `D:\DevEco Studio`，请设置 `DEVECO_HOME`。

## 可选资源导入

`tools/build_library.ps1` 可以从一个你有权使用的资源目录生成谱面和元数据。
该工具没有内置资源路径，必须通过 `-SongsRoot` 指定输入目录。

`tools/make_audio_zip.ps1` 可以把使用者自行拥有的音频打包为独立导入 ZIP。
生成的 ZIP 已被 Git 忽略。

仓库默认只包含空曲库清单，因此不会在公开源码中附带第三方内容。

## 版本管理

兼容信息分为三层：

1. Version Profile：版本号、ABI、依赖和资源版本信息。
2. Analysis Profile：仅用于本地的版本差异、候选位置和验证记录。
3. Compatibility Logic：在实际源码和测试中实现兼容行为。

详见 [docs/VERSIONING.md](docs/VERSIONING.md) 和
[profiles/schema/version-profile.schema.json](profiles/schema/version-profile.schema.json)。

## 参与贡献

贡献代码需接受 Apache License 2.0，并添加 Developer Certificate of Origin
签署。请阅读 [CONTRIBUTING.md](CONTRIBUTING.md) 和 [SECURITY.md](SECURITY.md)。

提交前请运行：

```powershell
.\tools\check-open-source-boundary.ps1
```

## 许可证

本仓库中的原创源码采用 Apache License 2.0。第三方名称、游戏内容和商标
不包含在该许可证的授权范围内。
