# ADR 0007：静态集成固定版本的 yyjson

- 状态：已接受
- 日期：2026-08-10

## 背景

配置规范要求使用带版本的 JSON 文件，并正确处理损坏输入、UTF-8、整数边界和未来版本。Windows 没有适合此处的轻量原生 JSON API。自行实现解析器会把 Unicode、数字和恶意输入处理变成项目自有的长期安全负担；模板型 C++ JSON 库更易使用，但与本项目优先控制编译和二进制体积的目标不完全一致。

## 决策

首版固定使用 yyjson 0.12.0。只将上游发布中的 `yyjson.h`、`yyjson.c` 和 MIT `LICENSE` 置于 `third_party/yyjson/`，静态编译进唯一可执行文件：

- 不依赖运行时 DLL。
- 不在 CMake 配置或构建期间联网下载依赖。
- 在 `third_party/yyjson/README.md` 记录上游地址、版本、取得日期和源文件 SHA-256。
- 第三方目标关闭其测试和工具，并与第一方 `/W4 /WX`、clang-tidy 和 `/analyze` 目标隔离；这不是全局关闭第一方诊断。
- 只有 `config` 持久化实现可以包含 yyjson 头文件。公共 `AppConfig`、校验和排行接口不得暴露 yyjson 类型。
- 使用严格 RFC 8259 读取模式，不启用 JSON5、注释、尾随逗号或扩展数字。

## 后果

- 配置解析复用经过独立测试的严格 UTF-8/数字实现，同时保持静态单文件交付。
- 仓库需要维护第三方许可证和来源校验记录，并在升级时作为独立变更重新运行全部质量门与配置回归测试。
- yyjson 是 C API，项目必须在一个小型 RAII 适配层内管理文档和写入缓冲区，不能把裸所有权传播到其他模块。
- “体积更小”目前只是选择方向，不是已验证结论；Release 原型必须记录引入前后可执行文件尺寸。

## 参考

- https://github.com/ibireme/yyjson
- https://github.com/ibireme/yyjson/releases/tag/0.12.0
