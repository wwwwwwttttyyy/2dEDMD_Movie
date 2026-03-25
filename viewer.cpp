//=============================================================
//  viewer.cpp  —  EDMD 2D 实时 OpenGL 可视化前端
//
//  两种模式：
//    1. 实时仿真（无参数）：  viewer
//       直接运行 EDMD 仿真并实时渲染粒子运动
//    2. 文件回放（有参数）：  viewer  <snapshot.dat 或 movie.dat>
//       读取快照/轨迹文件并逐帧播放
//
//  键鼠操作（两种模式均支持）：
//    空格      — 暂停 / 继续
//    + / -     — 加速 / 减速
//    ← / →     — 上一帧 / 下一帧（文件模式）
//    Home/End  — 跳到首帧 / 末帧（文件模式）
//    I         — 显示 / 隐藏信息面板
//    R         — 重置视角
//    滚轮      — 缩放
//    右键拖拽  — 平移
//
//  依赖：raylib 5.x（通过 CMake FetchContent 自动下载）
//=============================================================

#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//==============================================================
//  命令行参数结构体
//==============================================================
struct AppArgs
{
    const char* configFile  = "config.txt"; // 仿真配置
    const char* replayFile  = nullptr;       // 回放文件（非空则进入回放模式）
    double      initSpeed   = 1.0;           // 初始仿真速度（t/s）
    float       initFPS     = 10.0f;         // 回放帧率
    bool        startPaused = false;
    bool        showInfo    = true;
    bool        fullscreen  = false;
    int         winW        = 900;
    int         winH        = 900;
};

static void printHelp(const char* prog)
{
    printf(
        "\nEDMD 2D Viewer\n"
        "\n"
        "用法：\n"
        "  %s [选项]                    实时仿真模式（需 BUILD_WITH_EDMD）\n"
        "  %s [选项] <snapshot.dat>     文件回放模式\n"
        "\n"
        "通用选项：\n"
        "  -h, --help                  显示本帮助\n"
        "  --no-info                   启动时隐藏信息面板\n"
        "  --fullscreen                全屏启动\n"
        "  --size WxH                  窗口尺寸，例如 --size 1280x720\n"
        "\n"
        "仿真模式选项：\n"
        "  -c, --config <config.txt>   指定配置文件（默认 config.txt）\n"
        "  --speed <val>               初始仿真速度 t/s（默认 1.0）\n"
        "  --paused                    启动时暂停\n"
        "\n"
        "回放模式选项：\n"
        "  --fps <val>                 初始回放帧率（默认 10）\n"
        "\n"
        "运行时操作：\n"
        "  空格        暂停 / 继续\n"
        "  +/-         加速 / 减速\n"
        "  ←→ / A/D   上一帧 / 下一帧（回放）\n"
        "  Home/End    跳到首帧 / 末帧（回放）\n"
        "  滚轮        缩放\n"
        "  右键拖拽    平移\n"
        "  R           重置视角\n"
        "  I           显示 / 隐藏信息面板\n"
        "  F           切换全屏\n"
        "  Esc / Q     退出\n"
        "\n",
        prog, prog);
}

static AppArgs parseArgs(int argc, char** argv)
{
    AppArgs a;
    for (int i = 1; i < argc; i++)
    {
        std::string s = argv[i];
        if (s == "-h" || s == "--help")
        { printHelp(argv[0]); std::exit(0); }
        else if ((s == "-c" || s == "--config") && i + 1 < argc)
            a.configFile = argv[++i];
        else if (s == "--speed" && i + 1 < argc)
            a.initSpeed = std::atof(argv[++i]);
        else if (s == "--fps" && i + 1 < argc)
            a.initFPS = (float)std::atof(argv[++i]);
        else if (s == "--paused")
            a.startPaused = true;
        else if (s == "--no-info")
            a.showInfo = false;
        else if (s == "--fullscreen")
            a.fullscreen = true;
        else if (s == "--size" && i + 1 < argc)
        {
            std::string wh = argv[++i];
            auto pos = wh.find_first_of("xX");
            if (pos != std::string::npos)
            { a.winW = std::atoi(wh.substr(0, pos).c_str());
              a.winH = std::atoi(wh.substr(pos + 1).c_str()); }
        }
        else if (!s.empty() && s[0] != '-')
        {
            if (s.size() >= 4 && s.substr(s.size() - 4) == ".txt")
                a.configFile = argv[i];
            else
                a.replayFile = argv[i];
        }
        else
            fprintf(stderr, "未知选项：%s  （用 --help 查看帮助）\n", argv[i]);
    }
    return a;
}

