# Water Sort Puzzle 🧪

基于 **OpenGL + GLSL** 的 3D 水瓶排序益智游戏。将不同颜色的液体倒入瓶中，使每个瓶子只包含单一颜色。

## 功能特性

- **3D 渲染** — 玻璃瓶透明效果 + 三光源照明系统
- **GLSL 着色器** — 物理级液体渲染，支持液面裁剪和边缘高光
- **倾倒动画** — 五阶段动画状态机 + 贝塞尔曲线液流模拟
- **多难度关卡** — Easy / Medium / Hard 内置关卡 + 自定义关卡文件导入
- **鼠标交互** — 颜色拾取法选瓶 + 悬停提示 + 可操作性预览
- **评分系统** — 效率 / 难度 / 完美度三维评分，S~E 六级评级
- **Demo 模式** — 自动演示求解过程

## 技术栈

| 组件 | 说明 |
|------|------|
| C++ (MSVC v143) | 核心语言，约 2800 行 |
| OpenGL 2.0+ | 3D 图形渲染 API |
| GLSL 1.20 | 液体着色器（Vertex + Fragment） |
| FreeGLUT 3.0 | 窗口管理、输入事件 |
| GLEW 2.1 | OpenGL 扩展加载 |

## 项目结构

```
├── WaterSort.cpp           # 主源代码（游戏逻辑 + 渲染 + 交互）
├── WaterSort.sln/.vcxproj  # Visual Studio 工程文件
├── level*.txt              # 关卡配置文件
├── diagrams/               # 12 张 SVG 架构图
│   ├── architecture.svg
│   ├── render_pipeline.svg
│   ├── liquid_rendering.svg
│   └── ...
└── docs/                   # 项目详细文档
    └── README_Project.md
```

## 构建与运行

### 环境要求

- Windows 10/11
- Visual Studio 2022（需安装 C++ 桌面开发工作负载）
- 显卡支持 OpenGL 2.0+

### 构建步骤

1. 打开 `WaterSort.sln`
2. 配置平台为 **x86 (Win32)**
3. 将 `glew32.dll`、`freeglut.dll`、`glut32.dll` 放入输出目录
4. 编译运行（F5）

### 依赖库配置

需要以下库文件（已在项目属性中配置）：
- `glew32.lib` + `glew32.dll`
- `freeglut.lib` + `freeglut.dll`
- `opengl32.lib` + `glu32.lib`

> 可从 [GLEW](http://glew.sourceforge.net/) 和 [FreeGLUT](http://freeglut.sourceforge.net/) 官网获取。

## 操作说明

| 操作 | 功能 |
|------|------|
| 左键点击瓶子 | 选择源瓶 → 点击目标瓶倾倒 |
| 鼠标悬停 | 显示瓶子编号和颜色信息 |
| ↑↓ 方向键 | 菜单选择 / 相机俯仰 |
| ←→ 方向键 | 相机旋转 |
| R | 重新开始当前关卡 |
| U | 撤销上一步 |
| ESC | 返回菜单 |

## 自定义关卡

创建 `.txt` 文件，每行一个瓶子，数字代表颜色（0 = 空）：

```
4           ← 瓶子数量
4 1 2 1 2   ← 瓶子1：4层，颜色序列
4 2 1 2 1   ← 瓶子2
0           ← 空瓶
0           ← 空瓶
```

在游戏菜单中选择「Load Custom」导入。

## 架构概览

详细技术文档见 [docs/README_Project.md](docs/README_Project.md)，包含：
- 系统架构设计
- 渲染管线流程
- 液体物理模拟算法
- 得分评判系统
- 12 张 SVG 架构图

## License

本项目为合肥工业大学计算机图形学课程作业。
