# 项目版本管理

本项目使用 Git 管理代码、配置和文档，使用 Git LFS 管理 `.uasset`、`.umap` 和 `.png`。工程关联的 Unreal Engine 版本为 5.7。

## 提交范围

- 保留 `Source/`、`Config/`、`Tcamp_FPS.uproject`、`scripts/` 和项目文档。
- 保留 `Content/`，包括地图的外部 Actor/Object 文件。源码引用了 `/Game/RAR/` 下的资源，因此保留 `Content/RAR/` 的资产。
- 保留 `SourceArt/ShopIcons/` 中用于重新导入的原图。
- 保留 `Build/` 的必要平台配置，仅排除已知生成目录。
- 排除 `Binaries/`（工程根目录）、所有层级的 `Intermediate/`、`Saved/`、`DerivedDataCache/`，以及 IDE 本地状态和日志。
- `Content/RAR/Config/DefaultCrypto.ini` 含有密钥及签名私钥，仅在本地保留。需要该嵌套示例工程的打包加密设置时，通过独立安全渠道配置，勿提交密钥。
- 两份 `DefaultEngine.ini` 中的 Android File Server `SecurityToken` 已清空。原配置备份在被忽略的 `Saved/GitBootstrapLocalConfig/` 下；需要开发文件服务器时，在本地配置访问令牌。
- 未来添加仅提供二进制的插件时，保留其必需二进制文件。

## 获取和恢复工程

先安装 Git、Git LFS、Unreal Engine 5.7 及相应 C++ 编译工具。执行 `git lfs install` 后克隆仓库；若克隆时未下载资产，在仓库内执行 `git lfs pull`。通过 `.uproject` 生成 Visual Studio 工程文件，再编译并打开项目。

`Saved/` 中的自动保存、存档及临时调试文件不属于项目版本。需要保留的成果应先保存到正式资产、源码或文档位置。

## 日常操作

以 `main` 保存项目基线，功能开发使用 `feat/...` 分支。提交前执行 `git status` 和 `git diff --cached` 核对清单，使用 `git lfs ls-files` 检查资产跟踪。蓝图和地图发生冲突时，需要在 UE 编辑器中核对和处理资产。

新增较大的二进制素材格式时，先补充 `.gitattributes` 再添加文件。Git LFS 的历史对象占用存储，资产下载使用仓库所有者的流量额度。

远端仓库：`https://github.com/JOSEPHF691/UE5_FPS_demo`。