//--------------------------------------------------------------
//  类型颜色表（最多 8 种粒子类型）
//--------------------------------------------------------------
static const Color PALETTE[] = {
    {100, 149, 237, 255}, // type 0 — 蓝
    {255,  80,  80, 255}, // type 1 — 红
    { 80, 200,  80, 255}, // type 2 — 绿
    {255, 200,  50, 255}, // type 3 — 金黄
    {200,  80, 200, 255}, // type 4 — 紫
    { 80, 210, 210, 255}, // type 5 — 青
    {255, 160,  40, 255}, // type 6 — 橙
    {160, 120, 220, 255}, // type 7 — 淡紫
};
static const int PALETTE_SIZE = 8;

//==============================================================
//  文件回放模式：快照/轨迹数据结构
//==============================================================
struct PRecord { float x, y, r; int type; };
struct Frame   { float lx, ly; std::vector<PRecord> particles; };

//--------------------------------------------------------------
//  从文件中读取所有帧（格式：Lx Ly N / x y r type 逐帧堆叠）
//--------------------------------------------------------------
static std::vector<Frame> loadFrames(const std::string& filename)
{
    std::vector<Frame> frames;
    std::ifstream f(filename);
    if (!f.is_open()) return frames;

    std::string line;
    while (f.good())
    {
        Frame fr;
        int n = 0;
        bool gotHeader = false;

        // 读取帧头（跳过注释与空行）
        while (std::getline(f, line))
        {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream iss(line);
            if (iss >> fr.lx >> fr.ly >> n) { gotHeader = true; break; }
        }
        if (!gotHeader || n <= 0) break;

        fr.particles.resize(n);
        for (int i = 0; i < n; i++)
        {
            PRecord& p = fr.particles[i];
            p.type = 0;
            while (std::getline(f, line))
            {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream iss(line);
                double px, py, pr;
                if (iss >> px >> py >> pr)
                {
                    int t = 0; iss >> t;
                    // 折回盒子 [0, L)
                    p.x = (float)std::fmod(px, fr.lx);
                    if (p.x < 0) p.x += fr.lx;
                    p.y = (float)std::fmod(py, fr.ly);
                    if (p.y < 0) p.y += fr.ly;
                    p.r    = (float)pr;
                    p.type = t;
                    break;
                }
            }
        }
        frames.push_back(std::move(fr));
    }
    return frames;
}

//==============================================================
//  Instanced renderer — 一次 draw call 绘制所有粒子圆盘
//  原理：把所有粒子的位置/半径/颜色打包成实例缓冲区，
//  GPU 并行处理，等价于 Python matplotlib.PatchCollection 的批绘
//==============================================================
static const int CIRCLE_SEGS = 36;
static const int MAX_INST    = 500000;  // 每帧最多实例数（含 PBC 幽灵）

struct InstanceData { float x, y, r, cr, cg, cb; };

struct InstRenderer
{
    unsigned int vao     = 0;
    unsigned int vboCirc = 0;
    unsigned int vboInst = 0;
    Shader       shader  = {0};
    int          locMVP  = -1;
    int          vertCnt = 0;
    bool         ready   = false;

