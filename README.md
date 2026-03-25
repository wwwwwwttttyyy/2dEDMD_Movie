# EDMD 2D

二维硬盘体系的事件驱动分子动力学（Event-Driven Molecular Dynamics, EDMD）模拟与可视化项目。

仓库内包含三个主要可执行程序：

- `edmd2d`：纯命令行模拟器
- `viewer`：快照 / 轨迹文件回放器
- `viewer_sim`：实时模拟 + 文件回放可视化程序

此外还提供一个辅助脚本：

- `visualize.py`：将快照文件渲染为 `snapshot.png`

## 功能特性

- 纯二维硬盘碰撞模型
- 事件驱动时间推进
- 周期性边界条件
- 邻居列表 + cell list 加速
- 支持从文件加载初始构型，或自动生成正方晶格初态
- 支持 Andersen 热浴
- 输出最终快照、轨迹文件和压力数据
- 基于 raylib 的实时可视化与文件回放

## 项目结构

| 路径 | 说明 |
|------|------|
| `main.cpp` | 命令行模拟入口，默认读取 `config.txt` |
| `viewer.cpp` | 图形化前端，构建为 `viewer` 和 `viewer_sim` |
| `src/edmd2d.cpp` | EDMD 核心仿真逻辑 |
| `include/edmd2d.h` | 仿真核心头文件 |
| `include/snapshot_io.h` | 快照 / 轨迹读写工具 |
| `config.txt` | 示例配置文件 |
| `visualize.py` | 将快照渲染为图片的脚本 |

## 构建依赖

| 依赖 | 用途 | 备注 |
|------|------|------|
| C++17 编译器 | 编译核心程序 | GCC / Clang / MSVC 均可 |
| CMake >= 3.10 | 项目构建 | 必需 |
| raylib 5.5 | 图形界面 | `BUILD_VIEWER=ON` 时通过 FetchContent 自动下载 |
| X11 开发库 | Linux / WSL 下构建 raylib | `viewer` / `viewer_sim` 需要 |
| Python 3 | 运行 `visualize.py` | 可选 |
| `numpy`、`matplotlib` | 生成快照图片 | 可选 |

Linux / WSL 下若需要构建图形界面，可先安装：

```bash
sudo apt install build-essential cmake \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev
```

## 快速开始

### 1. 构建全部目标

Linux / macOS / WSL：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Windows（Ninja 示例）：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

首次构建图形界面时，CMake 会联网下载 raylib。

### 2. 仅构建命令行模拟器

如果当前环境不方便安装图形依赖，可以关闭 viewer：

```bash
cmake -S . -B build -DBUILD_VIEWER=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## 运行方式

> 以下命令默认在构建目录下执行，例如 `cd build` 后运行。Windows 下可执行文件名为 `*.exe`。

### 命令行模拟

```bash
./edmd2d
```

`edmd2d` 当前会固定读取当前工作目录中的 `config.txt`。

### 实时模拟

```bash
./viewer_sim
./viewer_sim -c ../config.txt
./viewer_sim --speed 10 --paused
```

常用参数：

- `-c, --config <file>`：指定配置文件
- `--speed <val>`：设置初始物理时间推进速度
- `--paused`：启动后先暂停
- `--no-info`：隐藏信息面板
- `--fullscreen`：全屏启动
- `--size WxH`：设置窗口尺寸，例如 `--size 1280x720`

### 文件回放

```bash
./viewer ../snapshot_end.dat
./viewer --fps 20 ../movie.dat
./viewer_sim ../movie.dat
```

说明：

- `viewer` 仅用于文件回放
- `viewer_sim` 无参数时进入实时模拟模式，传入数据文件时进入回放模式
- `--fps <val>` 可设置初始回放帧率

### 快照转图片

```bash
python visualize.py snapshot_end.dat
```

运行后会生成 `snapshot.png`。

## 配置文件说明

项目默认使用根目录下的 `config.txt`，采用 `key = value` 格式。

| 参数 | 含义 | 说明 |
|------|------|------|
| `seed` | 随机数种子 | 用于初始化速度随机数 |
| `maxtime` | 模拟总时间 | 仿真停止时间 |
| `writeinterval` | 输出间隔 | 控制屏幕日志和压力文件输出频率 |
| `makesnapshots` | 是否写轨迹 | `1` 为输出轨迹，`0` 为关闭 |
| `snapshotinterval` | 轨迹快照间隔 | 仅在 `makesnapshots=1` 时生效 |
| `initialconfig` | 初始构型模式 | `0` 从文件读取，`1` 生成正方晶格 |
| `inputfilename` | 输入构型文件 | `initialconfig=0` 时使用 |
| `packfrac` | 填充率 | `initialconfig=1` 时使用 |
| `N` | 粒子数 | `initialconfig=1` 时使用；会自动调整为完全平方数 |
| `maxscheduletime` | 事件列表覆盖时间 | 高级调参项 |
| `eventlisttimemultiplier` | 事件列表时间倍率 | 高级调参项 |
| `shellsize` | 邻居列表壳层宽度 | 推荐 `1.2 ~ 2.0` |
| `usethermostat` | 是否开启 Andersen 热浴 | `1` 开启，`0` 关闭 |
| `thermostatinterval` | 热浴触发间隔 | 仅在热浴开启时生效 |

示例：

```ini
seed = 1
maxtime = 10000
writeinterval = 100.0

makesnapshots = 1
snapshotinterval = 1000

initialconfig = 0
inputfilename = config1_vf_0.750.dat

shellsize = 1.5
usethermostat = 1
thermostatinterval = 0.01
```

## 输出文件

模拟运行后通常会生成以下文件：

- `snapshot_end.dat`：模拟结束时的最终快照
- `mov.n<N>.a<AREA>.dat`：轨迹文件，`makesnapshots=1` 时生成
- `press.n<N>.a<AREA>.dat`：压力随时间变化数据
- `snapshot.png`：由 `visualize.py` 生成的图片

## 数据格式

### 标准快照 / 轨迹帧格式

```text
Lx Ly N
x y r type
x y r type
...
```

### 简化输入格式

输入文件也支持省略 `N` 和 `type`：

```text
Lx Ly
x y r
x y r
...
```

这种情况下程序会根据粒子半径自动推断类型编号。

## 交互操作

| 操作 | 模式 | 说明 |
|------|------|------|
| `Space` | 全部 | 暂停 / 继续 |
| `+` / `-` | 全部 | 加速 / 减速 |
| `I` | 全部 | 显示 / 隐藏信息面板 |
| `R` | 全部 | 重置视角 |
| `F` | 全部 | 切换全屏 |
| `Esc` / `Q` | 全部 | 退出 |
| 鼠标滚轮 | 全部 | 缩放 |
| 右键拖拽 | 全部 | 平移 |
| `Left` / `Right` 或 `A` / `D` | 回放 | 上一帧 / 下一帧 |
| `Home` / `End` | 回放 | 跳到首帧 / 末帧 |

