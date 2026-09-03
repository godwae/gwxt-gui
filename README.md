# 技能等级评价题库下载器 (gwxt-gui)

从技能等级评价系统批量下载题库的桌面 GUI 工具（C++ / Qt5）。

## 功能

- 应用内账号密码登录：直连统一权限认证中心（ISC SSO），无需浏览器，密码经 RSA 加密传输
- 关键字检索 + 勾选，批量下载为 CSV（Excel 可直接打开）
- 全选 / 取消全选 / 反选
- 进度条 + 实时日志，会话失效自动熔断
- 深色 / 浅色 / 跟随系统主题，选择自动记忆

## 使用

- 打开程序点 **「账号登录」**（所选单位须与账号归属单位一致），登录后自动拉取题库列表
- 关键字过滤 → 勾选专业 → 选保存目录 → 「开始下载」
- 会话过期时重新点「账号登录」即可

### 编译

```bash
# Linux (Fedora / Ubuntu)
sudo dnf install qt5-qtbase-devel mesa-libGL-devel cmake gcc-c++   # Ubuntu: qtbase5-dev libgl-dev cmake build-essential
cmake -B build && cmake --build build && ./build/gwxt-gui

# Windows: 用 Qt Creator 打开 gwxt-gui.pro，或
cmake -B build -G "MinGW Makefiles" && cmake --build build
```

### 便携版（开箱即用，免安装）

| 模式 | 命令 | 产物 |
|---|---|---|
| 单文件（推荐） | `scripts\deploy.bat standalone` | 一个 `gwxt-gui.exe`（Qt 静态链接，零依赖） |
| 目录版 | `scripts\deploy.bat folder` | exe + Qt DLL 目录 |

Linux: `scripts/deploy.sh`。

## 注意事项

- 未实名认证的账号可能被认证中心拒绝，需先在网页端完成实名认证
- SSO 校验由服务端完成，提交后需等待十几秒
- 单文件版首次构建需一次性静态 Qt（`scripts\build-qt-static.bat`）

## 声明

- 本项目 **仅供内部交流与技术讨论**，不构成任何对外承诺或担保。
- 作者**不参与软件分发**：仓库不提供 Release / 预编译包，请自行下载源码编译。
- If you find any issue, please contact me immediately — I will delete it at light speed.