    // ---- GLSL 330 着色器源码 ----
    static const char* VS()
    {
        return
            "#version 330 core\n"
            "layout(location=0) in vec2  aPos;\n"    // 单位圆顶点
            "layout(location=1) in vec2  aCenter;\n" // 粒子中心（仿真坐标）
            "layout(location=2) in float aRadius;\n" // 粒子半径
            "layout(location=3) in vec3  aColor;\n"  // RGB [0,1]
            "out vec4 vColor;\n"
            "uniform mat4 uMVP;\n"
            "void main() {\n"
            "    vec2 wp = aCenter + aPos * aRadius;\n"
            "    gl_Position = uMVP * vec4(wp, 0.0, 1.0);\n"
            "    vColor = vec4(aColor, 1.0);\n"
            "}\n";
    }
    static const char* FS()
    {
        return
            "#version 330 core\n"
            "in  vec4 vColor;\n"
            "out vec4 fragColor;\n"
            "void main() { fragColor = vColor; }\n";
    }

    // ---- 初始化（必须在 InitWindow 之后调用）----
    void init()
    {
        // 构建单位圆顶点（三角形列表，CIRCLE_SEGS 个三角形）
        std::vector<float> verts;
        verts.reserve(CIRCLE_SEGS * 6);
        for (int i = 0; i < CIRCLE_SEGS; i++)
        {
            float a0 = 2.0f * (float)M_PI *  i      / CIRCLE_SEGS;
            float a1 = 2.0f * (float)M_PI * (i + 1) / CIRCLE_SEGS;
            verts.push_back(0.0f);        verts.push_back(0.0f);
            verts.push_back(cosf(a0));    verts.push_back(sinf(a0));
            verts.push_back(cosf(a1));    verts.push_back(sinf(a1));
        }
        vertCnt = CIRCLE_SEGS * 3;

        shader = LoadShaderFromMemory(VS(), FS());
        locMVP = GetShaderLocation(shader, "uMVP");

        // 建立 VAO
        vao = rlLoadVertexArray();
        rlEnableVertexArray(vao);

        // VBO 0：圆盘几何（属性 0，非实例化）
        vboCirc = rlLoadVertexBuffer(verts.data(),
                      (int)(verts.size() * sizeof(float)), false);
        rlSetVertexAttribute(0, 2, RL_FLOAT, false, 0, 0);
        rlEnableVertexAttribute(0);

        // VBO 1：实例数据（属性 1/2/3，divisor = 1）
        vboInst = rlLoadVertexBuffer(nullptr,
                      MAX_INST * (int)sizeof(InstanceData), true);
        int st = (int)sizeof(InstanceData);  // stride = 24 bytes

        rlSetVertexAttribute(1, 2, RL_FLOAT, false, st, 0);                      // center
        rlEnableVertexAttribute(1);  rlSetVertexAttributeDivisor(1, 1);

        rlSetVertexAttribute(2, 1, RL_FLOAT, false, st, 2*(int)sizeof(float));   // radius
        rlEnableVertexAttribute(2);  rlSetVertexAttributeDivisor(2, 1);

        rlSetVertexAttribute(3, 3, RL_FLOAT, false, st, 3*(int)sizeof(float));   // color rgb
        rlEnableVertexAttribute(3);  rlSetVertexAttributeDivisor(3, 1);

        rlDisableVertexArray();
        ready = true;
    }

