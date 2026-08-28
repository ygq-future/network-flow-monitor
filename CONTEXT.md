# Network Flow Monitor 领域词汇表

## 运行形态

**产品宿主 (Product Host)**
用户会话中的普通权限进程，承载产品窗口、托盘、设置和 Collector 生命周期。

**便携采集器 (Portable Collector)**
随产品宿主启动的 C++ 采集进程，拥有 ETW 所需权限，并在宿主结束时退出。

**采集会话 (Collection Session)**
Collector 的一次运行纪元，由 `collector_instance_id` 唯一标识。

## 界面

**监控面板 (Monitor Panel)**
同时展示产品身份、总上传/下载速率和 Top N 应用排行的单一桌面表面。

**胶囊 (Dock Capsule)**
监控面板贴边收缩后的速率入口，可揭示面板并沿贴边方向拖动。

**贴边 (Dock)**
监控表面吸附到释放点所在显示器工作区左侧或右侧的行为。

**固定显示 (Pin)**
阻止监控面板在指针离开后收缩为胶囊的持久用户偏好。

**仅托盘 (Tray Only)**
监控面板和胶囊隐藏、托盘与采集继续运行的呈现状态。

## 数据

**流量节拍 (Traffic Tick)**
Collector 每秒四次发布的总上传和下载速率状态。

**排行快照 (Ranking Snapshot)**
Collector 每秒一次发布的 Top N 应用速率状态。

**会话重置 (Session Reset)**
Collector instance 改变时清除序列假设、陈旧排行和跨会话视觉历史的行为。
