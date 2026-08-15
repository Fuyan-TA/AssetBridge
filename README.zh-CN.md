# AssetBridge

[English documentation / 英文说明](README.md)

AssetBridge 是一个轻量级 Windows 原生工具，用于检查 3D 资产并执行边界明确、
经过验证的 OBJ→GLB 工作流。项目重点不是宣称格式万能互转，而是显式描述格式能力、
转换前诊断、事务式输出和导出后回读验证。

AssetBridge v0.3.0 不是通用全格式转换器，也不对已验证边界之外的场景作可用性声明。
产品只开放已经具备自动测试和本机验收证据的 OBJ→GLB 路线。

![AssetBridge v0.1.0 桌面转换界面](docs/images/assetbridge-v0.1.0.png)

文档截图中的本地文件系统路径已作隐藏处理。

## v0.3.0 批处理工作流

AssetBridge v0.3.0 新增顺序批处理编排，但没有扩大已验证转换路线。桌面拖放和 Windows
原生文件选择器可一次添加多个 OBJ；每个任务独立检查和预检，并在队列中显示状态。
**Convert All to GLB** 一次只处理一个任务；某个任务失败或暂未支持，不会阻断后续
任务。**Cancel After Current** 会让当前事务完成，再把剩余排队任务标记为 canceled。

CLI 使用同一个协调器和稳定 JSON Schema：

```powershell
assetbridge-cli batch --input .\a.obj --input .\b.obj `
  --to glb --output .\converted --json
```

输出根目录包含 `batch-report.json`；每个成功任务仍使用原有的不覆盖资产目录和
`conversion-report.json`。批处理没有开放其他输入/输出格式、贴图语义、层级模式或
`--allow-lossy`。

## v0.3.0 已验证范围

| 范围 | v0.3.0 产品承诺 |
|---|---|
| 平台 | Windows x64 原生桌面程序和命令行程序。 |
| 路线 | 仅 OBJ/MTL 输入到二进制 GLB2 输出。 |
| 批处理 | 最多顺序处理 256 个规范路径互不重复的 OBJ，逐任务隔离并支持 `Cancel After Current`。 |
| 几何 | 单静态 Mesh 或多个非空扁平静态 Mesh；对非三角 Face 受控三角化。 |
| 属性 | 源资产存在时保留顶点位置、法线和 UV0。 |
| 材质 | 多个被 Mesh 引用的 MTL 材质及逐材质 Kd 颜色。 |
| 图片 | 每个被引用材质允许一张 Base Color `map_Kd` PNG/JPEG/JPG；原始压缩字节嵌入 GLB BIN `bufferView`。 |
| 去重 | 多个材质共享同一源图片时只嵌入一次。 |
| 路径 | Windows UTF-16 输入/输出边界与 UTF-8 JSON，支持中文和空格路径。 |
| 安全 | 资产根目录约束、事务输出、GLB 结构检查、Assimp 回读和提交前验证。 |

最终 GLB 不依赖外部图片。MTL 和图片伴随文件必须解析到 OBJ 资产根目录树内。
绝对路径、UNC、URL、Data URI、规范化后的 `../` 逃逸、重解析点逃逸、文件缺失或
非普通文件、扩展名与文件头不匹配、不支持语义以及资源上限违规都会阻断转换，并
输出稳定诊断。

以下内容仍在 v0.3.0 已验证边界之外，检测到时会拒绝转换：

- Alpha 与透明语义：`d`、`Tr`、`map_d`。
- 法线贴图和凹凸贴图。
- 金属度、粗糙度、高光、自发光和不透明度贴图。
- `map_Kd` 变换、选项和 clamp 语义。
- 图片转码或重采样。
- meaningful hierarchy、非单位节点变换或 Mesh 实例化。
- 多 UV、顶点色、切线或未经验证的 PBR 数据。
- 骨骼、蒙皮权重、动画或 Morph Target。
- 其他输入输出格式和 `--allow-lossy`。

## 快速开始

### 桌面程序

1. 完整解压 Windows x64 ZIP，保持 EXE 与 DLL 位于同一目录。
2. 运行 `assetbridge-desktop.exe`。
3. 将一个或多个 OBJ 拖入窗口，或点击 **Choose OBJ Files**。
4. 查看每个队列项的统计、preflight 结果和诊断。
5. 移除不需要的任务，选择共享输出根目录，点击 **Convert All to GLB**。
6. 查看 success/not-supported/failed/canceled 汇总和根级批报告。

队列最多接受 256 个规范路径互不重复的 OBJ。转换期间队列不可修改。单 OBJ 仍走
同一条路径，只是它是一项任务的批次。

### CLI

```powershell
assetbridge-cli --version
assetbridge-cli inspect .\model.obj
assetbridge-cli inspect .\model.obj --json
assetbridge-cli preflight .\model.obj --target glb --json
assetbridge-cli convert .\model.obj --to glb --output .\converted --json
assetbridge-cli batch --input .\a.obj --input .\b.obj --to glb --output .\converted --json
assetbridge-cli capabilities --json
```

`capabilities` 分离三类事实：当前 Assimp 运行时提供什么、AssetBridge 产品开放什么、
AssetBridge 已用测试验证什么。运行时可用不等于产品支持声明。

成功转换会创建不覆盖已有内容的独立资产目录：

```text
<output-root>/
└── <source-stem>/
    ├── <source-stem>.glb
    └── conversion-report.json