    // ---- 绘制 count 个实例 ----
    // offX/offY/scale 是仿真坐标→屏幕的变换参数（与 drawParticles 一致）
    void draw(const InstanceData* data, int count,
              float lx, float ly,
              float offX, float offY, float scale, int W, int H)
    {
        if (!ready || count == 0) return;
        count = std::min(count, MAX_INST);

        // 正交投影矩阵（仿真坐标 → NDC，y 轴翻转）
        // ndc_x = sx*sim_x + tx,  ndc_y = sy*sim_y + ty
        float sx = 2.0f * scale / W;
        float sy = 2.0f * scale / H;
        float tx = 2.0f * offX / W - 1.0f;
        float ty = 1.0f - 2.0f * (offY + ly * scale) / H;

        // raylib Matrix 列主序: m0-m3=列0, m4-m7=列1, ...
        // raylib Matrix 结构体成员声明顺序：m0,m4,m8,m12, m1,m5,m9,m13, ...
        // 即每 4 个字段 = 一行，MatrixToFloat 按 m0,m1,m2,...m15 输出，
        // OpenGL 以列主序读取 → 正确的平移列需在 m12,m13 位置。
        Matrix mvp = { sx, 0,  0, tx,   // m0  m4  m8  m12
                        0, sy, 0, ty,   // m1  m5  m9  m13
                        0,  0, 1,  0,   // m2  m6  m10 m14
                        0,  0, 0,  1 }; // m3  m7  m11 m15

        rlDrawRenderBatchActive();           // 先刷新 raylib 批绘缓冲
        rlUpdateVertexBuffer(vboInst, data,
                             count * (int)sizeof(InstanceData), 0);
        rlEnableShader(shader.id);
        rlSetUniformMatrix(locMVP, mvp);
        rlEnableVertexArray(vao);
        rlDrawVertexArrayInstanced(0, vertCnt, count);
        rlDisableVertexArray();
        rlDisableShader();
    }

    void unload()
    {
        if (!ready) return;
        rlUnloadVertexArray(vao);
        rlUnloadVertexBuffer(vboCirc);
        rlUnloadVertexBuffer(vboInst);
        UnloadShader(shader);
        ready = false;
    }
};

//==============================================================
//  公共绘制函数
//  输入：粒子数组 + box尺寸，屏幕偏移与缩放
//==============================================================
static void drawParticles(
    const float* px, const float* py, const float* pr, const int* pt,
    int n, float lx, float ly,
    float offX, float offY, float scale,
    int W, int H, InstRenderer& rend)
{
    // 盒子边框（仍用 raylib API，开销极小）
    DrawRectangleLinesEx({offX, offY, lx * scale, ly * scale}, 2.0f, WHITE);

    // 构建实例列表（含 PBC 幽灵粒子，在仿真坐标系裁剪）
    static std::vector<InstanceData> inst;
    inst.clear();
    for (int i = 0; i < n; i++)
    {
        const Color& col = PALETTE[pt[i] % PALETTE_SIZE];
        float cr  = col.r / 255.0f;
        float cg  = col.g / 255.0f;
        float cbv = col.b / 255.0f;
        float r   = pr[i];
        for (int ddx = -1; ddx <= 1; ddx++)
        {
            float ix = px[i] + ddx * lx;
            if (ix + r < 0.0f || ix - r > lx) continue;
            for (int ddy = -1; ddy <= 1; ddy++)
            {
                float iy = py[i] + ddy * ly;
                if (iy + r < 0.0f || iy - r > ly) continue;
                inst.push_back({ix, iy, r, cr, cg, cbv});
            }
        }
    }
    // 一次 draw call 绘制所有粒子
    rend.draw(inst.data(), (int)inst.size(), lx, ly, offX, offY, scale, W, H);
}

//==============================================================
//  计算填充率
//==============================================================
static float computePhi(const float* pr, int n, float lx, float ly)
{
    float sum = 0;
    for (int i = 0; i < n; i++) sum += pr[i] * pr[i];
    return sum * (float)M_PI / (lx * ly);
}

//==============================================================
//  实时仿真模式（需链接 edmd2d.cpp）
//==============================================================
#ifdef BUILD_WITH_EDMD

#include "edmd2d.h"

