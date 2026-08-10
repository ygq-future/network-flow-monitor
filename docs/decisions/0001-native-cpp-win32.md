# ADR 0001：使用 C++20 和原生 Win32

- 状态：已接受
- 日期：2026-08-10

## 背景

产品首要约束是 Windows 11 专用、体积小、启动快，并需要直接使用 ETW、服务、托盘、命名管道、Direct2D 和多显示器 API。

## 决策

使用 C++20、CMake、Win32、Direct2D 和 DirectWrite。不采用 Qt、Electron、WinUI 3、WPF 或 Windows App SDK。

## 后果

- 可以直接控制运行库、二进制大小、权限和 Windows API 生命周期。
- 需要更严格的 RAII、错误处理、线程和 COM 约束。
- UI 开发效率低于高级框架，但功能范围小且视觉结构简单。