```

如果最终目录已存在，AssetBridge 会选择 `<source-stem>_2`、`_3` 等名称。导出和
验证在同级临时目录中完成，只有全部验证通过后才出现最终目录。

批处理在根目录增加一个报告，同时保留逐资产事务边界：

```text
<output-root>/
├── batch-report.json
├── a/
│   ├── a.glb
│   └── conversion-report.json
└── b/
    ├── b.glb
    └── conversion-report.json
```

CLI 退出码保持稳定：

| 退出码 | 含义 |
|---:|---|
| 0 | 命令成功完成。 |
| 1 | 文件、导入、预检、导出或验证错误。 |
| 2 | 命令、参数或目标格式无效。 |

`batch` 命令中，退出码 `0` 表示所有任务成功；`1` 表示部分/全部任务失败或取消；
`2` 表示参数错误或没有有效 OBJ 输入。

## 从源码构建

依赖：

- Windows 11 或兼容的 Windows x64 开发环境。
- Visual Studio 2022 Build Tools、MSVC v143 和 Windows SDK。
- CMake 3.25 或更高版本，以及 Ninja。
- vcpkg，并将 `VCPKG_ROOT` 设置为其安装目录。

在 x64 Visual Studio Developer PowerShell 中执行：

```powershell
$env:VCPKG_ROOT = "<vcpkg-root>"

cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug

cmake --preset msvc-release
cmake --build --preset msvc-release
ctest --preset msvc-release
```

生成便携 Release 包及 SHA-256 文件：

```powershell
cmake --build --preset msvc-release --target package
```

输出位于 `out/build/msvc-release/packages/`。CMake 项目版本 `0.3.0` 是权威版本源，
configure 阶段生成的版本头同时供 CLI 与桌面 UI 使用。

## 验证模型

转换路径依次检查原始资产、执行产品 preflight、生成并三角化 export-ready scene、
导出 GLB、重新导入并比较，最后才提交输出。验证 Mesh 数、三角形数、索引有效性、
场景整体和逐 Mesh AABB、法线/UV0存在性及当前已验证材质数据。对于纹理资产，
还会验证 GLB JSON/BIN 结构、图片 `bufferView` 边界、材质/纹理绑定、图片文件头、
不存在外部图片 URI，以及源压缩图片字节被精确保留。顶点数只作为诊断，因为合法
三角化和格式表达可能改变顶点拆分方式。

工程细节见[架构说明](docs/ARCHITECTURE.md)和
[验证流水线](docs/VALIDATION_PIPELINE.md)。

## 轻量指标

以下是实测值，不是跨机器保证。测量没有清理系统缓存，也没有修改 Windows 安全策略。

| 指标 | v0.2.0 记录值 | v0.3.0 记录值 | 相对 v0.2.0 变化 |
|---|---:|---:|---:|
| Desktop EXE | 865,792 bytes | 944,128 bytes | +78,336 bytes |
| CLI EXE | 496,640 bytes | 561,664 bytes | +65,024 bytes |
| DLL 数量 | 11 | 11 | 0 |
| 便携目录 | 10,603,313 bytes | 10,746,673 bytes | +143,360 bytes |
| ZIP | 4,461,429 bytes | 4,528,346 bytes | +66,917 bytes |
| 首次测量：进程创建到窗口可响应 | 247.930 ms | 198.407 ms | -49.523 ms |
| 缓存后启动中位数，5 次 | 170.737 ms | 168.460 ms | -2.277 ms |
| 空闲 Working Set 中位数，5 次 | 71,917,568 bytes | 72,151,040 bytes（68.81 MiB） | +233,472 bytes |
| 空闲 Private Memory 中位数，5 次 | 79,364,096 bytes | 82,112,512 bytes（78.31 MiB） | +2,748,416 bytes |

10 份项目自制最小 OBJ 的复制实例按顺序完成，总耗时 235.050 ms。单任务 GUI
运行的 Working Set 峰值为 73,928,704 bytes；10 任务运行峰值为 87,666,688
bytes，批次完成后回落到 74,014,720 bytes。10 个 GLB 与单项报告均已生成，
且没有事务临时目录残留。这些数字只描述当前候选版本，不构成大型资产吞吐保证。

人工 GUI 验收使用项目自制资产：100 个 OBJ 队列完整转换成功；无效 OBJ 被隔离为
`Failed`，且没有阻止其他有效任务继续；`Cancel After Current` 让活动事务完成后，
将后续排队任务标记为 `Canceled`。未观察到错误最终目录或事务临时目录残留。这些是
人工验收结果，与自动化 CTest 证据分开记录。

测试机器：AMD Ryzen 5 9600X、12 个逻辑处理器、Windows build
10.0.26200.8875。启动时间从创建进程计时，到主窗口存在且可响应为止；空闲内存
在该时刻后 1.5 秒、未加载资产时采样。v0.3.0 测量使用合并到 main 的批处理功能树，
其 merge commit 为 `f8724bd2999316f45f81520b8f119dbd02d528bb`，并使用基于该
main 的 v0.3.0 Release Preparation 构建。

完整 DLL 与验证证据见[作品集案例](docs/PORTFOLIO_CASE_STUDY.md)。

## Windows 未签名构建

v0.3.0 便携构建没有进行 Authenticode 签名。部分 Windows 安全策略可能阻止
未签名程序。AssetBridge 不建议关闭或绕过 Smart App Control、杀毒软件或组织的
代码完整性策略。如果程序被阻止，安全替代方案是检查源码并自行构建，或等待未来
签名版本。

随包提供的 SHA-256 只验证压缩包完整性，不是身份签名，也不能替代代码签名。

## 文档

- [v0.3.0 Release Notes（英文）](docs/RELEASE_NOTES_v0.3.0.md)
- [v0.2.0 Release Notes（英文）](docs/RELEASE_NOTES_v0.2.0.md)
- [架构说明](docs/ARCHITECTURE.md)
- [验证流水线](docs/VALIDATION_PIPELINE.md)
- [作品集案例](docs/PORTFOLIO_CASE_STUDY.md)
- [贡献指南](CONTRIBUTING.md)
- [更新记录](CHANGELOG.md)

已发布的历史版本继续保留：
[v0.2.0](https://github.com/Fuyan-TA/AssetBridge/releases/tag/v0.2.0) 和
[v0.1.0](https://github.com/Fuyan-TA/AssetBridge/releases/tag/v0.1.0)。本文档不会在
v0.3.0 Release 实际存在前提前写入其 URL。

## 许可证

AssetBridge 使用 [MIT License](LICENSE)。第三方库和随包分发的运行时依赖记录在
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)，许可证原文位于
`third_party/licenses/`。