static void runSimMode(const AppArgs& args)
{
    loadConfig(args.configFile);
    init();

    // 临时数组（逐帧填充当前外推位置）
    std::vector<float> px(N), py(N), pr(N);
    std::vector<int>   pt(N);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT |
                   (args.fullscreen ? FLAG_FULLSCREEN_MODE : 0));
    InitWindow(args.winW, args.winH, "EDMD 2D Viewer — 实时仿真");
    SetTargetFPS(60);

    InstRenderer rend;
    rend.init();

    double physTimePerSec = args.initSpeed;
    bool   paused         = args.startPaused;
    bool   showInfo       = args.showInfo;
    bool   simDone        = false;

    float  zoom = 1.0f;
    Vector2 pan = {0, 0};
    bool   dragging  = false;
    Vector2 dragAnch = {0, 0};
    Vector2 panAnch  = {0, 0};

    while (!WindowShouldClose())
    {
        int   W  = GetScreenWidth();
        int   H  = GetScreenHeight();
        float dt = GetFrameTime();

        // --- 输入 ---
        if (IsKeyPressed(KEY_SPACE)) paused = !paused;
        if (IsKeyPressed(KEY_I))     showInfo = !showInfo;
        if (IsKeyPressed(KEY_R))     { zoom = 1.0f; pan = {0, 0}; }
        if (IsKeyPressed(KEY_F))     ToggleFullscreen();
        if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_ESCAPE)) break;
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))
            physTimePerSec = std::min(physTimePerSec * 2.0, 10000.0);
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))
            physTimePerSec = std::max(physTimePerSec * 0.5, 0.001);

        // 滚轮缩放
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            Vector2 m = GetMousePosition();
            float z = (wheel > 0) ? 1.25f : 0.8f;
            pan.x = m.x + (pan.x - m.x) * z;
            pan.y = m.y + (pan.y - m.y) * z;
            zoom *= z;
        }

        // 右键平移
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
        {
            dragging = true;
            dragAnch = GetMousePosition();
            panAnch  = pan;
        }
        if (dragging)
        {
            Vector2 m = GetMousePosition();
            pan.x = panAnch.x + m.x - dragAnch.x;
            pan.y = panAnch.y + m.y - dragAnch.y;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) dragging = false;

        // --- 推进仿真 ---
        if (!paused && !simDone)
        {
            double targetTime = simtime + dt * physTimePerSec;
            int stepCap = 500000; // 每帧最大步数，防止界面卡死
            while (simtime < targetTime && simtime < maxtime && stepCap-- > 0)
                step();
            if (simtime >= maxtime) simDone = true;
        }

        // --- 收集当前粒子位置（线性外推到 simtime）---
        double KE  = 0.0;
        for (int i = 0; i < N; i++)
        {
            Particle* p = &particles[i];
            double dt2 = simtime - p->t;
            double cx  = p->x + p->vx * dt2;
            double cy  = p->y + p->vy * dt2;
            // 折回盒子
            cx = std::fmod(cx, xsize); if (cx < 0) cx += xsize;
            cy = std::fmod(cy, ysize); if (cy < 0) cy += ysize;
            px[i] = (float)cx;
            py[i] = (float)cy;
            pr[i] = (float)p->radius;
            pt[i] = (int)p->type;
            KE += p->mass * (p->vx * p->vx + p->vy * p->vy);
        }
        double T   = KE / (2.0 * N);
        float  phi = computePhi(pr.data(), N, (float)xsize, (float)ysize);

        // --- 缩放与偏移 ---
        float margin   = 50.0f;
        float baseScale = std::min((W - 2.f * margin) / (float)xsize,
                                   (H - 2.f * margin) / (float)ysize);
        float scaleFinal = baseScale * zoom;
        float boxW  = (float)xsize * scaleFinal;
        float boxH  = (float)ysize * scaleFinal;
        float offX  = (W - boxW) * 0.5f + pan.x;
        float offY  = (H - boxH) * 0.5f + pan.y;

        // --- 绘制 ---
        BeginDrawing();
        ClearBackground({18, 18, 28, 255});

        drawParticles(px.data(), py.data(), pr.data(), pt.data(),
                      N, (float)xsize, (float)ysize,
                      offX, offY, scaleFinal, W, H, rend);

        if (showInfo)
        {
            int hy = 10;
            DrawText(TextFormat("t = %.3f / %.1f", simtime, maxtime), 10, hy, 18, WHITE);
            hy += 22;
            DrawText(TextFormat("N = %d   phi = %.4f", N, phi), 10, hy, 18, LIGHTGRAY);
            hy += 22;
            DrawText(TextFormat("T = %.4f", T), 10, hy, 18, LIGHTGRAY);
            hy += 22;
            DrawText(TextFormat("Speed: %.3g t/s  [+/-]", physTimePerSec), 10, hy, 18, LIGHTGRAY);
            hy += 22;
            if (simDone)
                DrawText("仿真结束", 10, hy, 20, RED);
            else
                DrawText(paused ? "PAUSED  [SPACE]" : "RUNNING [SPACE]",
                         10, hy, 18, paused ? YELLOW : GREEN);
            DrawText("I: 面板  R: 视角  滚轮: 缩放  右键: 平移", 10, H - 22, 14, DARKGRAY);
        }
        DrawFPS(W - 90, 10);
        EndDrawing();
    }
    rend.unload();
    CloseWindow();
}

