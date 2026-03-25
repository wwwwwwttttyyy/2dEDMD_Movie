# EDMD 2D — 二维事件驱动分子动力学模拟 + 可视化

## 依赖

| 依赖 | 用途 | 安装方式 |
|------|------|----------|
| C++17 编译器 | 编译 | WSL: `sudo apt install g++`；Windows: MSYS2 |
| CMake ≥ 3.10 | 构建 | WSL: `sudo apt install cmake` |
| raylib 5.5 | 可视化渲染 | CMake 自动下载（FetchContent） |
| X11 开发库 | raylib 依赖（Linux） | `sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev` |
| voro++ | Voronoi 缺陷上色 | `conda install -c conda-forge voro++`（analyse 环境） |

## 编译（WSL）

```bash
conda activate analyse
cd /mnt/d/Codes/EDMD/edmd_my_movie
mkdir build_wsl && cd build_wsl

# 首次需联网下载 raylib；已有本地源码可用 -DFETCHCONTENT_SOURCE_DIR_RAYLIB=<路径> 跳过
cmake .. \
    -DVOROPP_ROOT=$CONDA_PREFIX \
    -DCMAKE_BUILD_TYPE=Release \
    -DFETCHCONTENT_SOURCE_DIR_RAYLIB=/mnt/d/Codes/EDMD/edmd_my_movie/build/_deps/raylib-src

make -j$(nproc)
```

## 构建目标

| 目标 | 说明 |
|------|------|
| `edmd2d` | 纯命令行仿真，无渲染 |
| `viewer` | 文件回放模式：`./viewer <snapshot.dat>` |
| `viewer_sim` | 实时仿真 + 渲染（链接 edmd2d 仿真逻辑） |

## 使用

### 命令行仿真
```bash
./edmd2d                    # 读取当前目录 config.txt
```

### 实时仿真 + 可视化
```bash
./viewer_sim                # 读取 config.txt，实时运行并渲染
./viewer_sim -c other.txt   # 指定配置文件
```

### 文件回放
```bash
./viewer snapshot.dat       # 单帧快照
./viewer movie.dat          # 多帧轨迹回放
```

### 快捷键

| 按键 | 功能 |
|------|------|
| 空格 | 暂停 / 继续 |
| V | 切换 Voronoi 缺陷上色（5邻居蓝 / 6邻居绿 / 7邻居红） |
| +/- | 加速 / 减速 |
| ←→ | 上一帧 / 下一帧（回放模式） |
| R | 重置视角 |
| I | 显示 / 隐藏信息面板 |
| 滚轮 | 缩放 |
| 右键拖拽 | 平移 |

## config.txt 参数

```ini
seed = 1                    # 随机数种子
maxtime = 10000             # 模拟总时间
N = 4096                    # 粒子数
packfrac = 0.72             # 填充率 φ

initialconfig = 0           # 0=从文件读取
initialconfig = 1           # 1=正方晶格（N 自动调整为完全平方数）
initialconfig = 2           # 2=三角晶格（理想六角密排，自动计算 lx,ly）

makesnapshots = 1           # 是否输出轨迹
snapshotinterval = 100      # 轨迹快照间隔
usethermostat = 1           # Andersen 热浴开关
thermostatinterval = 0.01   # 热浴触发间隔
shellsize = 1.5             # 邻居列表壳层（推荐 1.2~2.0）
```