#endif // BUILD_WITH_EDMD

//==============================================================
//  文件回放模式
//==============================================================
static void runFileMode(const std::string& filename, const AppArgs& args)
{
    auto frames = loadFrames(filename);
    if (frames.empty())
    {
        // 显示错误窗口
        InitWindow(640, 160, "EDMD Viewer — 错误");
        SetTargetFPS(30);
        std::string msg = "无法加载：" + filename;
        while (!WindowShouldClose())
        {
            BeginDrawing();
            ClearBackground(BLACK);
            DrawText(msg.c_str(), 20, 40,  20, RED);
            DrawText("用法：viewer <snapshot.dat | movie.dat>", 20, 80, 16, GRAY);
            EndDrawing();
        }
        CloseWindow();
        return;
    }

    int nFrames = (int)frames.size();

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT |
                   (args.fullscreen ? FLAG_FULLSCREEN_MODE : 0));
    std::string title = "EDMD Viewer — " + filename +
                        " (" + std::to_string(nFrames) + " 帧)";
    InitWindow(args.winW, args.winH, title.c_str());
    SetTargetFPS(60);

    InstRenderer rend;
    rend.init();

    int    curFrame = 0;
    bool   playing  = (nFrames > 1) && !args.startPaused;
    float  playFPS  = args.initFPS;
    float  timer    = 0.0f;
    bool   showInfo = args.showInfo;

    float  zoom = 1.0f;
    Vector2 pan = {0, 0};
    bool   dragging  = false;
    Vector2 dragAnch = {0, 0};
    Vector2 panAnch  = {0, 0};

    while (!WindowShouldClose())
    {
        int   W  = GetScreenWidth();
        int   H  = GetScreenHeight();
        float dt = GetFrameTime();

        // --- 输入 ---
        if (IsKeyPressed(KEY_SPACE)) playing = !playing;
        if (IsKeyPressed(KEY_I))     showInfo = !showInfo;
        if (IsKeyPressed(KEY_R))     { zoom = 1.0f; pan = {0, 0}; }
        if (IsKeyPressed(KEY_F))     ToggleFullscreen();
        if (IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_ESCAPE)) break;

        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))
            curFrame = (curFrame + 1) % nFrames;
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A))
            curFrame = (curFrame - 1 + nFrames) % nFrames;
        if (IsKeyPressed(KEY_HOME)) curFrame = 0;
        if (IsKeyPressed(KEY_END))  curFrame = nFrames - 1;

        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))
            playFPS = std::min(playFPS * 2.0f, 120.0f);
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))
            playFPS = std::max(playFPS / 2.0f, 0.5f);

        // 滚轮缩放
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            Vector2 m = GetMousePosition();
            float z = (wheel > 0) ? 1.25f : 0.8f;
            pan.x = m.x + (pan.x - m.x) * z;
            pan.y = m.y + (pan.y - m.y) * z;
            zoom *= z;
        }

        // 右键平移
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
        {
            dragging = true;
            dragAnch = GetMousePosition();
            panAnch  = pan;
        }
        if (dragging)
        {
            Vector2 m = GetMousePosition();
            pan.x = panAnch.x + m.x - dragAnch.x;
            pan.y = panAnch.y + m.y - dragAnch.y;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) dragging = false;

        // 自动播放
        if (playing && nFrames > 1)
        {
            timer += dt;
            if (timer >= 1.0f / playFPS)
            {
                timer -= 1.0f / playFPS;
                curFrame = (curFrame + 1) % nFrames;
            }
        }

        const Frame& fr = frames[curFrame];
        int  n  = (int)fr.particles.size();
        auto& ps = fr.particles;

        // 构建临时数组
        std::vector<float> px(n), py_v(n), pr(n);
        std::vector<int>   pt(n);
        for (int i = 0; i < n; i++)
        {
            px[i]  = ps[i].x;
            py_v[i]= ps[i].y;
            pr[i]  = ps[i].r;
            pt[i]  = ps[i].type;
        }
        float phi = computePhi(pr.data(), n, fr.lx, fr.ly);

        // 计算缩放偏移
        float margin    = 50.0f;
        float baseScale = std::min((W - 2.f * margin) / fr.lx,
                                   (H - 2.f * margin) / fr.ly);
        float scaleFinal = baseScale * zoom;
        float boxW  = fr.lx * scaleFinal;
        float boxH  = fr.ly * scaleFinal;
        float offX  = (W - boxW) * 0.5f + pan.x;
        float offY  = (H - boxH) * 0.5f + pan.y;

        // --- 绘制 ---
        BeginDrawing();
        ClearBackground({18, 18, 28, 255});

        drawParticles(px.data(), py_v.data(), pr.data(), pt.data(),
                      n, fr.lx, fr.ly, offX, offY, scaleFinal, W, H, rend);

        if (showInfo)
        {
            int hy = 10;
            DrawText(TextFormat("帧: %d / %d", curFrame + 1, nFrames), 10, hy, 18, WHITE);
            hy += 22;
            DrawText(TextFormat("N = %d   phi = %.4f", n, phi), 10, hy, 18, LIGHTGRAY);
            hy += 22;
            DrawText(TextFormat("Box: %.2f x %.2f", fr.lx, fr.ly), 10, hy, 18, LIGHTGRAY);
            if (nFrames > 1)
            {
                hy += 22;
                DrawText(TextFormat("速度: %.1f fps  [+/-]", playFPS), 10, hy, 18, LIGHTGRAY);
                hy += 22;
                DrawText(playing ? ">> PLAYING  [SPACE]" : "|| PAUSED   [SPACE]",
                         10, hy, 18, playing ? GREEN : YELLOW);
            }
            DrawText("I: 面板  R: 视角  ←→: 切帧  Home/End  滚轮: 缩放  右键: 平移",
                     10, H - 22, 14, DARKGRAY);
        }
        DrawFPS(W - 90, 10);
        EndDrawing();
    }
    rend.unload();
    CloseWindow();
}

//==============================================================
//  主入口
//==============================================================
int main(int argc, char** argv)
{
    AppArgs args = parseArgs(argc, argv);

    if (args.replayFile)
        runFileMode(args.replayFile, args);
    else
    {
#ifdef BUILD_WITH_EDMD
        runSimMode(args);
#else
        InitWindow(700, 200, "EDMD Viewer");
        SetTargetFPS(30);
        while (!WindowShouldClose())
        {
            BeginDrawing();
            ClearBackground(BLACK);
            DrawText("用法：viewer <snapshot.dat | movie.dat>", 30,  50, 22, WHITE);
            DrawText("或：用 BUILD_WITH_EDMD 宏编译实时仿真版本",  30,  90, 18, GRAY);
            DrawText("运行 viewer --help 查看所有选项",             30, 130, 16, DARKGRAY);
            EndDrawing();
        }
        CloseWindow();
#endif
    }
    return 0;
}
