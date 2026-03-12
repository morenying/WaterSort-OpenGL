// WaterSort.cpp - Water Sort Puzzle with Menu System
// Features: Start menu, difficulty selection, rules, precise pour animation
// Enhanced: GLSL shader for physically correct liquid rendering during tilt
// AI Agent: In-game AI chat assistant with RAG knowledge base
#define _CRT_SECURE_NO_WARNINGS
#define AI_AGENT_IMPLEMENTATION

#include <glew.h>  // GLEW must be included before other GL headers
#include <glut.h>
#include <GL.h>
#include <GLU.h>
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "ai_agent/ai_agent.h"

#pragma comment(lib, "glew32.lib")
#pragma comment(lib, "freeglut.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")

#define MAX_BOTTLES 12
#define MAX_LAYERS 4
#define PI 3.14159265358979f
#define SLICES 64

// ========== GLSL Shader Program ==========
GLuint liquidShaderProgram = 0;
GLuint liquidVertexShader = 0;
GLuint liquidFragmentShader = 0;
int useShaderLiquid = 1;  // 是否使用shader渲染液体

// Shader uniform locations
GLint uLiquidTopY = -1;         // 液面在局部坐标中的Y高度
GLint uLiquidBottomY = -1;      // 液体底部在局部坐标中的Y高度
GLint uLiquidColor = -1;        // 液体颜色

// Vertex Shader: 传递局部坐标位置
const char* liquidVertexShaderSrc = R"(
#version 120
varying vec3 localPos;
varying vec3 vNormal;
varying vec3 viewPos;

void main() {
    // 保存局部坐标（顶点原始位置）
    localPos = gl_Vertex.xyz;
    // 计算视图空间位置（用于光照）
    viewPos = (gl_ModelViewMatrix * gl_Vertex).xyz;
    // 传递法线
    vNormal = gl_NormalMatrix * gl_Normal;
    // 标准变换
    gl_Position = ftransform();
    gl_FrontColor = gl_Color;
}
)";

// Fragment Shader: 根据局部坐标Y裁剪液体
const char* liquidFragmentShaderSrc = R"(
#version 120
varying vec3 localPos;
varying vec3 vNormal;
varying vec3 viewPos;

uniform float uLiquidTopY;      // 液面局部Y坐标
uniform float uLiquidBottomY;   // 液体底部局部Y坐标
uniform vec4 uLiquidColor;      // 液体颜色

void main() {
    // 关键：根据局部坐标Y来裁剪
    // 液体只在 uLiquidBottomY 到 uLiquidTopY 之间可见
    if (localPos.y > uLiquidTopY + 0.01) {
        discard;  // 超过液面的部分不渲染
    }
    if (localPos.y < uLiquidBottomY - 0.01) {
        discard;  // 低于底部的部分不渲染
    }
    
    // 简单光照计算
    vec3 N = normalize(vNormal);
    vec3 L = normalize(vec3(0.5, 1.0, 0.3));
    float diff = max(dot(N, L), 0.0) * 0.6 + 0.4;
    
    // 边缘高光效果
    vec3 V = normalize(-viewPos);
    float rim = 1.0 - max(dot(N, V), 0.0);
    rim = pow(rim, 2.0) * 0.3;
    
    vec4 color = uLiquidColor;
    color.rgb *= diff;
    color.rgb += vec3(rim);
    
    gl_FragColor = color;
}
)";

// 编译shader
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        printf("Shader compilation error: %s\n", infoLog);
        return 0;
    }
    return shader;
}

// 初始化shader程序
void initLiquidShader() {
    // 编译vertex shader
    liquidVertexShader = compileShader(GL_VERTEX_SHADER, liquidVertexShaderSrc);
    if (!liquidVertexShader) {
        printf("Failed to compile vertex shader, falling back to fixed pipeline\n");
        useShaderLiquid = 0;
        return;
    }
    
    // 编译fragment shader
    liquidFragmentShader = compileShader(GL_FRAGMENT_SHADER, liquidFragmentShaderSrc);
    if (!liquidFragmentShader) {
        printf("Failed to compile fragment shader, falling back to fixed pipeline\n");
        useShaderLiquid = 0;
        return;
    }
    
    // 创建程序并链接
    liquidShaderProgram = glCreateProgram();
    glAttachShader(liquidShaderProgram, liquidVertexShader);
    glAttachShader(liquidShaderProgram, liquidFragmentShader);
    glLinkProgram(liquidShaderProgram);
    
    GLint success;
    glGetProgramiv(liquidShaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(liquidShaderProgram, 512, NULL, infoLog);
        printf("Shader program link error: %s\n", infoLog);
        useShaderLiquid = 0;
        return;
    }
    
    // 获取uniform位置
    uLiquidTopY = glGetUniformLocation(liquidShaderProgram, "uLiquidTopY");
    uLiquidBottomY = glGetUniformLocation(liquidShaderProgram, "uLiquidBottomY");
    uLiquidColor = glGetUniformLocation(liquidShaderProgram, "uLiquidColor");
    
    printf("Liquid shader initialized successfully!\n");
}

// ========== Game State ==========
enum GameState { STATE_MENU, STATE_RULES, STATE_PLAYING, STATE_WON, STATE_DEMO };
GameState gameState = STATE_MENU;
int menuSelection = 0;
int currentLevel = 1;

// ========== Demo Mode ==========
int demoMode = 0;              // 是否在Demo模式
int demoStep = 0;              // 当前Demo步骤
float demoTimer = 0;           // Demo计时器

// Demo关卡: 瓶0=[1,2,1,2], 瓶1=[2,1,2,1], 瓶2=空, 瓶3=空
// 手动推演解法：
// 初始: 0=[1,2,1,2], 1=[2,1,2,1], 2=[], 3=[]
// Step1: 0->2  0=[1,2,1], 1=[2,1,2,1], 2=[2], 3=[]
// Step2: 1->3  0=[1,2,1], 1=[2,1,2], 2=[2], 3=[1]
// Step3: 0->3  0=[1,2], 1=[2,1,2], 2=[2], 3=[1,1]
// Step4: 1->2  0=[1,2], 1=[2,1], 2=[2,2], 3=[1,1]
// Step5: 0->2  0=[1], 1=[2,1], 2=[2,2,2], 3=[1,1]
// Step6: 1->3  0=[1], 1=[2], 2=[2,2,2], 3=[1,1,1]
// Step7: 0->3  0=[], 1=[2], 2=[2,2,2], 3=[1,1,1,1] 瓶3满了!
// Step8: 1->2  0=[], 1=[], 2=[2,2,2,2], 3=[1,1,1,1] 完成!

int demoSolution[][2] = {
    {0, 2},  // Step1
    {1, 3},  // Step2
    {0, 3},  // Step3
    {1, 2},  // Step4
    {0, 2},  // Step5
    {1, 3},  // Step6
    {0, 3},  // Step7
    {1, 2}   // Step8
};
int demoSolutionLen = 8;

// ========== Colors ==========
float liquidColors[][4] = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {0.95f, 0.22f, 0.22f, 0.92f},
    {0.22f, 0.92f, 0.35f, 0.92f},
    {0.28f, 0.48f, 0.95f, 0.92f},
    {1.0f, 0.88f, 0.18f, 0.92f},
    {0.85f, 0.28f, 0.92f, 0.92f},
    {1.0f, 0.52f, 0.12f, 0.92f},
    {0.18f, 0.88f, 0.88f, 0.92f},
};

// ========== Bottle ==========
struct Bottle {
    int layers[MAX_LAYERS];
    int count;
    float x, y, z;
    float visualLiquid;
    float offsetY;
    float tiltAngle;
    int tiltDir;
    float moveX;
    float targetMoveX;
    float glowIntensity;    // 发光强度（用于悬停效果）
    float pulsePhase;       // 脉冲动画相位
};

Bottle bottles[MAX_BOTTLES];
int numBottles = 5;
int selectedBottle = -1;
int hoveredBottle = -1;  // 鼠标悬停的瓶子
int lastMouseX = 0, lastMouseY = 0;  // 鼠标位置

// ========== 鼠标交互效果 ==========
float hoverGlowTime = 0;           // 悬停发光动画时间
float selectPulseTime = 0;         // 选中脉冲动画时间
int showTooltip = 1;               // 是否显示悬停提示
int highlightValidTargets = 1;     // 是否高亮有效目标

// Error message
char errorMsg[64] = "";
float errorTimer = 0;

void showError(const char* msg) {
    strcpy(errorMsg, msg);
    errorTimer = 2.0f;  // Show for 2 seconds
}

int animating = 0;
int animSrc = -1, animTgt = -1;
int animPhase = 0;
float pourAmt = 0;
int pourColor = 0;
int pourTotal = 0, poured = 0;

// ========== 增强评分系统 ==========
int playerMoves = 0;           // 玩家步数
int bestMoves[] = {8, 12, 18}; // 各难度最佳步数 (Easy, Medium, Hard)
float gameStartTime = 0;       // 游戏开始时间
float gameEndTime = 0;         // 游戏结束时间
int perfectPours = 0;          // 完美倒水次数（一次倒完同色）
int comboPours = 0;            // 连续有效操作次数
int maxCombo = 0;              // 最大连击数
int undoCount = 0;             // 撤销次数
int totalLayersMoved = 0;      // 总共移动的层数

// 评分细节
struct ScoreBreakdown {
    int baseScore;         // 基础分（根据步数）
    int timeBonus;         // 时间奖励
    int comboBonus;        // 连击奖励
    int perfectBonus;      // 完美倒水奖励
    int efficiencyBonus;   // 效率奖励
    int penalty;           // 扣分
    int totalScore;        // 总分
    char grade[16];        // 评级
};
ScoreBreakdown lastScore;

// 计算详细评分
ScoreBreakdown calculateScore() {
    ScoreBreakdown score = {0, 0, 0, 0, 0, 0, 0, ""};
    int best = bestMoves[currentLevel - 1];
    
    // 基础分：根据步数与最佳步数的差距
    int moveDiff = playerMoves - best;
    if (moveDiff <= 0) {
        score.baseScore = 50;  // 达到或超过最佳
    } else if (moveDiff <= 3) {
        score.baseScore = 45;
    } else if (moveDiff <= 6) {
        score.baseScore = 35;
    } else if (moveDiff <= 10) {
        score.baseScore = 25;
    } else {
        score.baseScore = 15;
    }
    
    // 时间奖励：根据完成时间
    float timeTaken = gameEndTime - gameStartTime;
    if (timeTaken < 30.0f) {
        score.timeBonus = 20;  // 30秒内完成
    } else if (timeTaken < 60.0f) {
        score.timeBonus = 15;
    } else if (timeTaken < 120.0f) {
        score.timeBonus = 10;
    } else {
        score.timeBonus = 5;
    }
    
    // 连击奖励
    score.comboBonus = maxCombo * 2;
    if (score.comboBonus > 15) score.comboBonus = 15;
    
    // 完美倒水奖励（一次倒完同色层）
    score.perfectBonus = perfectPours * 3;
    if (score.perfectBonus > 15) score.perfectBonus = 15;
    
    // 效率奖励：移动层数与步数的比率
    if (playerMoves > 0) {
        float efficiency = (float)totalLayersMoved / playerMoves;
        if (efficiency >= 2.0f) {
            score.efficiencyBonus = 10;
        } else if (efficiency >= 1.5f) {
            score.efficiencyBonus = 7;
        } else {
            score.efficiencyBonus = 3;
        }
    }
    
    // 扣分：撤销操作
    score.penalty = undoCount * 5;
    if (score.penalty > 20) score.penalty = 20;
    
    // 总分
    score.totalScore = score.baseScore + score.timeBonus + score.comboBonus + 
                       score.perfectBonus + score.efficiencyBonus - score.penalty;
    if (score.totalScore < 0) score.totalScore = 0;
    if (score.totalScore > 100) score.totalScore = 100;
    
    // 评级
    if (score.totalScore >= 95) strcpy(score.grade, "S - Perfect!");
    else if (score.totalScore >= 85) strcpy(score.grade, "A - Excellent!");
    else if (score.totalScore >= 70) strcpy(score.grade, "B - Good!");
    else if (score.totalScore >= 50) strcpy(score.grade, "C - Not Bad");
    else strcpy(score.grade, "D - Keep Trying");
    
    return score;
}

float camX = 22, camY = 0, camDist = 8.0f;
int winW = 1280, winH = 800;

const float B_RAD = 0.36f;
const float B_HGT = 1.8f;
const float L_HGT = B_HGT / MAX_LAYERS;
const float BOTTLE_SPACING = 1.1f;
const float NECK_RAD = B_RAD * 0.52f;
const float BOTTLE_TOP = B_HGT + 0.35f;

// 瓶子屏幕坐标缓存（每帧更新一次）
float bottleScreenX[MAX_BOTTLES];
float bottleScreenTopY[MAX_BOTTLES];
float bottleScreenBottomY[MAX_BOTTLES];

// ========== AI Chat Agent ==========
aiagent::AIAgent g_aiAgent;
bool g_chatOpen = false;           // 聊天面板是否打开
char g_chatInput[256] = "";        // 输入缓冲区
int g_chatInputLen = 0;            // 输入长度
float g_chatScrollY = 0;           // 消息滚动偏移
bool g_aiInitialized = false;      // AI是否初始化成功

// 颜色名称映射（用于状态序列化）
const char* colorNames[] = { "空", "红", "绿", "蓝", "黄", "紫", "橙", "青", "粉", "棕" };

// ========== 液体物理模拟 ==========
// 核心原理：
// 1. 液体体积恒定
// 2. 液面在世界坐标中始终保持水平（垂直于重力方向）
// 3. 当瓶子倾斜时，液体重新分布
// 4. 瓶子是圆柱形，液体也是圆柱形

// 计算倾斜瓶子中液体的真实物理形状
// tiltAngle: 瓶子倾斜角度（度），0=直立，90=水平
// liquidVolume: 液体体积（以"层"为单位，1层 = L_HGT高度的圆柱）
// 返回：液体在瓶子局部坐标系中的高度范围

struct PhysicsLiquid {
    float lowY;      // 液体最低点Y（瓶子局部坐标）
    float highY;     // 液体最高点Y（瓶子局部坐标）
    float centerY;   // 液面中心Y
    int isPouring;   // 是否正在溢出
};

// 安全的 tan 函数，防止接近 90° 时产生无穷大
static float safeTan(float radians) {
    // 将角度限制在安全范围内（最大约 89.5°）
    float maxRad = 89.5f * PI / 180.0f;
    if (radians > maxRad) radians = maxRad;
    return tanf(radians);
}

// 真实物理计算：给定倾斜角度和液体体积，计算液体形状
PhysicsLiquid calcPhysicsLiquid(float tiltAngleDeg, float liquidVolume) {
    PhysicsLiquid result = {0.02f, 0.02f, 0.02f, 0};
    
    if (liquidVolume < 0.01f) return result;
    
    float r = B_RAD * 0.86f;  // 液体半径（略小于瓶子半径）
    float clampedAngle = fabsf(tiltAngleDeg);
    if (clampedAngle > 89.5f) clampedAngle = 89.5f;
    float tiltRad = clampedAngle * PI / 180.0f;
    
    // 液体体积 = π * r² * h（直立时的高度）
    float normalHeight = liquidVolume * L_HGT;
    
    if (tiltRad < 0.01f) {
        // 直立状态
        result.lowY = 0.02f;
        result.highY = normalHeight + 0.02f;
        result.centerY = (result.lowY + result.highY) / 2.0f;
        return result;
    }
    
    // 倾斜时，液面在世界坐标中是水平的
    // 在瓶子局部坐标中，液面是一个倾斜的平面
    // 液面方程（瓶子局部坐标）：y = centerY + x * tan(tiltAngle)
    // 其中 x 是沿倾斜方向的坐标
    
    float tanAngle = safeTan(tiltRad);
    
    // 液面在瓶壁处的高度差 = 2 * r * tan(angle)
    float heightDiff = 2.0f * r * tanAngle;
    
    // 体积守恒：液体体积 = π * r² * normalHeight
    // 倾斜时，液体仍然是这个体积，只是形状变了
    // 对于圆柱形瓶子，倾斜时液体的平均高度仍然是 normalHeight
    
    // 液面中心高度
    float centerY = normalHeight;
    
    // 低侧和高侧的高度
    float lowY = centerY - heightDiff / 2.0f;
    float highY = centerY + heightDiff / 2.0f;
    
    // 边界条件1：低侧不能低于瓶底
    if (lowY < 0.02f) {
        // 液体"堆积"在低侧，需要重新计算
        // 体积守恒，但低侧被瓶底限制
        float overflow = 0.02f - lowY;
        lowY = 0.02f;
        // 高侧相应升高（近似处理）
        highY += overflow;
        centerY = (lowY + highY) / 2.0f;
    }
    
    // 边界条件2：高侧超过瓶口时开始溢出
    if (highY > B_HGT) {
        result.isPouring = 1;
        highY = B_HGT;  // 限制在瓶口
    }
    
    result.lowY = lowY;
    result.highY = highY;
    result.centerY = centerY;
    
    return result;
}

// 绘制物理正确的倾斜液体（单色）
// 在瓶子局部坐标系中绘制，液面在世界坐标中保持水平
void drawPhysicsLiquid(float tiltAngleDeg, int tiltDir, float liquidVolume, int color) {
    if (color <= 0 || liquidVolume < 0.01f) return;
    
    float r = B_RAD * 0.86f;
    float clampedAngle = fabsf(tiltAngleDeg);
    if (clampedAngle > 89.5f) clampedAngle = 89.5f;
    float tiltRad = clampedAngle * PI / 180.0f;
    float tanAngle = safeTan(tiltRad);
    float normalHeight = liquidVolume * L_HGT;
    
    // 设置液体材质
    float liqSpec[] = {0.6f, 0.6f, 0.65f, 1.0f};
    float liqShin[] = {50.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, liqSpec);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, liqShin);
    
    int segments = 32;
    
    // 计算物理形状
    PhysicsLiquid phys = calcPhysicsLiquid(tiltAngleDeg, liquidVolume);
    
    // ===== 绘制液体侧面（圆柱壁） =====
    glColor4f(liquidColors[color][0] * 0.95f, 
             liquidColors[color][1] * 0.95f,
             liquidColors[color][2] * 0.95f,
             liquidColors[color][3]);
    
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * 2.0f * PI;
        float x = r * cosf(angle);
        float z = r * sinf(angle);
        
        // 底部：瓶底或液体最低点
        float yBottom = 0.02f;
        
        // 顶部：液面高度，根据x位置变化
        // 液面方程：y = centerY + x * tan(tiltAngle) * tiltDir
        // 注意：tiltDir决定倾斜方向
        float yTop = phys.centerY + x * tanAngle * tiltDir;
        
        // 限制在合理范围内
        if (yTop < 0.02f) yTop = 0.02f;
        if (yTop > B_HGT) yTop = B_HGT;
        
        glNormal3f(x / r, 0, z / r);
        glVertex3f(x, yBottom, z);
        glVertex3f(x, yTop, z);
    }
    glEnd();
    
    // ===== 绘制底面 =====
    glColor4f(liquidColors[color][0] * 0.8f, 
             liquidColors[color][1] * 0.8f,
             liquidColors[color][2] * 0.8f,
             liquidColors[color][3]);
    glBegin(GL_TRIANGLE_FAN);
    glNormal3f(0, -1, 0);
    glVertex3f(0, 0.02f, 0);
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * 2.0f * PI;
        glVertex3f(r * cosf(angle), 0.02f, r * sinf(angle));
    }
    glEnd();
    
    // ===== 绘制倾斜的液面（关键！）=====
    // 液面在世界坐标中是水平的，但在瓶子局部坐标中是倾斜的
    glColor4f(liquidColors[color][0] * 1.1f, 
             liquidColors[color][1] * 1.1f,
             liquidColors[color][2] * 1.1f,
             liquidColors[color][3] * 0.95f);
    
    glBegin(GL_TRIANGLE_FAN);
    // 液面法线（在瓶子局部坐标中）
    float nx = sinf(tiltRad) * tiltDir;
    float ny = cosf(tiltRad);
    glNormal3f(-nx, ny, 0);
    
    // 液面中心点
    glVertex3f(0, phys.centerY, 0);
    
    // 液面边缘
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * 2.0f * PI;
        float x = r * cosf(angle);
        float z = r * sinf(angle);
        float y = phys.centerY + x * tanAngle * tiltDir;
        if (y < 0.02f) y = 0.02f;
        if (y > B_HGT) y = B_HGT;
        glVertex3f(x, y, z);
    }
    glEnd();
    
    // ===== 添加液面高光 =====
    glDisable(GL_LIGHTING);
    glColor4f(1.0f, 1.0f, 1.0f, 0.25f);
    float hlX = -r * 0.3f;
    float hlY = phys.centerY + hlX * tanAngle * tiltDir;
    glBegin(GL_TRIANGLES);
    glVertex3f(hlX, hlY + 0.01f, -r * 0.15f);
    glVertex3f(hlX - 0.08f, hlY + 0.01f, -r * 0.05f);
    glVertex3f(hlX + 0.04f, hlY + 0.01f, -r * 0.25f);
    glEnd();
    glEnable(GL_LIGHTING);
    
    // 恢复默认材质
    float defSpec[] = {0.75f, 0.75f, 0.8f, 1};
    float defShin[] = {70.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, defSpec);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, defShin);
}

// 绘制物理正确的多层倾斜液体
// 每层液体都有自己的颜色，液面保持水平
void drawPhysicsLiquidMultiLayer(float tiltAngleDeg, int tiltDir, int* layers, int layerCount, float visualLiquid) {
    if (layerCount <= 0 || visualLiquid < 0.01f) return;
    
    float r = B_RAD * 0.86f;
    float clampedAngle = fabsf(tiltAngleDeg);
    if (clampedAngle > 89.5f) clampedAngle = 89.5f;
    float tiltRad = clampedAngle * PI / 180.0f;
    float tanAngle = safeTan(tiltRad);
    
    // 设置液体材质
    float liqSpec[] = {0.6f, 0.6f, 0.65f, 1.0f};
    float liqShin[] = {50.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, liqSpec);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, liqShin);
    
    int segments = 32;
    
    // 计算要绘制的层数
    int fullLayers = (int)visualLiquid;
    float partialLayer = visualLiquid - fullLayers;
    if (fullLayers > layerCount) fullLayers = layerCount;
    
    // 从底部开始，逐层绘制
    // 每层的底部Y和顶部Y都需要根据倾斜角度计算
    
    for (int layerIdx = 0; layerIdx < fullLayers || (layerIdx == fullLayers && partialLayer > 0.01f); layerIdx++) {
        if (layerIdx >= layerCount) break;
        
        int color = layers[layerIdx];
        if (color <= 0) continue;
        
        // 该层的体积（以层为单位）
        float layerVolume = 1.0f;
        if (layerIdx == fullLayers && partialLayer > 0.01f) {
            layerVolume = partialLayer;
        }
        if (layerIdx >= fullLayers && partialLayer < 0.01f) break;
        
        // 该层底部的累计体积
        float bottomVolume = (float)layerIdx;
        // 该层顶部的累计体积
        float topVolume = bottomVolume + layerVolume;
        
        // 计算该层底部和顶部的Y坐标（考虑倾斜）
        // 底部Y = bottomVolume * L_HGT（中心位置）
        // 顶部Y = topVolume * L_HGT（中心位置）
        float bottomCenterY = bottomVolume * L_HGT + 0.02f;
        float topCenterY = topVolume * L_HGT + 0.02f;
        
        // 是否是最顶层
        int isTopLayer = (layerIdx == fullLayers - 1 && partialLayer < 0.01f) || 
                        (layerIdx == fullLayers && partialLayer > 0.01f);
        
        // ===== 绘制该层液体侧面 =====
        glColor4f(liquidColors[color][0] * 0.95f, 
                 liquidColors[color][1] * 0.95f,
                 liquidColors[color][2] * 0.95f,
                 liquidColors[color][3]);
        
        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= segments; i++) {
            float angle = (float)i / segments * 2.0f * PI;
            float x = r * cosf(angle);
            float z = r * sinf(angle);
            
            // 底部Y（考虑倾斜）
            float yBottom = bottomCenterY + x * tanAngle * tiltDir;
            if (yBottom < 0.02f) yBottom = 0.02f;
            if (yBottom > B_HGT) yBottom = B_HGT;
            
            // 顶部Y（考虑倾斜）
            float yTop = topCenterY + x * tanAngle * tiltDir;
            if (yTop < 0.02f) yTop = 0.02f;
            if (yTop > B_HGT) yTop = B_HGT;
            
            // 确保顶部不低于底部
            if (yTop < yBottom) yTop = yBottom;
            
            glNormal3f(x / r, 0, z / r);
            glVertex3f(x, yBottom, z);
            glVertex3f(x, yTop, z);
        }
        glEnd();
        
        // ===== 绘制该层底面（只有第一层需要） =====
        if (layerIdx == 0) {
            glColor4f(liquidColors[color][0] * 0.8f, 
                     liquidColors[color][1] * 0.8f,
                     liquidColors[color][2] * 0.8f,
                     liquidColors[color][3]);
            glBegin(GL_TRIANGLE_FAN);
            glNormal3f(0, -1, 0);
            glVertex3f(0, 0.02f, 0);
            for (int i = 0; i <= segments; i++) {
                float angle = (float)i / segments * 2.0f * PI;
                glVertex3f(r * cosf(angle), 0.02f, r * sinf(angle));
            }
            glEnd();
        }
        
        // ===== 绘制该层顶面（液面） =====
        // 只有最顶层才绘制液面
        if (isTopLayer) {
            glColor4f(liquidColors[color][0] * 1.1f, 
                     liquidColors[color][1] * 1.1f,
                     liquidColors[color][2] * 1.1f,
                     liquidColors[color][3] * 0.95f);
            
            glBegin(GL_TRIANGLE_FAN);
            float nx = sinf(tiltRad) * tiltDir;
            float ny = cosf(tiltRad);
            glNormal3f(-nx, ny, 0);
            
            glVertex3f(0, topCenterY, 0);
            
            for (int i = 0; i <= segments; i++) {
                float angle = (float)i / segments * 2.0f * PI;
                float x = r * cosf(angle);
                float z = r * sinf(angle);
                float y = topCenterY + x * tanAngle * tiltDir;
                if (y < 0.02f) y = 0.02f;
                if (y > B_HGT) y = B_HGT;
                glVertex3f(x, y, z);
            }
            glEnd();
            
            // 液面高光
            glDisable(GL_LIGHTING);
            glColor4f(1.0f, 1.0f, 1.0f, 0.25f);
            float hlX = -r * 0.3f;
            float hlY = topCenterY + hlX * tanAngle * tiltDir;
            glBegin(GL_TRIANGLES);
            glVertex3f(hlX, hlY + 0.01f, -r * 0.15f);
            glVertex3f(hlX - 0.08f, hlY + 0.01f, -r * 0.05f);
            glVertex3f(hlX + 0.04f, hlY + 0.01f, -r * 0.25f);
            glEnd();
            glEnable(GL_LIGHTING);
        }
    }
    
    // 恢复默认材质
    float defSpec[] = {0.75f, 0.75f, 0.8f, 1};
    float defShin[] = {70.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, defSpec);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, defShin);
}

// ========== World-Space Liquid Rendering ==========
// 在世界坐标系中绘制液体，液面始终保持水平
// 核心原理：液体不跟随瓶子旋转，而是根据瓶子位置和倾斜角度计算正确的形状

// 绘制世界坐标系中的液体（液面水平）
void drawWorldSpaceLiquid(float bottleX, float bottleY, float bottleZ,
                          float tiltAngleDeg, int tiltDir,
                          int* layers, int layerCount, float visualLiquid) {
    if (layerCount <= 0 || visualLiquid < 0.01f) return;
    
    float r = B_RAD * 0.86f;
    float tiltRad = fabsf(tiltAngleDeg) * PI / 180.0f;
    float cosT = cosf(tiltRad);
    float sinT = sinf(tiltRad);
    
    // 设置液体材质
    float liqSpec[] = {0.6f, 0.6f, 0.65f, 1.0f};
    float liqShin[] = {50.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, liqSpec);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, liqShin);
    
    int segments = 32;
    
    // 计算要绘制的层数
    int fullLayers = (int)visualLiquid;
    float partialLayer = visualLiquid - fullLayers;
    if (fullLayers > layerCount) fullLayers = layerCount;
    
    // 瓶子倾斜时，瓶底中心的世界坐标
    // 当瓶子绕Z轴旋转时，瓶底中心位置不变（旋转中心在瓶底）
    float baseCenterX = bottleX;
    float baseCenterY = bottleY;
    float baseCenterZ = bottleZ;
    
    // 逐层绘制液体
    for (int layerIdx = 0; layerIdx < fullLayers || (layerIdx == fullLayers && partialLayer > 0.01f); layerIdx++) {
        if (layerIdx >= layerCount) break;
        
        int color = layers[layerIdx];
        if (color <= 0) continue;
        
        // 该层的体积
        float layerVolume = 1.0f;
        if (layerIdx == fullLayers && partialLayer > 0.01f) {
            layerVolume = partialLayer;
        }
        if (layerIdx >= fullLayers && partialLayer < 0.01f) break;
        
        // 该层在瓶子局部坐标中的Y范围（直立时）
        float bottomLocalY = (float)layerIdx * L_HGT + 0.02f;
        float topLocalY = bottomLocalY + layerVolume * L_HGT;
        
        // 转换到世界坐标
        // 当瓶子倾斜时，局部坐标系中的点 (0, y, 0) 变换到世界坐标：
        // worldX = bottleX + y * sin(tilt) * (-tiltDir)
        // worldY = bottleY + y * cos(tilt)
        // worldZ = bottleZ
        
        // 液体底部中心的世界坐标
        float bottomWorldX = baseCenterX + bottomLocalY * sinT * (-tiltDir);
        float bottomWorldY = baseCenterY + bottomLocalY * cosT;
        
        // 液体顶部中心的世界坐标
        float topWorldX = baseCenterX + topLocalY * sinT * (-tiltDir);
        float topWorldY = baseCenterY + topLocalY * cosT;
        
        // 是否是最顶层
        int isTopLayer = (layerIdx == fullLayers - 1 && partialLayer < 0.01f) || 
                        (layerIdx == fullLayers && partialLayer > 0.01f);
        
        // ===== 绘制液体侧面 =====
        // 液体是一个倾斜的圆柱体，但液面是水平的
        glColor4f(liquidColors[color][0] * 0.95f, 
                 liquidColors[color][1] * 0.95f,
                 liquidColors[color][2] * 0.95f,
                 liquidColors[color][3]);
        
        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= segments; i++) {
            float angle = (float)i / segments * 2.0f * PI;
            // 圆柱体的局部坐标（在瓶子坐标系中）
            float localX = r * cosf(angle);
            float localZ = r * sinf(angle);
            
            // 底部点的世界坐标
            // 旋转变换：绕Z轴旋转
            float bx = baseCenterX + localX * cosT + bottomLocalY * sinT * (-tiltDir);
            float by = baseCenterY - localX * sinT * (-tiltDir) + bottomLocalY * cosT;
            float bz = baseCenterZ + localZ;
            
            // 顶部点的世界坐标
            float tx = baseCenterX + localX * cosT + topLocalY * sinT * (-tiltDir);
            float ty = baseCenterY - localX * sinT * (-tiltDir) + topLocalY * cosT;
            float tz = baseCenterZ + localZ;
            
            // 法线（在世界坐标系中）
            float nx = localX / r * cosT;
            float ny = -localX / r * sinT * (-tiltDir);
            float nz = localZ / r;
            
            glNormal3f(nx, ny, nz);
            glVertex3f(bx, by, bz);
            glVertex3f(tx, ty, tz);
        }
        glEnd();
        
        // ===== 绘制底面（只有第一层需要） =====
        if (layerIdx == 0) {
            glColor4f(liquidColors[color][0] * 0.8f, 
                     liquidColors[color][1] * 0.8f,
                     liquidColors[color][2] * 0.8f,
                     liquidColors[color][3]);
            glBegin(GL_TRIANGLE_FAN);
            // 底面法线（指向下方，考虑旋转）
            glNormal3f(sinT * (-tiltDir), -cosT, 0);
            glVertex3f(bottomWorldX, bottomWorldY, baseCenterZ);
            for (int i = 0; i <= segments; i++) {
                float angle = (float)i / segments * 2.0f * PI;
                float localX = r * cosf(angle);
                float localZ = r * sinf(angle);
                float wx = baseCenterX + localX * cosT + 0.02f * sinT * (-tiltDir);
                float wy = baseCenterY - localX * sinT * (-tiltDir) + 0.02f * cosT;
                float wz = baseCenterZ + localZ;
                glVertex3f(wx, wy, wz);
            }
            glEnd();
        }
        
        // ===== 绘制顶面（液面） =====
        if (isTopLayer) {
            glColor4f(liquidColors[color][0] * 1.1f, 
                     liquidColors[color][1] * 1.1f,
                     liquidColors[color][2] * 1.1f,
                     liquidColors[color][3] * 0.95f);
            glBegin(GL_TRIANGLE_FAN);
            // 顶面法线（指向上方，考虑旋转）
            glNormal3f(-sinT * (-tiltDir), cosT, 0);
            glVertex3f(topWorldX, topWorldY, baseCenterZ);
            for (int i = 0; i <= segments; i++) {
                float angle = (float)i / segments * 2.0f * PI;
                float localX = r * cosf(angle);
                float localZ = r * sinf(angle);
                float wx = baseCenterX + localX * cosT + topLocalY * sinT * (-tiltDir);
                float wy = baseCenterY - localX * sinT * (-tiltDir) + topLocalY * cosT;
                float wz = baseCenterZ + localZ;
                glVertex3f(wx, wy, wz);
            }
            glEnd();
            
            // 液面高光
            glDisable(GL_LIGHTING);
            glColor4f(1.0f, 1.0f, 1.0f, 0.25f);
            float hlLocalX = -r * 0.3f;
            float hlX = baseCenterX + hlLocalX * cosT + topLocalY * sinT * (-tiltDir);
            float hlY = baseCenterY - hlLocalX * sinT * (-tiltDir) + topLocalY * cosT;
            glBegin(GL_TRIANGLES);
            glVertex3f(hlX, hlY + 0.01f, baseCenterZ - r * 0.15f);
            glVertex3f(hlX - 0.08f * cosT, hlY + 0.08f * sinT * (-tiltDir) + 0.01f, baseCenterZ - r * 0.05f);
            glVertex3f(hlX + 0.04f * cosT, hlY - 0.04f * sinT * (-tiltDir) + 0.01f, baseCenterZ - r * 0.25f);
            glEnd();
            glEnable(GL_LIGHTING);
        }
    }
    
    // 恢复默认材质
    float defSpec2[] = {0.75f, 0.75f, 0.8f, 1};
    float defShin2[] = {70.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, defSpec2);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, defShin2);
}

// 绘制瓶颈中流动的液体效果
// 当瓶子倾斜倾倒时，液体在颈部收束流出，增强真实感
void drawNeckFlow(float bottleX, float bottleY, float bottleZ,
                  float tiltAngleDeg, int tiltDir, int color, float flowIntensity) {
    if (color <= 0 || flowIntensity < 0.01f) return;
    
    float tiltRad = fabsf(tiltAngleDeg) * PI / 180.0f;
    float cosT = cosf(tiltRad);
    float sinT = sinf(tiltRad);
    
    // 瓶颈参数
    float neckR = NECK_RAD * 0.7f;  // 液体在颈中的半径
    float neckBottom = B_HGT - 0.1f;  // 颈部起点（瓶身到瓶颈过渡）
    float neckTop = BOTTLE_TOP;  // 瓶口
    
    int segments = 10;
    int radialSegs = 10;
    
    // 设置半透明颈部液体材质
    float liqSpec[] = {0.7f, 0.7f, 0.75f, 1.0f};
    float liqShin[] = {60.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, liqSpec);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, liqShin);
    
    static float neckFlowTime = 0;
    neckFlowTime += 0.08f;
    if (neckFlowTime > 100.0f) neckFlowTime = 0;
    
    // 颈部液体从宽变窄，模拟液体收束
    for (int i = 0; i < segments; i++) {
        float t1 = (float)i / segments;
        float t2 = (float)(i + 1) / segments;
        
        float localY1 = neckBottom + t1 * (neckTop - neckBottom);
        float localY2 = neckBottom + t2 * (neckTop - neckBottom);
        
        // 半径从瓶身半径收束到颈部半径
        float bodyR = B_RAD * 0.86f;
        float r1 = bodyR + t1 * (neckR - bodyR);  // 线性插值
        float r2 = bodyR + t2 * (neckR - bodyR);
        
        // 流动强度影响液体填充度（越满表示流量越大）
        float fill = flowIntensity * (0.5f + 0.5f * (1.0f - t1));
        r1 *= fill;
        r2 *= fill;
        
        // 流动波纹
        float wave = 1.0f + 0.1f * sinf(neckFlowTime * 5.0f - t1 * 8.0f);
        r1 *= wave;
        r2 *= wave;
        
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= radialSegs; j++) {
            float angle = (float)j / radialSegs * 2.0f * PI;
            float lx = cosf(angle);
            float lz = sinf(angle);
            
            // 转换到世界坐标
            float wx1 = bottleX + lx * r1 * cosT + localY1 * sinT * (-tiltDir);
            float wy1 = bottleY - lx * r1 * sinT * (-tiltDir) + localY1 * cosT;
            float wz1 = bottleZ + lz * r1;
            
            float wx2 = bottleX + lx * r2 * cosT + localY2 * sinT * (-tiltDir);
            float wy2 = bottleY - lx * r2 * sinT * (-tiltDir) + localY2 * cosT;
            float wz2 = bottleZ + lz * r2;
            
            float brightness = 0.8f + 0.2f * fabsf(lx);
            glColor4f(liquidColors[color][0] * brightness,
                     liquidColors[color][1] * brightness,
                     liquidColors[color][2] * brightness,
                     liquidColors[color][3] * fill * 0.85f);
            glNormal3f(lx, 0, lz);
            glVertex3f(wx1, wy1, wz1);
            glVertex3f(wx2, wy2, wz2);
        }
        glEnd();
    }
    
    // 恢复默认材质
    float defSpec3[] = {0.75f, 0.75f, 0.8f, 1};
    float defShin3[] = {70.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, defSpec3);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, defShin3);
}



// ========== Text ==========
void drawText(float x, float y, const char* text, void* font) {
    glRasterPos2f(x, y);
    for (; *text; text++) glutBitmapCharacter(font, *text);
}

void drawTextCentered(float y, const char* text, void* font) {
    int len = glutBitmapLength(font, (const unsigned char*)text);
    float x = (winW - len) / 2.0f;
    drawText(x, y, text, font);
}

// ========== Menu ==========
void drawMenu() {
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, winW, 0, winH);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor4f(0.08f, 0.09f, 0.12f, 1);
    glBegin(GL_QUADS);
    glVertex2f(0, 0); glVertex2f((float)winW, 0);
    glVertex2f((float)winW, (float)winH); glVertex2f(0, (float)winH);
    glEnd();

    glColor3f(0.4f, 0.85f, 0.95f);
    drawTextCentered((float)(winH - 120), "WATER SORT PUZZLE", GLUT_BITMAP_TIMES_ROMAN_24);
    glColor3f(0.6f, 0.65f, 0.7f);
    drawTextCentered((float)(winH - 160), "Sort colored liquids into bottles", GLUT_BITMAP_HELVETICA_18);

    const char* items[] = {"Easy (3 colors)", "Medium (4 colors)", "Hard (5 colors)", "Watch Demo", "How to Play"};
    float startY = (float)(winH / 2 + 80);
    for (int i = 0; i < 5; i++) {
        float y = startY - i * 50;
        if (i == menuSelection) {
            glColor4f(0.3f, 0.6f, 0.8f, 0.3f);
            glBegin(GL_QUADS);
            glVertex2f((float)(winW/2 - 120), y - 8);
            glVertex2f((float)(winW/2 + 120), y - 8);
            glVertex2f((float)(winW/2 + 120), y + 28);
            glVertex2f((float)(winW/2 - 120), y + 28);
            glEnd();
            glColor3f(1.0f, 0.95f, 0.7f);
        } else {
            glColor3f(0.8f, 0.82f, 0.85f);
        }
        drawTextCentered(y, items[i], GLUT_BITMAP_HELVETICA_18);
    }

    glColor3f(0.45f, 0.48f, 0.52f);
    drawTextCentered(70, "UP/DOWN to select, ENTER to confirm", GLUT_BITMAP_HELVETICA_12);

    // 主菜单底部居中显示学号姓名（GLUT最大字体）
    glColor3f(0.7f, 0.85f, 0.95f);
    drawTextCentered(30, "2023212167 Mo Renying", GLUT_BITMAP_TIMES_ROMAN_24);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

// ========== Rules ==========
void drawRules() {
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, winW, 0, winH);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor4f(0.08f, 0.09f, 0.12f, 1);
    glBegin(GL_QUADS);
    glVertex2f(0, 0); glVertex2f((float)winW, 0);
    glVertex2f((float)winW, (float)winH); glVertex2f(0, (float)winH);
    glEnd();

    glColor3f(0.4f, 0.85f, 0.95f);
    drawTextCentered((float)(winH - 100), "HOW TO PLAY", GLUT_BITMAP_TIMES_ROMAN_24);

    float y = (float)(winH - 170);
    glColor3f(0.85f, 0.87f, 0.9f);
    drawTextCentered(y, "Goal: Sort liquids so each bottle has one color", GLUT_BITMAP_HELVETICA_18);
    y -= 50;
    glColor3f(0.7f, 0.9f, 0.75f);
    drawTextCentered(y, "Rules:", GLUT_BITMAP_HELVETICA_18);
    y -= 35;
    glColor3f(0.8f, 0.82f, 0.85f);
    drawTextCentered(y, "1. Click a bottle to select it", GLUT_BITMAP_HELVETICA_18);
    y -= 35;
    drawTextCentered(y, "2. Click another bottle to pour", GLUT_BITMAP_HELVETICA_18);
    y -= 35;
    drawTextCentered(y, "3. Pour only onto same color or empty bottle", GLUT_BITMAP_HELVETICA_18);
    y -= 35;
    drawTextCentered(y, "4. Each bottle holds 4 layers max", GLUT_BITMAP_HELVETICA_18);
    y -= 35;
    drawTextCentered(y, "5. Connected same-color layers pour together", GLUT_BITMAP_HELVETICA_18);
    y -= 50;
    glColor3f(0.9f, 0.8f, 0.5f);
    drawTextCentered(y, "Controls: Arrows=Camera, R=Restart, M=Menu", GLUT_BITMAP_HELVETICA_18);
    
    glColor3f(0.5f, 0.75f, 0.9f);
    drawTextCentered(60, "Press ENTER to return", GLUT_BITMAP_HELVETICA_12);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

// ========== Drawing ==========
void drawLiquid(float h, int col) {
    if (col <= 0 || h < 0.01f) return;
    
    // 设置液体材质 - 半透明有光泽
    float liqSpec[] = {0.6f, 0.6f, 0.65f, 1.0f};
    float liqShin[] = {50.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, liqSpec);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, liqShin);
    
    glPushMatrix();
    glRotatef(-90, 1, 0, 0);
    float r = B_RAD * 0.86f;
    GLUquadric* q = gluNewQuadric();
    gluQuadricNormals(q, GLU_SMOOTH);
    
    // 液体主体 - 稍微加深颜色增加质感
    glColor4f(liquidColors[col][0] * 0.95f, 
             liquidColors[col][1] * 0.95f,
             liquidColors[col][2] * 0.95f,
             liquidColors[col][3]);
    gluCylinder(q, r, r, h, SLICES, 1);
    
    // 液体底面
    glPushMatrix();
    glRotatef(180, 1, 0, 0);
    glColor4f(liquidColors[col][0] * 0.8f, 
             liquidColors[col][1] * 0.8f,
             liquidColors[col][2] * 0.8f,
             liquidColors[col][3]);
    gluDisk(q, 0, r, SLICES, 1);
    glPopMatrix();
    
    // 液体顶面 - 更亮，模拟表面反射
    glTranslatef(0, 0, h);
    glColor4f(liquidColors[col][0] * 1.1f, 
             liquidColors[col][1] * 1.1f,
             liquidColors[col][2] * 1.1f,
             liquidColors[col][3] * 0.95f);
    gluDisk(q, 0, r, SLICES, 1);
    
    gluDeleteQuadric(q);
    glPopMatrix();
    
    // 添加液面高光
    glDisable(GL_LIGHTING);
    glPushMatrix();
    glTranslatef(0, h - 0.01f, 0);
    glColor4f(1.0f, 1.0f, 1.0f, 0.2f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(-r * 0.3f, 0.01f, -r * 0.2f);
    glVertex3f(-r * 0.5f, 0.01f, -r * 0.1f);
    glVertex3f(-r * 0.4f, 0.01f, -r * 0.35f);
    glVertex3f(-r * 0.2f, 0.01f, -r * 0.3f);
    glVertex3f(-r * 0.15f, 0.01f, -r * 0.15f);
    glEnd();
    glPopMatrix();
    glEnable(GL_LIGHTING);
    
    // 恢复默认材质
    float defSpec[] = {0.75f, 0.75f, 0.8f, 1};
    float defShin[] = {70.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, defSpec);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, defShin);
}

void drawGlass(int sel, int hovered) {
    // 获取当前瓶子的发光强度（用于动画效果）
    float glowAnim = 0.0f;
    float pulseAnim = 0.0f;
    
    // 计算动画效果
    if (sel) {
        pulseAnim = 0.15f * sinf(selectPulseTime * 4.0f);  // 选中时脉冲
    }
    if (hovered && !sel) {
        glowAnim = 0.1f * (0.5f + 0.5f * sinf(hoverGlowTime * 3.0f));  // 悬停时发光
    }
    
    // 设置玻璃材质属性
    float glassSpec[] = {1.0f, 1.0f, 1.0f, 1.0f};
    float glassShin[] = {96.0f};  // 高光泽度
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, glassSpec);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, glassShin);
    
    glPushMatrix();
    glRotatef(-90, 1, 0, 0);
    GLUquadric* q = gluNewQuadric();
    gluQuadricNormals(q, GLU_SMOOTH);
    
    // 瓶身 - 透明玻璃效果（带动画）
    if (sel) {
        float pulse = 0.95f + pulseAnim;
        glColor4f(1.0f * pulse, 0.95f * pulse, 0.7f, 0.35f + pulseAnim * 0.1f);
    } else if (hovered) {
        float glow = 0.9f + glowAnim;
        glColor4f(0.7f * glow, 0.9f * glow, 1.0f, 0.28f + glowAnim * 0.15f);
    } else {
        glColor4f(0.92f, 0.95f, 1.0f, 0.18f);
    }
    gluCylinder(q, B_RAD, B_RAD, B_HGT, SLICES, 1);
    
    // 瓶底
    glPushMatrix();
    glRotatef(180, 1, 0, 0);
    if (hovered && !sel) glColor4f(0.75f + glowAnim, 0.88f + glowAnim, 1.0f, 0.3f);
    else glColor4f(0.85f, 0.88f, 0.95f, 0.25f);
    gluDisk(q, 0, B_RAD, SLICES, 1);
    glPopMatrix();
    
    // 瓶颈过渡
    glTranslatef(0, 0, B_HGT);
    if (hovered && !sel) glColor4f(0.75f + glowAnim, 0.9f + glowAnim, 1.0f, 0.28f);
    else glColor4f(0.9f, 0.92f, 0.98f, 0.22f);
    gluCylinder(q, B_RAD, NECK_RAD, 0.1f, SLICES, 1);
    
    // 瓶颈
    glTranslatef(0, 0, 0.1f);
    if (hovered && !sel) glColor4f(0.72f + glowAnim, 0.88f + glowAnim, 1.0f, 0.26f);
    else glColor4f(0.88f, 0.9f, 0.96f, 0.2f);
    gluCylinder(q, NECK_RAD, NECK_RAD, 0.2f, SLICES, 1);
    
    // 瓶口边缘 - 高光环
    glTranslatef(0, 0, 0.2f);
    if (hovered && !sel) glColor4f(0.85f + glowAnim, 0.95f + glowAnim, 1.0f, 0.9f);
    else glColor4f(0.98f, 0.98f, 1.0f, 0.85f);
    gluCylinder(q, NECK_RAD * 1.12f, NECK_RAD * 1.12f, 0.045f, SLICES, 1);
    
    // 瓶口顶面
    glTranslatef(0, 0, 0.045f);
    if (hovered && !sel) glColor4f(0.8f + glowAnim, 0.92f + glowAnim, 1.0f, 0.75f);
    else glColor4f(0.95f, 0.96f, 1.0f, 0.7f);
    gluDisk(q, NECK_RAD * 0.5f, NECK_RAD * 1.12f, SLICES, 1);
    
    gluDeleteQuadric(q);
    glPopMatrix();
    
    // 绘制高光条纹（模拟玻璃反射）
    glDisable(GL_LIGHTING);
    glPushMatrix();
    
    // 左侧高光条 - 悬停时更亮
    float highlightAlpha = hovered ? (0.25f + glowAnim * 0.2f) : 0.15f;
    if (sel) highlightAlpha = 0.3f + pulseAnim * 0.1f;
    glColor4f(1.0f, 1.0f, 1.0f, highlightAlpha);
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= 20; i++) {
        float t = (float)i / 20;
        float y = t * B_HGT;
        float angle = -0.4f + t * 0.1f;
        float x = B_RAD * 0.92f * cosf(angle + 2.2f);
        float z = B_RAD * 0.92f * sinf(angle + 2.2f);
        float w = 0.03f * (1.0f - fabsf(t - 0.5f) * 0.5f);
        glVertex3f(x - w * 0.5f, y, z);
        glVertex3f(x + w * 0.5f, y, z + w);
    }
    glEnd();
    
    // 右侧次高光
    glColor4f(1.0f, 1.0f, 1.0f, hovered ? (0.15f + glowAnim * 0.1f) : 0.08f);
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= 20; i++) {
        float t = (float)i / 20;
        float y = t * B_HGT;
        float angle = 0.3f - t * 0.05f;
        float x = B_RAD * 0.9f * cosf(angle - 0.8f);
        float z = B_RAD * 0.9f * sinf(angle - 0.8f);
        float w = 0.025f * (1.0f - fabsf(t - 0.4f) * 0.6f);
        glVertex3f(x - w * 0.5f, y, z);
        glVertex3f(x + w * 0.5f, y, z + w);
    }
    glEnd();
    
    // 悬停或选中时添加发光边缘
    if (hovered || sel) {
        float r, g, b, a;
        if (sel) {
            r = 1.0f; g = 0.9f; b = 0.5f; a = 0.4f + pulseAnim * 0.2f;
        } else {
            r = 0.5f; g = 0.8f; b = 1.0f; a = 0.3f + glowAnim * 0.2f;
        }
        
        // 底部发光环
        glColor4f(r, g, b, a);
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 32; i++) {
            float angle = (float)i / 32 * 2.0f * PI;
            glVertex3f(B_RAD * 1.02f * cosf(angle), 0.01f, B_RAD * 1.02f * sinf(angle));
        }
        glEnd();
        
        // 顶部发光环
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 32; i++) {
            float angle = (float)i / 32 * 2.0f * PI;
            glVertex3f(B_RAD * 1.02f * cosf(angle), B_HGT, B_RAD * 1.02f * sinf(angle));
        }
        glEnd();
        
        // 垂直发光线
        glBegin(GL_LINES);
        for (int i = 0; i < 8; i++) {
            float angle = (float)i / 8 * 2.0f * PI;
            float x = B_RAD * 1.01f * cosf(angle);
            float z = B_RAD * 1.01f * sinf(angle);
            glVertex3f(x, 0.01f, z);
            glVertex3f(x, B_HGT, z);
        }
        glEnd();
    }
    
    glPopMatrix();
    glEnable(GL_LIGHTING);
    
    // 恢复默认材质
    float defSpec[] = {0.75f, 0.75f, 0.8f, 1};
    float defShin[] = {70.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, defSpec);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, defShin);
}

void drawBottle(int i, int pick) {
    Bottle* b = &bottles[i];
    glPushMatrix();
    glTranslatef(b->x + b->moveX, b->y + b->offsetY, b->z);
    
    if (pick) {
        // 拾取模式
        glDisable(GL_LIGHTING);
        glDisable(GL_BLEND);
        glColor3ub((i+1)*20, (i+1)*20, (i+1)*20);
        glPushMatrix();
        glRotatef(-90, 1, 0, 0);
        GLUquadric* q = gluNewQuadric();
        gluCylinder(q, B_RAD * 1.5f, B_RAD * 1.5f, B_HGT + 0.4f, 12, 1);
        glPushMatrix();
        glRotatef(180, 1, 0, 0);
        gluDisk(q, 0, B_RAD * 1.5f, 12, 1);
        glPopMatrix();
        glTranslatef(0, 0, B_HGT + 0.4f);
        gluDisk(q, 0, B_RAD * 1.5f, 12, 1);
        gluDeleteQuadric(q);
        glPopMatrix();
        glEnable(GL_LIGHTING);
        glEnable(GL_BLEND);
    } else {
        float vis = b->visualLiquid;
        
        // 绘制液体
        
        if (b->tiltAngle > 1.0f) {
            // 瓶子倾斜时，使用世界坐标系绘制液体
            // 液面始终保持水平
            
            // 准备颜色数组
            int tempLayers[MAX_LAYERS + 1];
            int tempCount = b->count;
            for (int j = 0; j < MAX_LAYERS; j++) {
                tempLayers[j] = (j < b->count) ? b->layers[j] : 0;
            }
            
            // 在Phase 3倾倒过程中，处理正在倒出的层
            if (animating && i == animSrc && animPhase == 3 && pourColor > 0) {
                int fullLayers = (int)vis;
                float partialLayer = vis - fullLayers;
                if (partialLayer > 0.01f && fullLayers < MAX_LAYERS && fullLayers >= b->count) {
                    tempLayers[fullLayers] = pourColor;
                    tempCount = fullLayers + 1;
                }
            }
            
            // 在世界坐标系中绘制液体（不受瓶子旋转影响）
            // 注意：当前已经在瓶子位置的变换矩阵中，需要先弹出
            glPopMatrix();  // 弹出瓶子位置变换
            
            // 在世界坐标系中绘制液体
            drawWorldSpaceLiquid(b->x + b->moveX, b->y + b->offsetY, b->z,
                                 b->tiltAngle, b->tiltDir,
                                 tempLayers, tempCount, vis);
            
            // 倾倒时绘制颈部液体流动效果
            if (animating && i == animSrc && animPhase == 3 && pourColor > 0) {
                float flowStrength = fminf(pourAmt * 1.5f, 1.0f);
                drawNeckFlow(b->x + b->moveX, b->y + b->offsetY, b->z,
                            b->tiltAngle, b->tiltDir, pourColor, flowStrength);
            }
            
            // 重新应用瓶子位置变换来绘制玻璃瓶
            glPushMatrix();
            glTranslatef(b->x + b->moveX, b->y + b->offsetY, b->z);
            
            // 绘制玻璃瓶（带旋转）
            glPushMatrix();
            glRotatef(b->tiltAngle * b->tiltDir, 0, 0, 1);
            drawGlass(i == selectedBottle, i == hoveredBottle);
            glPopMatrix();
        } else {
            // 瓶子直立时，分层绘制液体
            int full = (int)vis;
            float part = vis - full;
            float y = 0.02f;
            
            int layersToShow = full;
            if (layersToShow > MAX_LAYERS) layersToShow = MAX_LAYERS;
            
            for (int j = 0; j < layersToShow; j++) {
                int colorIdx;
                if (j < b->count) {
                    colorIdx = b->layers[j];
                } else if (animating && i == animTgt && pourColor > 0) {
                    colorIdx = pourColor;
                } else {
                    break;
                }
                glPushMatrix();
                glTranslatef(0, y, 0);
                drawLiquid(L_HGT - 0.02f, colorIdx);
                glPopMatrix();
                y += L_HGT;
            }
            
            // 绘制部分层
            if (part > 0.01f) {
                int colorIdx = 0;
                if (full < b->count) {
                    colorIdx = b->layers[full];
                } else if (animating && i == animTgt && pourColor > 0) {
                    colorIdx = pourColor;
                } else if (animating && i == animSrc && pourColor > 0) {
                    colorIdx = pourColor;
                }
                if (colorIdx > 0) {
                    glPushMatrix();
                    glTranslatef(0, y, 0);
                    drawLiquid((L_HGT - 0.02f) * part, colorIdx);
                    glPopMatrix();
                }
            }
            
            // 绘制玻璃瓶
            drawGlass(i == selectedBottle, i == hoveredBottle);
        }
    }
    glPopMatrix();
}

// ========== Game Logic ==========
// 自由倾倒模式：只检查目标瓶是否有空间，不检查颜色匹配
int canPour(int f, int t) {
    if (f == t || f < 0 || t < 0) return 0;
    if (bottles[f].count == 0) return 0;
    if (bottles[t].count >= MAX_LAYERS) return 0;
    // 移除颜色匹配检查，允许自由倾倒
    // 只要目标瓶有空间就可以倒
    return 1;
}

int countTop(int i) {
    Bottle* b = &bottles[i];
    if (b->count == 0) return 0;
    int c = b->layers[b->count - 1];
    int n = 0;
    for (int j = b->count - 1; j >= 0 && b->layers[j] == c; j--) n++;
    return n;
}

// 计算可以倒入的层数
int calcPourAmount(int source, int target) {
    if (!canPour(source, target)) return 0;
    
    Bottle* srcBottle = &bottles[source];
    Bottle* tgtBottle = &bottles[target];
    
    // 计算源瓶顶部连续同色层数
    int sameColorCount = countTop(source);
    
    // 目标瓶剩余空间
    int space = MAX_LAYERS - tgtBottle->count;
    
    return (sameColorCount < space) ? sameColorCount : space;
}

int checkWin() {
    for (int i = 0; i < numBottles; i++) {
        if (bottles[i].count == 0) continue;
        if (bottles[i].count != MAX_LAYERS) return 0;
        int c = bottles[i].layers[0];
        for (int j = 1; j < MAX_LAYERS; j++)
            if (bottles[i].layers[j] != c) return 0;
    }
    return 1;
}

// ========== Forward Declarations ==========
void updateDemo();
int canPour(int f, int t);
float calcLiftHeight(int srcIdx, int tgtIdx);
float calcMoveX(int srcIdx, int tgtIdx);
int checkWin();

// ========== Precise Animation ==========
// Animation variables for smooth return
float animLiftHeight = 0;
float animStartMoveX = 0;

// Calculate required lift height so tilted bottle mouth is above target bottle top
float calcLiftHeight(int srcIdx, int tgtIdx) {
    // Target bottle top height (including neck)
    float targetTop = BOTTLE_TOP + 0.15f;  // Add clearance
    
    // When tilted 90 degrees (horizontal), the bottle is completely sideways
    // mouth Y position = 0 (at bottle center height)
    float tiltRad = 90.0f * PI / 180.0f;
    float tiltedMouthY = cosf(tiltRad) * BOTTLE_TOP;  // = 0
    
    // Required lift = targetTop + safety margin
    float lift = targetTop + 0.3f;
    if (lift < 0.5f) lift = 0.5f;
    return lift;
}

// Calculate horizontal move so mouth is above target bottle center
float calcMoveX(int srcIdx, int tgtIdx) {
    Bottle* src = &bottles[srcIdx];
    Bottle* tgt = &bottles[tgtIdx];
    float dx = tgt->x - src->x;
    
    // When tilted 90 degrees, mouth offset from bottle center = BOTTLE_TOP
    float tiltRad = 90.0f * PI / 180.0f;
    float mouthOffsetX = sinf(tiltRad) * BOTTLE_TOP;  // = BOTTLE_TOP
    
    // tiltDir: target right = -1 (tilt right), target left = +1 (tilt left)
    int tiltDir = (dx > 0) ? -1 : 1;
    
    // We want: src.x + moveX + mouthOffsetX * (-tiltDir) = tgt.x
    // moveX = dx - mouthOffsetX * (-tiltDir) = dx + mouthOffsetX * tiltDir
    return dx + mouthOffsetX * tiltDir;
}

void updateAnim() {
    if (!animating) return;
    Bottle* src = &bottles[animSrc];
    Bottle* tgt = &bottles[animTgt];
    float spd = 0.022f;

    switch (animPhase) {
        case 0:  // Phase 0: Lift up
            src->offsetY += spd * 2.5f;
            if (src->offsetY >= animLiftHeight) {
                src->offsetY = animLiftHeight;
                animPhase = 1;
            }
            break;
            
        case 1: {  // Phase 1: Move horizontally to position
            float dx = src->targetMoveX - src->moveX;
            if (fabsf(dx) > 0.01f) {
                src->moveX += dx * spd * 5.0f;
            } else {
                src->moveX = src->targetMoveX;
                animPhase = 2;
                pourColor = src->layers[src->count - 1];
                pourTotal = countTop(animSrc);
                int space = MAX_LAYERS - tgt->count;
                if (pourTotal > space) pourTotal = space;
                poured = 0;
                pourAmt = 0;
            }
        } break;
        
        case 2:  // Phase 2: Tilt to horizontal (90 degrees)
            src->tiltAngle += spd * 80.0f;
            if (src->tiltAngle >= 90.0f) { 
                src->tiltAngle = 90.0f; 
                animPhase = 3; 
            }
            break;
            
        case 3: {  // Phase 3: Pour liquid
            // Fixed pour speed - same for all amounts
            float pourSpeed = spd * 0.9f;
            pourAmt += pourSpeed;
            
            // Clamp pourAmt to not exceed 1.0
            if (pourAmt > 1.0f) pourAmt = 1.0f;
            
            // 源瓶视觉：当前count减去正在倒的部分
            // src->count 已经是当前实际层数，pourAmt是正在倒出的部分(0-1)
            float srcVis = (float)src->count - pourAmt;
            if (srcVis < 0) srcVis = 0;
            src->visualLiquid = srcVis;
            
            // 目标瓶视觉：当前count加上正在倒入的部分
            float tgtVis = (float)tgt->count + pourAmt;
            if (tgtVis > MAX_LAYERS) tgtVis = (float)MAX_LAYERS;
            tgt->visualLiquid = tgtVis;
            
            // When one unit is fully poured
            if (pourAmt >= 1.0f) {
                // Transfer one layer
                src->count--;
                tgt->layers[tgt->count] = pourColor;
                tgt->count++;
                poured++;
                totalLayersMoved++;  // 记录移动的层数
                
                // Reset pour amount for next layer
                pourAmt = 0;
                
                // 立即同步视觉到新的实际值
                src->visualLiquid = (float)src->count;
                tgt->visualLiquid = (float)tgt->count;
                
                // Check if done pouring
                if (poured >= pourTotal) {
                    // 检查是否是完美倒水（一次倒完所有同色层）
                    if (pourTotal >= 2) {
                        perfectPours++;
                    }
                    // 更新连击
                    comboPours++;
                    if (comboPours > maxCombo) {
                        maxCombo = comboPours;
                    }
                    animPhase = 4;
                }
            }
        } break;
        
        case 4:  // Phase 4: Return upright (reverse of tilt)
            src->tiltAngle -= spd * 80.0f;
            if (src->tiltAngle <= 0) { 
                src->tiltAngle = 0; 
                animPhase = 5; 
            }
            break;
            
        case 5: {  // Phase 5: Move back horizontally (reverse of phase 1)
            float dx = 0 - src->moveX;
            if (fabsf(dx) > 0.01f) {
                src->moveX += dx * spd * 5.0f;
            } else {
                src->moveX = 0;
                animPhase = 6;
            }
        } break;
        
        case 6:  // Phase 6: Lower down (reverse of phase 0)
            src->offsetY -= spd * 2.5f;
            if (src->offsetY <= 0) {
                src->offsetY = 0;
                src->tiltDir = 0;
                animating = 0;
                if (checkWin()) {
                    gameEndTime = (float)glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
                    lastScore = calculateScore();
                    gameState = STATE_WON;
                }
            }
            break;
    }
    glutPostRedisplay();
}

void timerCB(int v) {
    updateAnim();
    
    // 更新悬停和选中动画时间
    hoverGlowTime += 0.016f;
    selectPulseTime += 0.016f;
    if (hoverGlowTime > 1000.0f) hoverGlowTime = 0;
    if (selectPulseTime > 1000.0f) selectPulseTime = 0;
    
    // Update demo mode
    if (demoMode && gameState == STATE_DEMO) {
        updateDemo();
        glutPostRedisplay();
    }
    
    // Update error message timer
    if (errorTimer > 0) {
        errorTimer -= 0.016f;
        if (errorTimer <= 0) {
            errorTimer = 0;
            errorMsg[0] = '\0';
        }
        glutPostRedisplay();
    }
    
    // 如果有悬停或选中的瓶子，持续刷新以显示动画
    if (hoveredBottle >= 0 || selectedBottle >= 0) {
        glutPostRedisplay();
    }
    
    // AI聊天面板打字机效果更新
    if (g_chatOpen && g_aiInitialized) {
        if (g_aiAgent.updateTypewriter(0.016f, 40.0f)) {
            glutPostRedisplay();
        }
    }
    
    glutTimerFunc(16, timerCB, 0);
}

// ========== Level Setup ==========
void initBottle(int i, float x) {
    bottles[i].x = x;
    bottles[i].y = 0;
    bottles[i].z = 0;
    bottles[i].offsetY = 0;
    bottles[i].tiltAngle = 0;
    bottles[i].tiltDir = 0;
    bottles[i].moveX = 0;
    bottles[i].targetMoveX = 0;
    bottles[i].count = 0;
    bottles[i].visualLiquid = 0;
    bottles[i].glowIntensity = 0;
    bottles[i].pulsePhase = 0;
}

void setupBottles(int n) {
    numBottles = n;
    float sp = BOTTLE_SPACING;
    float sx = -(numBottles - 1) * sp / 2.0f;
    for (int i = 0; i < numBottles; i++) initBottle(i, sx + i * sp);
}

void initEasy() {
    setupBottles(5);
    bottles[0].layers[0]=1; bottles[0].layers[1]=2; bottles[0].layers[2]=3; bottles[0].layers[3]=1;
    bottles[0].count=4; bottles[0].visualLiquid=4;
    bottles[1].layers[0]=2; bottles[1].layers[1]=3; bottles[1].layers[2]=1; bottles[1].layers[3]=2;
    bottles[1].count=4; bottles[1].visualLiquid=4;
    bottles[2].layers[0]=3; bottles[2].layers[1]=1; bottles[2].layers[2]=2; bottles[2].layers[3]=3;
    bottles[2].count=4; bottles[2].visualLiquid=4;
    bottles[3].count=0; bottles[3].visualLiquid=0;
    bottles[4].count=0; bottles[4].visualLiquid=0;
    selectedBottle = -1; animating = 0; currentLevel = 1;
    // 重置评分相关变量
    playerMoves = 0;
    gameStartTime = (float)glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
    gameEndTime = 0;
    perfectPours = 0;
    comboPours = 0;
    maxCombo = 0;
    undoCount = 0;
    totalLayersMoved = 0;
}

void initMedium() {
    setupBottles(6);
    bottles[0].layers[0]=1; bottles[0].layers[1]=2; bottles[0].layers[2]=3; bottles[0].layers[3]=4;
    bottles[0].count=4; bottles[0].visualLiquid=4;
    bottles[1].layers[0]=3; bottles[1].layers[1]=4; bottles[1].layers[2]=1; bottles[1].layers[3]=2;
    bottles[1].count=4; bottles[1].visualLiquid=4;
    bottles[2].layers[0]=2; bottles[2].layers[1]=1; bottles[2].layers[2]=4; bottles[2].layers[3]=3;
    bottles[2].count=4; bottles[2].visualLiquid=4;
    bottles[3].layers[0]=4; bottles[3].layers[1]=3; bottles[3].layers[2]=2; bottles[3].layers[3]=1;
    bottles[3].count=4; bottles[3].visualLiquid=4;
    bottles[4].count=0; bottles[4].visualLiquid=0;
    bottles[5].count=0; bottles[5].visualLiquid=0;
    selectedBottle = -1; animating = 0; currentLevel = 2;
    // 重置评分相关变量
    playerMoves = 0;
    gameStartTime = (float)glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
    gameEndTime = 0;
    perfectPours = 0;
    comboPours = 0;
    maxCombo = 0;
    undoCount = 0;
    totalLayersMoved = 0;
}

void initHard() {
    setupBottles(7);
    bottles[0].layers[0]=1; bottles[0].layers[1]=2; bottles[0].layers[2]=3; bottles[0].layers[3]=4;
    bottles[0].count=4; bottles[0].visualLiquid=4;
    bottles[1].layers[0]=5; bottles[1].layers[1]=1; bottles[1].layers[2]=2; bottles[1].layers[3]=3;
    bottles[1].count=4; bottles[1].visualLiquid=4;
    bottles[2].layers[0]=4; bottles[2].layers[1]=5; bottles[2].layers[2]=1; bottles[2].layers[3]=2;
    bottles[2].count=4; bottles[2].visualLiquid=4;
    bottles[3].layers[0]=3; bottles[3].layers[1]=4; bottles[3].layers[2]=5; bottles[3].layers[3]=1;
    bottles[3].count=4; bottles[3].visualLiquid=4;
    bottles[4].layers[0]=2; bottles[4].layers[1]=3; bottles[4].layers[2]=4; bottles[4].layers[3]=5;
    bottles[4].count=4; bottles[4].visualLiquid=4;
    bottles[5].count=0; bottles[5].visualLiquid=0;
    bottles[6].count=0; bottles[6].visualLiquid=0;
    selectedBottle = -1; animating = 0; currentLevel = 3;
    // 重置评分相关变量
    playerMoves = 0;
    gameStartTime = (float)glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
    gameEndTime = 0;
    perfectPours = 0;
    comboPours = 0;
    maxCombo = 0;
    undoCount = 0;
    totalLayersMoved = 0;
}

void restartLevel() {
    switch (currentLevel) {
        case 1: initEasy(); break;
        case 2: initMedium(); break;
        case 3: initHard(); break;
        default: initEasy(); break;
    }
    gameState = STATE_PLAYING;
}

// ========== Demo Mode ==========
void initDemo() {
    // Demo使用简化关卡：4个瓶子，2种颜色，交替排列
    setupBottles(4);
    // 瓶0: [1,2,1,2] - 交替排列
    bottles[0].layers[0]=1; bottles[0].layers[1]=2; bottles[0].layers[2]=1; bottles[0].layers[3]=2;
    bottles[0].count=4; bottles[0].visualLiquid=4;
    // 瓶1: [2,1,2,1] - 交替排列
    bottles[1].layers[0]=2; bottles[1].layers[1]=1; bottles[1].layers[2]=2; bottles[1].layers[3]=1;
    bottles[1].count=4; bottles[1].visualLiquid=4;
    // 瓶2: 空
    bottles[2].count=0; bottles[2].visualLiquid=0;
    // 瓶3: 空
    bottles[3].count=0; bottles[3].visualLiquid=0;
    
    selectedBottle = -1; 
    animating = 0; 
    currentLevel = 1;
    // 重置评分相关变量
    playerMoves = 0;
    gameStartTime = (float)glutGet(GLUT_ELAPSED_TIME) / 1000.0f;
    gameEndTime = 0;
    perfectPours = 0;
    comboPours = 0;
    maxCombo = 0;
    undoCount = 0;
    totalLayersMoved = 0;
    
    demoMode = 1;
    demoStep = 0;
    demoTimer = 1.5f;  // 开始前等待1.5秒
    gameState = STATE_DEMO;
}

void startDemoMove(int src, int tgt) {
    if (!canPour(src, tgt)) {
        // 如果不能倒，跳过这一步（不应该发生，但作为保护）
        return;
    }
    
    animSrc = src;
    animTgt = tgt;
    
    float dx = bottles[animTgt].x - bottles[animSrc].x;
    bottles[animSrc].tiltDir = (dx > 0) ? -1 : 1;
    animLiftHeight = calcLiftHeight(animSrc, animTgt);
    bottles[animSrc].targetMoveX = calcMoveX(animSrc, animTgt);
    
    animating = 1;
    animPhase = 0;
    pourAmt = 0;
}

void updateDemo() {
    if (!demoMode || gameState != STATE_DEMO) return;
    
    // 等待动画完成
    if (animating) return;
    
    // 计时器
    demoTimer -= 0.016f;
    if (demoTimer > 0) return;
    
    // 检查是否完成所有步骤
    if (demoStep >= demoSolutionLen) {
        // 等待最后一个动画完成后再结束
        demoTimer = 1.0f;  // 等1秒
        demoMode = 0;
        gameState = STATE_WON;
        return;
    }
    
    // 执行下一步
    int src = demoSolution[demoStep][0];
    int tgt = demoSolution[demoStep][1];
    
    if (canPour(src, tgt)) {
        startDemoMove(src, tgt);
        playerMoves++;
    }
    // 无论是否成功都增加步骤计数，避免卡住
    demoStep++;
    demoTimer = 0.8f;  // 每步之间间隔0.8秒，让动画完成
}

// ========== Rendering ==========
// ========== Enhanced Fluid Stream ==========
// 绘制带有重力抛物线效果的3D液柱
void drawStream(float sx, float sy, float tx, float ty, int col, float prog) {
    if (col <= 0 || prog < 0.01f) return;
    
    // 获取当前时间用于动画
    static float streamTime = 0;
    streamTime += 0.05f;
    if (streamTime > 100.0f) streamTime = 0;
    
    glDisable(GL_LIGHTING);  // 液柱使用自发光效果
    
    // 重力抛物线控制点：让液柱起点有一定水平速度，然后受重力下落
    float dx_total = tx - sx;
    float dy_total = ty - sy;
    
    // 控制点位于起点上方，模拟液体初始抛射方向
    float mx = sx + dx_total * 0.35f;
    float my = sy + fabsf(dy_total) * 0.1f + 0.2f;  // 起始段先略微上升再下落
    
    int segments = 32;   // 更多段数 = 更平滑
    int radialSegs = 14;  // 更多径向段 = 更圆
    
    // 基础半径随 prog 的流量感变化
    float baseRadius = 0.05f * prog;
    if (baseRadius > 0.05f) baseRadius = 0.05f;
    
    // 绘制3D圆柱形液柱
    for (int i = 0; i < segments; i++) {
        float t1 = (float)i / segments;
        float t2 = (float)(i + 1) / segments;
        if (t1 > prog) break;
        if (t2 > prog) t2 = prog;
        
        float u1 = 1 - t1, u2 = 1 - t2;
        
        // 贝塞尔曲线位置
        float px1 = u1*u1*sx + 2*u1*t1*mx + t1*t1*tx;
        float py1 = u1*u1*sy + 2*u1*t1*my + t1*t1*ty;
        float px2 = u2*u2*sx + 2*u2*t2*mx + t2*t2*tx;
        float py2 = u2*u2*sy + 2*u2*t2*my + t2*t2*ty;
        
        // 液柱半径：起点有颈口收缩效果，中段因表面张力略粗，末端变细
        // 模拟真实水流：出口处受颈口限制，之后因重力加速而变细
        float neckFactor = 0.7f + 0.3f * t1;  // 颈口收缩 → 正常
        float gravityThin = 1.0f - t1 * t1 * 0.5f;  // 重力加速导致液柱变细
        float wave1 = 1.0f + 0.08f * sinf(streamTime * 4.0f + t1 * 12.0f);
        float wave2 = 1.0f + 0.08f * sinf(streamTime * 4.0f + t2 * 12.0f);
        float r1 = baseRadius * neckFactor * gravityThin * wave1;
        float r2 = baseRadius * (0.7f + 0.3f * t2) * (1.0f - t2 * t2 * 0.5f) * wave2;
        
        // 避免半径过小
        if (r1 < 0.005f) r1 = 0.005f;
        if (r2 < 0.005f) r2 = 0.005f;
        
        // 计算切线方向
        float dx = px2 - px1;
        float dy = py2 - py1;
        float len = sqrtf(dx*dx + dy*dy);
        if (len < 0.001f) continue;
        
        // 绘制圆柱段
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= radialSegs; j++) {
            float angle = (float)j / radialSegs * 2.0f * PI;
            float nx = cosf(angle);
            float nz = sinf(angle);
            
            // 液体光泽效果：高光在正面，边缘半透明
            float facing = fabsf(nx);
            float edgeFade = 0.6f + 0.4f * facing;  // 边缘透明度降低
            float specular = facing * facing * 0.3f;  // 高光
            float brightness = 0.75f + 0.25f * facing + specular;
            float alpha = liquidColors[col][3] * edgeFade * (0.9f - t1 * 0.25f);
            
            glColor4f(fminf(liquidColors[col][0] * brightness, 1.0f), 
                     fminf(liquidColors[col][1] * brightness, 1.0f),
                     fminf(liquidColors[col][2] * brightness, 1.0f),
                     alpha);
            
            glNormal3f(nx, 0, nz);
            glVertex3f(px1 + nx * r1, py1, nz * r1);
            glVertex3f(px2 + nx * r2, py2, nz * r2);
        }
        glEnd();
    }
    
    // 绘制液柱末端的水滴效果 + 飞溅小液滴
    if (prog > 0.7f) {
        float dropT = prog;
        float u = 1 - dropT;
        float dropX = u*u*sx + 2*u*dropT*mx + dropT*dropT*tx;
        float dropY = u*u*sy + 2*u*dropT*my + dropT*dropT*ty;
        
        // 主水滴 - 拉长的椭球形
        float dropR = 0.035f * (1.0f + 0.15f * sinf(streamTime * 5.0f));
        
        glColor4f(liquidColors[col][0] * 1.1f, liquidColors[col][1] * 1.1f, 
                 liquidColors[col][2] * 1.1f, liquidColors[col][3] * 0.9f);
        
        GLUquadric* q = gluNewQuadric();
        glPushMatrix();
        glTranslatef(dropX, dropY, 0);
        glScalef(0.8f, 1.3f, 0.8f);  // 拉长水滴形状（重力方向）
        gluSphere(q, dropR, 10, 10);
        glPopMatrix();
        
        // 小飞溅液滴（仅在接近目标时显示）
        if (prog > 0.9f) {
            for (int sp = 0; sp < 3; sp++) {
                float spOff = sinf(streamTime * 2.0f + sp * 2.1f) * 0.08f;
                float spY = dropY - 0.02f - sp * 0.03f;
                float spR = 0.012f * (1.0f + 0.3f * sinf(streamTime * 7.0f + sp));
                glPushMatrix();
                glTranslatef(dropX + spOff, spY, cosf(streamTime + sp) * 0.04f);
                gluSphere(q, spR, 6, 6);
                glPopMatrix();
            }
        }
        
        gluDeleteQuadric(q);
    }
    
    glEnable(GL_LIGHTING);
}

void setupLights() {
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_LIGHT2);  // 添加第三个光源用于高光
    
    // 主光源 - 暖色调
    float pos0[] = {5, 10, 8, 1};
    float dif0[] = {1.0f, 0.98f, 0.95f, 1};
    float amb[] = {0.28f, 0.28f, 0.32f, 1};
    float spec0[] = {1.0f, 1.0f, 1.0f, 1};
    glLightfv(GL_LIGHT0, GL_POSITION, pos0);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif0);
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_SPECULAR, spec0);
    
    // 补光 - 冷色调
    float pos1[] = {-4, 6, 5, 1};
    float dif1[] = {0.4f, 0.45f, 0.55f, 1};
    float spec1[] = {0.5f, 0.5f, 0.6f, 1};
    glLightfv(GL_LIGHT1, GL_POSITION, pos1);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, dif1);
    glLightfv(GL_LIGHT1, GL_SPECULAR, spec1);
    
    // 背光 - 用于边缘高光
    float pos2[] = {0, 3, -5, 1};
    float dif2[] = {0.2f, 0.22f, 0.25f, 1};
    float spec2[] = {0.8f, 0.85f, 0.9f, 1};
    glLightfv(GL_LIGHT2, GL_POSITION, pos2);
    glLightfv(GL_LIGHT2, GL_DIFFUSE, dif2);
    glLightfv(GL_LIGHT2, GL_SPECULAR, spec2);
}

void drawFloor() {
    glDisable(GL_LIGHTING);
    glBegin(GL_QUADS);
    glColor4f(0.12f, 0.13f, 0.18f, 1);
    glVertex3f(-10, -0.02f, -3);
    glVertex3f(10, -0.02f, -3);
    glColor4f(0.08f, 0.09f, 0.12f, 1);
    glVertex3f(10, -0.02f, 5);
    glVertex3f(-10, -0.02f, 5);
    glEnd();
    glEnable(GL_LIGHTING);
}

// 将3D世界坐标转换为2D屏幕坐标
void worldToScreen(float wx, float wy, float wz, float* sx, float* sy) {
    GLdouble modelview[16], projection[16];
    GLint viewport[4];
    GLdouble screenX, screenY, screenZ;
    
    glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
    glGetDoublev(GL_PROJECTION_MATRIX, projection);
    glGetIntegerv(GL_VIEWPORT, viewport);
    
    gluProject(wx, wy, wz, modelview, projection, viewport, &screenX, &screenY, &screenZ);
    
    *sx = (float)screenX;
    *sy = (float)screenY;
}

// 更新所有瓶子的屏幕坐标缓存（在3D渲染后、2D UI渲染前调用）
void updateBottleScreenPositions() {
    for (int i = 0; i < numBottles; i++) {
        Bottle* b = &bottles[i];
        float sx, sy;
        
        // 瓶子中心X坐标
        worldToScreen(b->x + b->moveX, b->y + b->offsetY + B_HGT / 2, b->z, &sx, &sy);
        bottleScreenX[i] = sx;
        
        // 瓶子顶部Y坐标
        worldToScreen(b->x + b->moveX, b->y + b->offsetY + BOTTLE_TOP, b->z, &sx, &sy);
        bottleScreenTopY[i] = sy;
        
        // 瓶子底部Y坐标
        worldToScreen(b->x + b->moveX, b->y + b->offsetY, b->z, &sx, &sy);
        bottleScreenBottomY[i] = sy;
    }
}

// 获取瓶子在屏幕上的X坐标（使用缓存）
float getBottleScreenX(int bottleIdx) {
    if (bottleIdx < 0 || bottleIdx >= numBottles) return (float)(winW / 2);
    return bottleScreenX[bottleIdx];
}

// 获取瓶子顶部在屏幕上的Y坐标（使用缓存）
float getBottleScreenTopY(int bottleIdx) {
    if (bottleIdx < 0 || bottleIdx >= numBottles) return (float)(winH / 2);
    return bottleScreenTopY[bottleIdx];
}

// 获取瓶子底部在屏幕上的Y坐标（使用缓存）
float getBottleScreenBottomY(int bottleIdx) {
    if (bottleIdx < 0 || bottleIdx >= numBottles) return (float)(winH / 2);
    return bottleScreenBottomY[bottleIdx];
}

// 绘制悬停提示框
void drawTooltip(int bottleIdx) {
    if (bottleIdx < 0 || bottleIdx >= numBottles || !showTooltip) return;
    
    Bottle* b = &bottles[bottleIdx];
    const char* colorNames[] = {"", "Red", "Green", "Blue", "Yellow", "Purple", "Orange", "Cyan"};
    
    // 使用精确的屏幕坐标计算
    float tipX = getBottleScreenX(bottleIdx);
    float tipY = getBottleScreenTopY(bottleIdx) + 30;  // 在瓶子顶部上方30像素
    
    // 计算连续同色层数
    int sameColorCount = 0;
    int topColor = 0;
    if (b->count > 0) {
        topColor = b->layers[b->count - 1];
        for (int j = b->count - 1; j >= 0 && b->layers[j] == topColor; j--)
            sameColorCount++;
    }
    
    // 检查是否为纯色瓶
    int isPure = 0;
    if (b->count == MAX_LAYERS) {
        isPure = 1;
        int baseColor = b->layers[0];
        for (int j = 1; j < MAX_LAYERS; j++) {
            if (b->layers[j] != baseColor) { isPure = 0; break; }
        }
    }
    
    // 计算可以倒入的目标数量
    int validTargets = 0;
    for (int i = 0; i < numBottles; i++) {
        if (i != bottleIdx && canPour(bottleIdx, i)) validTargets++;
    }
    
    // 提示框背景 - 更大更详细
    float boxW = 180, boxH = 160;
    
    // 渐变背景
    glBegin(GL_QUADS);
    glColor4f(0.05f, 0.08f, 0.15f, 0.95f);
    glVertex2f(tipX - boxW/2, tipY - boxH);
    glVertex2f(tipX + boxW/2, tipY - boxH);
    glColor4f(0.1f, 0.15f, 0.25f, 0.95f);
    glVertex2f(tipX + boxW/2, tipY);
    glVertex2f(tipX - boxW/2, tipY);
    glEnd();
    
    // 发光边框
    float glowPulse = 0.5f + 0.3f * sinf(hoverGlowTime * 3.0f);
    glColor4f(0.3f * glowPulse, 0.7f * glowPulse, 1.0f * glowPulse, 0.9f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(tipX - boxW/2, tipY - boxH);
    glVertex2f(tipX + boxW/2, tipY - boxH);
    glVertex2f(tipX + boxW/2, tipY);
    glVertex2f(tipX - boxW/2, tipY);
    glEnd();
    glLineWidth(1.0f);
    
    // 标题栏背景
    glColor4f(0.2f, 0.4f, 0.6f, 0.5f);
    glBegin(GL_QUADS);
    glVertex2f(tipX - boxW/2 + 2, tipY - 25);
    glVertex2f(tipX + boxW/2 - 2, tipY - 25);
    glVertex2f(tipX + boxW/2 - 2, tipY - 2);
    glVertex2f(tipX - boxW/2 + 2, tipY - 2);
    glEnd();
    
    char text[64];
    float yPos = tipY - 18;
    
    // 瓶子编号标题
    sprintf(text, "=== Bottle #%d ===", bottleIdx + 1);
    glColor3f(1.0f, 0.95f, 0.7f);
    drawText(tipX - 55, yPos, text, GLUT_BITMAP_HELVETICA_12);
    yPos -= 22;
    
    // 状态指示
    if (isPure) {
        glColor3f(0.3f, 1.0f, 0.5f);
        drawText(tipX - 75, yPos, "[COMPLETE] Pure Color!", GLUT_BITMAP_HELVETICA_12);
    } else if (b->count == 0) {
        glColor3f(0.6f, 0.7f, 0.8f);
        drawText(tipX - 55, yPos, "[EMPTY] Available", GLUT_BITMAP_HELVETICA_12);
    } else if (b->count == MAX_LAYERS) {
        glColor3f(1.0f, 0.6f, 0.3f);
        drawText(tipX - 45, yPos, "[FULL] Mixed", GLUT_BITMAP_HELVETICA_12);
    } else {
        glColor3f(0.5f, 0.8f, 1.0f);
        sprintf(text, "[%d/%d] In Progress", b->count, MAX_LAYERS);
        drawText(tipX - 55, yPos, text, GLUT_BITMAP_HELVETICA_12);
    }
    yPos -= 18;
    
    // 分隔线
    glColor4f(0.4f, 0.6f, 0.8f, 0.5f);
    glBegin(GL_LINES);
    glVertex2f(tipX - boxW/2 + 10, yPos + 5);
    glVertex2f(tipX + boxW/2 - 10, yPos + 5);
    glEnd();
    
    // 颜色层详情
    glColor3f(0.8f, 0.85f, 0.9f);
    drawText(tipX - 75, yPos, "Color Layers:", GLUT_BITMAP_HELVETICA_12);
    yPos -= 16;
    
    if (b->count > 0) {
        // 显示每一层的颜色（从底到顶）
        for (int layer = 0; layer < b->count && layer < 4; layer++) {
            int c = b->layers[layer];
            // 颜色方块
            glColor3f(liquidColors[c][0], liquidColors[c][1], liquidColors[c][2]);
            glBegin(GL_QUADS);
            glVertex2f(tipX - 75, yPos - 10);
            glVertex2f(tipX - 60, yPos - 10);
            glVertex2f(tipX - 60, yPos + 2);
            glVertex2f(tipX - 75, yPos + 2);
            glEnd();
            // 颜色名称
            sprintf(text, "L%d: %s", layer + 1, colorNames[c]);
            glColor3f(liquidColors[c][0] * 0.8f + 0.2f, liquidColors[c][1] * 0.8f + 0.2f, liquidColors[c][2] * 0.8f + 0.2f);
            drawText(tipX - 55, yPos, text, GLUT_BITMAP_HELVETICA_12);
            yPos -= 14;
        }
    } else {
        glColor3f(0.5f, 0.55f, 0.6f);
        drawText(tipX - 75, yPos, "  (No liquid)", GLUT_BITMAP_HELVETICA_12);
        yPos -= 14;
    }
    
    // 分隔线
    yPos -= 3;
    glColor4f(0.4f, 0.6f, 0.8f, 0.5f);
    glBegin(GL_LINES);
    glVertex2f(tipX - boxW/2 + 10, yPos + 5);
    glVertex2f(tipX + boxW/2 - 10, yPos + 5);
    glEnd();
    yPos -= 5;
    
    // 操作提示
    if (b->count > 0 && !isPure) {
        sprintf(text, "Top %d same color", sameColorCount);
        glColor3f(0.9f, 0.8f, 0.4f);
        drawText(tipX - 75, yPos, text, GLUT_BITMAP_HELVETICA_12);
        yPos -= 14;
        
        if (validTargets > 0) {
            sprintf(text, "Can pour to %d bottles", validTargets);
            glColor3f(0.4f, 1.0f, 0.6f);
        } else {
            sprintf(text, "No valid targets");
            glColor3f(1.0f, 0.5f, 0.4f);
        }
        drawText(tipX - 75, yPos, text, GLUT_BITMAP_HELVETICA_12);
    } else if (b->count == 0) {
        glColor3f(0.6f, 0.9f, 0.7f);
        drawText(tipX - 75, yPos, "Ready to receive", GLUT_BITMAP_HELVETICA_12);
    }
}

// 绘制有效目标高亮
void drawValidTargetIndicators() {
    if (selectedBottle < 0 || !highlightValidTargets) return;
    
    Bottle* srcBottle = &bottles[selectedBottle];
    int topColor = srcBottle->count > 0 ? srcBottle->layers[srcBottle->count - 1] : 0;
    const char* colorNames[] = {"", "Red", "Green", "Blue", "Yellow", "Purple", "Orange", "Cyan"};
    
    // 计算连续同色层数
    int sameColorCount = 0;
    if (srcBottle->count > 0) {
        for (int j = srcBottle->count - 1; j >= 0 && srcBottle->layers[j] == topColor; j--)
            sameColorCount++;
    }
    
    // 统计有效目标
    int validTargetCount = 0;
    int validTargets[20];
    for (int i = 0; i < numBottles; i++) {
        if (i != selectedBottle && canPour(selectedBottle, i)) {
            validTargets[validTargetCount++] = i;
        }
    }
    
    // ========== 左上角显示选中瓶子的详细信息面板 ==========
    float panelX = 20;
    float panelY = winH - 80;
    float panelW = 220;
    float panelH = 180;
    
    // 面板背景（渐变）
    glBegin(GL_QUADS);
    glColor4f(0.08f, 0.12f, 0.2f, 0.95f);
    glVertex2f(panelX, panelY - panelH);
    glVertex2f(panelX + panelW, panelY - panelH);
    glColor4f(0.12f, 0.18f, 0.28f, 0.95f);
    glVertex2f(panelX + panelW, panelY);
    glVertex2f(panelX, panelY);
    glEnd();
    
    // 发光边框
    float glowPulse = 0.6f + 0.4f * sinf(hoverGlowTime * 2.5f);
    glColor4f(1.0f * glowPulse, 0.85f * glowPulse, 0.3f * glowPulse, 0.9f);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(panelX, panelY - panelH);
    glVertex2f(panelX + panelW, panelY - panelH);
    glVertex2f(panelX + panelW, panelY);
    glVertex2f(panelX, panelY);
    glEnd();
    glLineWidth(1.0f);
    
    // 标题栏
    glColor4f(1.0f, 0.8f, 0.2f, 0.3f);
    glBegin(GL_QUADS);
    glVertex2f(panelX + 2, panelY - 28);
    glVertex2f(panelX + panelW - 2, panelY - 28);
    glVertex2f(panelX + panelW - 2, panelY - 2);
    glVertex2f(panelX + 2, panelY - 2);
    glEnd();
    
    char text[64];
    float yPos = panelY - 18;
    
    // 标题
    sprintf(text, ">> SELECTED: Bottle #%d <<", selectedBottle + 1);
    glColor3f(1.0f, 0.9f, 0.4f);
    drawText(panelX + 15, yPos, text, GLUT_BITMAP_HELVETICA_12);
    yPos -= 25;
    
    // 瓶子状态
    sprintf(text, "Status: %d/%d layers", srcBottle->count, MAX_LAYERS);
    glColor3f(0.8f, 0.9f, 1.0f);
    drawText(panelX + 10, yPos, text, GLUT_BITMAP_HELVETICA_12);
    yPos -= 18;
    
    // 顶层颜色信息
    if (srcBottle->count > 0) {
        // 颜色方块
        glColor3f(liquidColors[topColor][0], liquidColors[topColor][1], liquidColors[topColor][2]);
        glBegin(GL_QUADS);
        glVertex2f(panelX + 10, yPos - 10);
        glVertex2f(panelX + 30, yPos - 10);
        glVertex2f(panelX + 30, yPos + 4);
        glVertex2f(panelX + 10, yPos + 4);
        glEnd();
        // 边框
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(panelX + 10, yPos - 10);
        glVertex2f(panelX + 30, yPos - 10);
        glVertex2f(panelX + 30, yPos + 4);
        glVertex2f(panelX + 10, yPos + 4);
        glEnd();
        
        sprintf(text, "Top Color: %s", colorNames[topColor]);
        glColor3f(liquidColors[topColor][0] * 0.7f + 0.3f, liquidColors[topColor][1] * 0.7f + 0.3f, liquidColors[topColor][2] * 0.7f + 0.3f);
        drawText(panelX + 35, yPos, text, GLUT_BITMAP_HELVETICA_12);
        yPos -= 18;
        
        // 可倒出层数
        sprintf(text, "Pourable: %d layers (same color)", sameColorCount);
        glColor3f(0.4f, 1.0f, 0.6f);
        drawText(panelX + 10, yPos, text, GLUT_BITMAP_HELVETICA_12);
        yPos -= 22;
    }
    
    // 分隔线
    glColor4f(0.5f, 0.7f, 0.9f, 0.5f);
    glBegin(GL_LINES);
    glVertex2f(panelX + 10, yPos + 8);
    glVertex2f(panelX + panelW - 10, yPos + 8);
    glEnd();
    
    // 有效目标信息
    if (validTargetCount > 0) {
        sprintf(text, "Valid Targets: %d bottles", validTargetCount);
        glColor3f(0.3f, 1.0f, 0.5f);
        drawText(panelX + 10, yPos, text, GLUT_BITMAP_HELVETICA_12);
        yPos -= 16;
        
        // 列出目标瓶子
        char targetList[128] = "  -> #";
        for (int i = 0; i < validTargetCount && i < 6; i++) {
            char num[8];
            sprintf(num, "%d", validTargets[i] + 1);
            strcat(targetList, num);
            if (i < validTargetCount - 1 && i < 5) strcat(targetList, ", #");
        }
        if (validTargetCount > 6) strcat(targetList, "...");
        glColor3f(0.6f, 0.9f, 0.7f);
        drawText(panelX + 10, yPos, targetList, GLUT_BITMAP_HELVETICA_12);
        yPos -= 18;
        
        // 操作提示
        glColor3f(0.9f, 0.9f, 0.5f);
        drawText(panelX + 10, yPos, "Click target to pour!", GLUT_BITMAP_HELVETICA_12);
    } else {
        glColor3f(1.0f, 0.5f, 0.4f);
        drawText(panelX + 10, yPos, "No valid targets!", GLUT_BITMAP_HELVETICA_12);
        yPos -= 16;
        glColor3f(0.7f, 0.7f, 0.7f);
        drawText(panelX + 10, yPos, "Click elsewhere to cancel", GLUT_BITMAP_HELVETICA_12);
    }
    
    // ========== 在每个可以倒入的瓶子上方显示箭头和预览 ==========
    for (int i = 0; i < numBottles; i++) {
        if (i == selectedBottle) continue;
        if (!canPour(selectedBottle, i)) continue;
        
        // 计算可倒入的层数
        int pourAmount = calcPourAmount(selectedBottle, i);
        
        // 使用精确的屏幕坐标计算
        float indicatorX = getBottleScreenX(i);
        float indicatorY = getBottleScreenTopY(i) + 15;  // 在瓶子顶部上方15像素
        
        // 绘制向下箭头（更大更明显）
        float arrowSize = 15.0f;
        float bounce = 6.0f * sinf(hoverGlowTime * 4.0f);
        
        // 箭头发光效果
        glColor4f(0.2f, 0.8f, 0.4f, 0.3f);
        glBegin(GL_TRIANGLES);
        glVertex2f(indicatorX, indicatorY - arrowSize - 5 + bounce);
        glVertex2f(indicatorX - arrowSize - 5, indicatorY + arrowSize * 0.5f + 5 + bounce);
        glVertex2f(indicatorX + arrowSize + 5, indicatorY + arrowSize * 0.5f + 5 + bounce);
        glEnd();
        
        // 主箭头
        glColor4f(0.3f, 1.0f, 0.5f, 0.9f);
        glBegin(GL_TRIANGLES);
        glVertex2f(indicatorX, indicatorY - arrowSize + bounce);
        glVertex2f(indicatorX - arrowSize * 0.8f, indicatorY + arrowSize * 0.5f + bounce);
        glVertex2f(indicatorX + arrowSize * 0.8f, indicatorY + arrowSize * 0.5f + bounce);
        glEnd();
        
        // 箭头杆
        glBegin(GL_QUADS);
        glVertex2f(indicatorX - 4, indicatorY + arrowSize * 0.5f + bounce);
        glVertex2f(indicatorX + 4, indicatorY + arrowSize * 0.5f + bounce);
        glVertex2f(indicatorX + 4, indicatorY + arrowSize * 1.8f + bounce);
        glVertex2f(indicatorX - 4, indicatorY + arrowSize * 1.8f + bounce);
        glEnd();
        
        // 显示可倒入层数
        sprintf(text, "+%d", pourAmount);
        glColor3f(1.0f, 1.0f, 0.3f);
        drawText(indicatorX - 8, indicatorY + arrowSize * 2.2f + bounce, text, GLUT_BITMAP_HELVETICA_12);
        
        // 目标瓶子底部显示颜色预览
        float previewY = getBottleScreenBottomY(i) - 35;  // 在瓶子底部下方35像素
        Bottle* targetBottle = &bottles[i];
        
        // 预览框背景
        glColor4f(0.1f, 0.15f, 0.2f, 0.8f);
        glBegin(GL_QUADS);
        glVertex2f(indicatorX - 25, previewY - 25);
        glVertex2f(indicatorX + 25, previewY - 25);
        glVertex2f(indicatorX + 25, previewY + 5);
        glVertex2f(indicatorX - 25, previewY + 5);
        glEnd();
        
        // 预览框边框
        glColor4f(0.4f, 0.8f, 0.5f, 0.7f);
        glBegin(GL_LINE_LOOP);
        glVertex2f(indicatorX - 25, previewY - 25);
        glVertex2f(indicatorX + 25, previewY - 25);
        glVertex2f(indicatorX + 25, previewY + 5);
        glVertex2f(indicatorX - 25, previewY + 5);
        glEnd();
        
        // 显示倒入后的结果
        sprintf(text, "%d->%d", targetBottle->count, targetBottle->count + pourAmount);
        glColor3f(0.8f, 1.0f, 0.8f);
        drawText(indicatorX - 18, previewY - 12, text, GLUT_BITMAP_HELVETICA_12);
    }
}

void drawGameUI() {
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, winW, 0, winH);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor3f(0.85f, 0.88f, 0.92f);
    const char* levels[] = {"Easy", "Medium", "Hard"};
    char title[64];
    sprintf(title, "Water Sort - %s", levels[currentLevel-1]);
    drawText(20, (float)(winH - 28), title, GLUT_BITMAP_HELVETICA_18);

    // 右上角显示步数和连击
    char movesStr[64];
    if (comboPours > 1) {
        sprintf(movesStr, "Moves: %d  Combo: %d", playerMoves, comboPours);
    } else {
        sprintf(movesStr, "Moves: %d", playerMoves);
    }
    glColor3f(0.9f, 0.85f, 0.5f);
    drawText((float)(winW - 150), (float)(winH - 28), movesStr, GLUT_BITMAP_HELVETICA_18);

    glColor3f(0.45f, 0.48f, 0.52f);
    drawText(20, 18, "[R] Restart  [M] Menu  [Arrows] Camera", GLUT_BITMAP_HELVETICA_12);

    // 底部居中显示学号姓名（在瓶子下方，GLUT最大字体）
    glColor3f(0.7f, 0.85f, 0.95f);
    drawTextCentered(50, "2023212167 Mo Renying", GLUT_BITMAP_TIMES_ROMAN_24);

    // 绘制有效目标指示器
    drawValidTargetIndicators();
    
    // 绘制悬停提示
    if (hoveredBottle >= 0 && !animating) {
        drawTooltip(hoveredBottle);
    }

    // Show error message if any
    if (errorTimer > 0) {
        glColor3f(1.0f, 0.4f, 0.4f);
        drawTextCentered((float)(winH / 2 + 100), errorMsg, GLUT_BITMAP_HELVETICA_18);
    }

    glPopMatrix();;
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

void drawWinScreen() {
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, winW, 0, winH);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_BLEND);
    glColor4f(0, 0, 0, 0.75f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0); glVertex2f((float)winW, 0);
    glVertex2f((float)winW, (float)winH); glVertex2f(0, (float)winH);
    glEnd();

    // 标题
    glColor3f(0.35f, 1.0f, 0.55f);
    drawTextCentered((float)(winH/2 + 180), "CONGRATULATIONS!", GLUT_BITMAP_TIMES_ROMAN_24);
    glColor3f(0.9f, 0.92f, 0.95f);
    drawTextCentered((float)(winH/2 + 145), "You sorted all the liquids!", GLUT_BITMAP_HELVETICA_18);

    // 评分面板背景
    float panelLeft = (float)(winW/2 - 180);
    float panelRight = (float)(winW/2 + 180);
    float panelTop = (float)(winH/2 + 120);
    float panelBottom = (float)(winH/2 - 140);
    
    glColor4f(0.1f, 0.12f, 0.18f, 0.9f);
    glBegin(GL_QUADS);
    glVertex2f(panelLeft, panelBottom);
    glVertex2f(panelRight, panelBottom);
    glVertex2f(panelRight, panelTop);
    glVertex2f(panelLeft, panelTop);
    glEnd();
    
    // 面板边框
    glColor4f(0.4f, 0.6f, 0.8f, 0.6f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(panelLeft, panelBottom);
    glVertex2f(panelRight, panelBottom);
    glVertex2f(panelRight, panelTop);
    glVertex2f(panelLeft, panelTop);
    glEnd();

    char text[64];
    float y = panelTop - 30;
    float leftX = panelLeft + 20;
    float rightX = panelRight - 80;
    
    // 步数统计
    int best = bestMoves[currentLevel - 1];
    sprintf(text, "Your Moves: %d", playerMoves);
    glColor3f(0.9f, 0.85f, 0.5f);
    drawText(leftX, y, text, GLUT_BITMAP_HELVETICA_18);
    
    sprintf(text, "Best: %d", best);
    glColor3f(0.5f, 0.85f, 0.9f);
    drawText(rightX, y, text, GLUT_BITMAP_HELVETICA_18);
    y -= 30;
    
    // 时间
    float timeTaken = gameEndTime - gameStartTime;
    sprintf(text, "Time: %.1fs", timeTaken);
    glColor3f(0.8f, 0.8f, 0.85f);
    drawText(leftX, y, text, GLUT_BITMAP_HELVETICA_18);
    y -= 35;
    
    // 分隔线
    glColor4f(0.4f, 0.5f, 0.6f, 0.5f);
    glBegin(GL_LINES);
    glVertex2f(panelLeft + 15, y + 15);
    glVertex2f(panelRight - 15, y + 15);
    glEnd();
    
    // 评分细节
    glColor3f(0.7f, 0.75f, 0.8f);
    drawText(leftX, y, "Score Breakdown:", GLUT_BITMAP_HELVETICA_12);
    y -= 22;
    
    // 基础分
    sprintf(text, "Base Score:");
    glColor3f(0.75f, 0.78f, 0.82f);
    drawText(leftX, y, text, GLUT_BITMAP_HELVETICA_12);
    sprintf(text, "+%d", lastScore.baseScore);
    glColor3f(0.5f, 0.9f, 0.5f);
    drawText(rightX, y, text, GLUT_BITMAP_HELVETICA_12);
    y -= 18;
    
    // 时间奖励
    sprintf(text, "Time Bonus:");
    glColor3f(0.75f, 0.78f, 0.82f);
    drawText(leftX, y, text, GLUT_BITMAP_HELVETICA_12);
    sprintf(text, "+%d", lastScore.timeBonus);
    glColor3f(0.5f, 0.9f, 0.5f);
    drawText(rightX, y, text, GLUT_BITMAP_HELVETICA_12);
    y -= 18;
    
    // 连击奖励
    sprintf(text, "Combo Bonus (max %d):", maxCombo);
    glColor3f(0.75f, 0.78f, 0.82f);
    drawText(leftX, y, text, GLUT_BITMAP_HELVETICA_12);
    sprintf(text, "+%d", lastScore.comboBonus);
    glColor3f(0.5f, 0.9f, 0.5f);
    drawText(rightX, y, text, GLUT_BITMAP_HELVETICA_12);
    y -= 18;
    
    // 完美倒水奖励
    sprintf(text, "Perfect Pours (%d):", perfectPours);
    glColor3f(0.75f, 0.78f, 0.82f);
    drawText(leftX, y, text, GLUT_BITMAP_HELVETICA_12);
    sprintf(text, "+%d", lastScore.perfectBonus);
    glColor3f(0.5f, 0.9f, 0.5f);
    drawText(rightX, y, text, GLUT_BITMAP_HELVETICA_12);
    y -= 18;
    
    // 效率奖励
    sprintf(text, "Efficiency:");
    glColor3f(0.75f, 0.78f, 0.82f);
    drawText(leftX, y, text, GLUT_BITMAP_HELVETICA_12);
    sprintf(text, "+%d", lastScore.efficiencyBonus);
    glColor3f(0.5f, 0.9f, 0.5f);
    drawText(rightX, y, text, GLUT_BITMAP_HELVETICA_12);
    y -= 18;
    
    // 扣分（如果有）
    if (lastScore.penalty > 0) {
        sprintf(text, "Penalty:");
        glColor3f(0.75f, 0.78f, 0.82f);
        drawText(leftX, y, text, GLUT_BITMAP_HELVETICA_12);
        sprintf(text, "-%d", lastScore.penalty);
        glColor3f(0.9f, 0.5f, 0.5f);
        drawText(rightX, y, text, GLUT_BITMAP_HELVETICA_12);
        y -= 18;
    }
    
    y -= 10;
    
    // 分隔线
    glColor4f(0.4f, 0.5f, 0.6f, 0.5f);
    glBegin(GL_LINES);
    glVertex2f(panelLeft + 15, y + 8);
    glVertex2f(panelRight - 15, y + 8);
    glEnd();
    
    // 总分和评级
    sprintf(text, "Total Score: %d", lastScore.totalScore);
    glColor3f(1.0f, 0.9f, 0.4f);
    drawTextCentered(y - 15, text, GLUT_BITMAP_TIMES_ROMAN_24);
    
    // 评级（根据分数显示不同颜色）
    if (lastScore.totalScore >= 95) {
        glColor3f(1.0f, 0.85f, 0.2f);  // 金色
    } else if (lastScore.totalScore >= 85) {
        glColor3f(0.3f, 1.0f, 0.5f);   // 绿色
    } else if (lastScore.totalScore >= 70) {
        glColor3f(0.5f, 0.8f, 1.0f);   // 蓝色
    } else if (lastScore.totalScore >= 50) {
        glColor3f(0.9f, 0.7f, 0.3f);   // 橙色
    } else {
        glColor3f(0.8f, 0.5f, 0.5f);   // 红色
    }
    drawTextCentered(y - 50, lastScore.grade, GLUT_BITMAP_HELVETICA_18);

    // 提示
    glColor3f(0.6f, 0.75f, 0.85f);
    drawTextCentered((float)(winH/2 - 175), "Press R to play again or M for menu", GLUT_BITMAP_HELVETICA_12);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

// Forward declaration
void drawChatPanel();

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (gameState == STATE_MENU) { drawMenu(); glutSwapBuffers(); return; }
    if (gameState == STATE_RULES) { drawRules(); glutSwapBuffers(); return; }

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    float cx = camDist * sinf(camY * PI/180) * cosf(camX * PI/180);
    float cy = camDist * sinf(camX * PI/180) + 0.7f;
    float cz = camDist * cosf(camY * PI/180) * cosf(camX * PI/180);
    gluLookAt(cx, cy, cz, 0, 0.7f, 0, 0, 1, 0);

    setupLights();
    drawFloor();
    for (int i = 0; i < numBottles; i++) drawBottle(i, 0);

    if (animating && animPhase == 3) {
        Bottle* s = &bottles[animSrc];
        Bottle* t = &bottles[animTgt];
        float rad = s->tiltAngle * PI / 180.0f;
        float bx = s->x + s->moveX;
        float by = s->y + s->offsetY;
        
        // 精确计算瓶口边缘出水点
        // 旋转变换: 局部(lx, ly) → 世界(wx, wy)
        // θ = rad * tiltDir
        // wx = bx + lx*cos(θ) - ly*sin(θ)
        // wy = by + lx*sin(θ) + ly*cos(θ)
        // cos(rad*tiltDir) = cos(rad), sin(rad*tiltDir) = sin(rad)*tiltDir
        float neckEdgeOffset = NECK_RAD * 0.5f;
        float lx = -neckEdgeOffset * s->tiltDir;  // 出水侧偏移
        float ly = BOTTLE_TOP;
        
        float cosR = cosf(rad);
        float sinR = sinf(rad);
        float px = bx + lx * cosR - ly * sinR * s->tiltDir;
        float py = by + lx * sinR * s->tiltDir + ly * cosR;
        
        // 目标瓶的接收点：液面正上方
        float ty = t->y + t->visualLiquid * L_HGT + 0.08f;
        float prog = 0.6f + pourAmt * 0.4f;
        drawStream(px, py, t->x, ty, pourColor, prog);
    }

    // Demo模式显示特殊UI
    if (gameState == STATE_DEMO) {
        // 在切换到2D UI之前更新瓶子屏幕坐标
        updateBottleScreenPositions();
        drawGameUI();
        // 显示Demo提示
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        gluOrtho2D(0, winW, 0, winH);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        
        glColor3f(0.3f, 0.9f, 0.5f);
        drawTextCentered((float)(winH - 60), "DEMO MODE - Watch and Learn!", GLUT_BITMAP_HELVETICA_18);
        
        char stepStr[32];
        sprintf(stepStr, "Step %d / %d", demoStep, demoSolutionLen);
        glColor3f(0.9f, 0.85f, 0.5f);
        drawTextCentered((float)(winH - 90), stepStr, GLUT_BITMAP_HELVETICA_18);
        
        glColor3f(0.6f, 0.65f, 0.7f);
        drawTextCentered(80, "Press M to return to menu", GLUT_BITMAP_HELVETICA_12);
        
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_LIGHTING);
    } else {
        // 在切换到2D UI之前更新瓶子屏幕坐标
        updateBottleScreenPositions();
        drawGameUI();
    }
    
    if (gameState == STATE_WON) drawWinScreen();
    
    // AI聊天面板（覆盖在游戏画面上方）
    if (g_chatOpen) drawChatPanel();
    
    glutSwapBuffers();
}

// ========== Picking ==========
int pickBottle(int mx, int my) {
    unsigned char pix[3];
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    float cx = camDist * sinf(camY * PI/180) * cosf(camX * PI/180);
    float cy = camDist * sinf(camX * PI/180) + 0.7f;
    float cz = camDist * cosf(camY * PI/180) * cosf(camX * PI/180);
    gluLookAt(cx, cy, cz, 0, 0.7f, 0, 0, 1, 0);
    for (int i = 0; i < numBottles; i++) drawBottle(i, 1);
    glReadPixels(mx, winH - my, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pix);
    int idx = pix[0] / 20 - 1;
    return (idx >= 0 && idx < numBottles) ? idx : -1;
}

// ========== Input ==========
void mouseGame(int x, int y) {
    if (animating) return;
    int cl = pickBottle(x, y);
    if (cl >= 0) {
        if (selectedBottle < 0) {
            // Selecting a bottle
            if (bottles[cl].count > 0) {
                selectedBottle = cl;
                bottles[cl].offsetY = 0.15f;
            } else {
                showError("Empty bottle - nothing to pour!");
                comboPours = 0;  // 重置连击
            }
        } else {
            // Trying to pour
            if (cl == selectedBottle) {
                // Deselect
                bottles[selectedBottle].offsetY = 0;
                selectedBottle = -1;
            } else if (bottles[cl].count >= MAX_LAYERS) {
                // Target is full
                showError("Target bottle is full!");
                comboPours = 0;  // 重置连击
            } else if (bottles[selectedBottle].count == 0) {
                // Source is empty (shouldn't happen but safety check)
                showError("Selected bottle is empty!");
                bottles[selectedBottle].offsetY = 0;
                selectedBottle = -1;
                comboPours = 0;  // 重置连击
            } else if (!canPour(selectedBottle, cl)) {
                // Colors don't match
                showError("Colors don't match!");
                comboPours = 0;  // 重置连击
            } else {
                // Valid pour - start animation
                animSrc = selectedBottle;
                animTgt = cl;
                
                // Calculate animation parameters
                float dx = bottles[animTgt].x - bottles[animSrc].x;
                bottles[animSrc].tiltDir = (dx > 0) ? -1 : 1;
                animLiftHeight = calcLiftHeight(animSrc, animTgt);
                bottles[animSrc].targetMoveX = calcMoveX(animSrc, animTgt);
                
                animating = 1;
                animPhase = 0;
                pourAmt = 0;
                bottles[selectedBottle].offsetY = 0;
                selectedBottle = -1;
                
                playerMoves++;  // 增加步数
            }
        }
    } else {
        // Clicked empty space - deselect
        if (selectedBottle >= 0) {
            bottles[selectedBottle].offsetY = 0;
            selectedBottle = -1;
        }
    }
    glutPostRedisplay();
}

void mouseMenu(int x, int y) {
    float startY = (float)(winH / 2 + 80);
    for (int i = 0; i < 5; i++) {
        float itemY = startY - i * 50;
        if (y > (winH - itemY - 28) && y < (winH - itemY + 8)) {
            if (x > winW/2 - 120 && x < winW/2 + 120) {
                menuSelection = i;
                if (i == 0) { initEasy(); gameState = STATE_PLAYING; }
                else if (i == 1) { initMedium(); gameState = STATE_PLAYING; }
                else if (i == 2) { initHard(); gameState = STATE_PLAYING; }
                else if (i == 3) { initDemo(); }  // Demo模式
                else if (i == 4) { gameState = STATE_RULES; }
                glutPostRedisplay();
                return;
            }
        }
    }
}

void mouse(int btn, int st, int x, int y) {
    if (btn != GLUT_LEFT_BUTTON || st != GLUT_DOWN) return;
    switch (gameState) {
        case STATE_MENU: mouseMenu(x, y); break;
        case STATE_RULES: gameState = STATE_MENU; glutPostRedisplay(); break;
        case STATE_PLAYING: mouseGame(x, y); break;
        case STATE_DEMO: break;  // Demo模式不响应鼠标
        case STATE_WON: gameState = STATE_MENU; demoMode = 0; glutPostRedisplay(); break;
    }
}

// ========== AI Chat: Game State Serializer ==========
std::string getGameStateText() {
    std::string s;
    const char* stateNames[] = {"主菜单", "规则页面", "游戏中", "已通关", "Demo演示"};
    s += "当前状态: ";
    s += stateNames[(int)gameState];
    s += "\n";
    
    if (gameState == STATE_PLAYING || gameState == STATE_WON || gameState == STATE_DEMO) {
        const char* diffNames[] = {"简单(3色5瓶)", "中等(5色7瓶)", "困难(8色10瓶)"};
        s += "难度: ";
        s += diffNames[currentLevel - 1];
        s += "\n";
        s += "步数: " + std::to_string(playerMoves) + "\n";
        s += "瓶子状态:\n";
        for (int i = 0; i < numBottles; i++) {
            s += "  瓶" + std::to_string(i + 1) + ": [";
            for (int j = 0; j < bottles[i].count; j++) {
                int c = bottles[i].layers[j];
                if (c >= 0 && c < 10) s += colorNames[c];
                else s += "?";
                if (j < bottles[i].count - 1) s += ",";
            }
            s += "]";
            if (bottles[i].count == 0) s += "(空)";
            s += "\n";
        }
    }
    return s;
}

// ========== AI Chat: UI Rendering ==========
void drawChatPanel() {
    // 切换到2D正交投影
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, winW, 0, winH);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    
    // 面板尺寸和位置（右侧面板）
    float panelW = 360.0f;
    float panelX = (float)winW - panelW - 10.0f;
    float panelY = 10.0f;
    float panelH = (float)winH - 20.0f;
    float inputH = 30.0f;
    float headerH = 35.0f;
    
    // 半透明背景
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.05f, 0.07f, 0.12f, 0.92f);
    glBegin(GL_QUADS);
    glVertex2f(panelX, panelY);
    glVertex2f(panelX + panelW, panelY);
    glVertex2f(panelX + panelW, panelY + panelH);
    glVertex2f(panelX, panelY + panelH);
    glEnd();
    
    // 边框
    glColor4f(0.3f, 0.5f, 0.9f, 0.6f);
    glLineWidth(1.5f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(panelX, panelY);
    glVertex2f(panelX + panelW, panelY);
    glVertex2f(panelX + panelW, panelY + panelH);
    glVertex2f(panelX, panelY + panelH);
    glEnd();
    
    // 标题栏
    glColor4f(0.1f, 0.15f, 0.25f, 0.95f);
    glBegin(GL_QUADS);
    glVertex2f(panelX, panelY + panelH - headerH);
    glVertex2f(panelX + panelW, panelY + panelH - headerH);
    glVertex2f(panelX + panelW, panelY + panelH);
    glVertex2f(panelX, panelY + panelH);
    glEnd();
    
    glColor3f(0.4f, 0.7f, 1.0f);
    drawText(panelX + 12, panelY + panelH - 22, "AI Assistant (F1 close | Enter send)", GLUT_BITMAP_HELVETICA_12);
    
    // 输入框背景
    glColor4f(0.12f, 0.14f, 0.2f, 0.95f);
    glBegin(GL_QUADS);
    glVertex2f(panelX + 5, panelY + 5);
    glVertex2f(panelX + panelW - 5, panelY + 5);
    glVertex2f(panelX + panelW - 5, panelY + 5 + inputH);
    glVertex2f(panelX + 5, panelY + 5 + inputH);
    glEnd();
    
    // 输入文字
    glColor3f(0.85f, 0.9f, 0.95f);
    char inputDisplay[270];
    snprintf(inputDisplay, sizeof(inputDisplay), "> %s_", g_chatInput);
    drawText(panelX + 10, panelY + 17, inputDisplay, GLUT_BITMAP_HELVETICA_12);
    
    // 消息区域
    float msgAreaTop = panelY + panelH - headerH - 5;
    float msgAreaBottom = panelY + 5 + inputH + 5;
    float msgY = msgAreaBottom + g_chatScrollY;
    
    // 设置裁剪区域（防止文字溢出面板）
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)panelX, (int)msgAreaBottom, (int)panelW, (int)(msgAreaTop - msgAreaBottom));
    
    // 从下往上绘制消息（最新的在最下面）
    auto& history = g_aiAgent.getHistory();
    float lineH = 16.0f;
    
    // 先计算总高度
    float totalH = 0;
    for (int i = 0; i < (int)history.size(); i++) {
        const auto& msg = history[i];
        // 简单估算行数（每行约40个ASCII字符或20个中文字符）
        int estimatedLines = 1 + (int)msg.content.size() / 38;
        totalH += (estimatedLines + 1) * lineH;  // +1 for role label
    }
    
    // 如果消息超出可视区域，从底部开始渲染
    float startY = msgAreaTop - 5;
    if (totalH > (msgAreaTop - msgAreaBottom)) {
        startY = msgAreaBottom + totalH + g_chatScrollY;
    }
    
    float curY = startY;
    for (int i = 0; i < (int)history.size(); i++) {
        const auto& msg = history[i];
        
        // 角色标签
        if (msg.role == "user") {
            glColor3f(0.3f, 0.85f, 0.4f);
            drawText(panelX + 10, curY, "[You]", GLUT_BITMAP_HELVETICA_12);
        } else if (msg.role == "assistant") {
            glColor3f(0.4f, 0.7f, 1.0f);
            drawText(panelX + 10, curY, "[AI]", GLUT_BITMAP_HELVETICA_12);
        }
        curY -= lineH;
        
        // 消息内容（支持自动换行）
        if (msg.role == "user") glColor3f(0.8f, 0.85f, 0.8f);
        else glColor3f(0.82f, 0.85f, 0.9f);
        
        // 获取显示文本（支持打字机效果）
        std::string displayText = msg.content;
        if (msg.role == "assistant" && i == (int)history.size() - 1 && msg.displayedChars >= 0) {
            displayText = g_aiAgent.getVisibleLastReply();
        }
        
        // 简单的自动换行渲染
        const int maxCharsPerLine = 38;
        int pos = 0;
        int len = (int)displayText.size();
        while (pos < len) {
            // 处理UTF-8多字节字符的换行
            int lineEnd = pos;
            int charCount = 0;
            while (lineEnd < len && charCount < maxCharsPerLine) {
                unsigned char c = (unsigned char)displayText[lineEnd];
                if (c < 0x80) { lineEnd++; charCount++; }
                else if (c < 0xE0) { lineEnd += 2; charCount += 2; }
                else if (c < 0xF0) { lineEnd += 3; charCount += 2; }
                else { lineEnd += 4; charCount += 2; }
                if (lineEnd > len) lineEnd = len;
            }
            std::string line = displayText.substr(pos, lineEnd - pos);
            drawText(panelX + 15, curY, line.c_str(), GLUT_BITMAP_HELVETICA_12);
            curY -= lineH;
            pos = lineEnd;
        }
        curY -= 4;  // 消息间距
    }
    
    // 加载中指示器
    if (g_aiAgent.getState() == aiagent::AgentState::Querying) {
        glColor3f(0.9f, 0.8f, 0.3f);
        static int dots = 0;
        static float dotTimer = 0;
        dotTimer += 0.016f;
        if (dotTimer > 0.5f) { dotTimer = 0; dots = (dots + 1) % 4; }
        char loading[20] = "Thinking";
        for (int d = 0; d < dots; d++) strcat(loading, ".");
        drawText(panelX + 15, curY, loading, GLUT_BITMAP_HELVETICA_12);
    }
    
    glDisable(GL_SCISSOR_TEST);
    
    // 提示信息（如果没有历史消息）
    if (history.empty()) {
        glColor3f(0.5f, 0.55f, 0.6f);
        drawText(panelX + 30, panelY + panelH / 2 + 10,
                 "Ask me anything about the game!", GLUT_BITMAP_HELVETICA_12);
        drawText(panelX + 40, panelY + panelH / 2 - 10,
                 "e.g. How do I play?", GLUT_BITMAP_HELVETICA_12);
    }
    
    // 恢复矩阵
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

// ========== AI Chat: Send Message ==========
void chatSendMessage() {
    if (g_chatInputLen == 0 || !g_aiInitialized) return;
    if (g_aiAgent.getState() == aiagent::AgentState::Querying) return;
    
    std::string userMsg(g_chatInput);
    g_chatInput[0] = '\0';
    g_chatInputLen = 0;
    g_chatScrollY = 0;  // 重置滚动
    
    g_aiAgent.askAsync(userMsg, [](const std::string& reply, bool success) {
        if (!success) {
            printf("AI Agent error: %s\n", g_aiAgent.getLastError().c_str());
        }
        glutPostRedisplay();
    });
    glutPostRedisplay();
}

void keyboard(unsigned char k, int x, int y) {
    // AI聊天输入拦截
    if (g_chatOpen) {
        if (k == 27) {  // ESC关闭聊天
            g_chatOpen = false;
            glutPostRedisplay();
            return;
        }
        if (k == 13) {  // Enter发送
            chatSendMessage();
            return;
        }
        if (k == 8) {  // Backspace删除
            if (g_chatInputLen > 0) {
                // UTF-8回退：找到最后一个字符的起始位置
                int i = g_chatInputLen - 1;
                while (i > 0 && ((unsigned char)g_chatInput[i] & 0xC0) == 0x80) i--;
                g_chatInput[i] = '\0';
                g_chatInputLen = i;
            }
            glutPostRedisplay();
            return;
        }
        // 普通字符输入
        if (k >= 32 && g_chatInputLen < 250) {
            g_chatInput[g_chatInputLen++] = k;
            g_chatInput[g_chatInputLen] = '\0';
            glutPostRedisplay();
        }
        return;
    }
    
    // F1键不在unsigned char范围，用'/' 或 'h' 替代
    if (k == '/' || k == 'h' || k == 'H') {
        if (gameState == STATE_PLAYING || gameState == STATE_WON) {
            g_chatOpen = !g_chatOpen;
            glutPostRedisplay();
            return;
        }
    }
    
    if (gameState == STATE_MENU) {
        if (k == 13) {
            if (menuSelection == 0) { initEasy(); gameState = STATE_PLAYING; }
            else if (menuSelection == 1) { initMedium(); gameState = STATE_PLAYING; }
            else if (menuSelection == 2) { initHard(); gameState = STATE_PLAYING; }
            else if (menuSelection == 3) { initDemo(); }  // Demo模式
            else if (menuSelection == 4) { gameState = STATE_RULES; }
        } else if (k == 27) exit(0);
        glutPostRedisplay();
        return;
    }
    if (gameState == STATE_RULES) {
        if (k == 13 || k == 8 || k == 27) gameState = STATE_MENU;
        glutPostRedisplay();
        return;
    }
    if (gameState == STATE_DEMO) {
        // Demo模式下只能按M返回菜单
        if (k == 'm' || k == 'M' || k == 27) {
            demoMode = 0;
            animating = 0;
            gameState = STATE_MENU;
        }
        glutPostRedisplay();
        return;
    }
    switch (k) {
        case 'r': case 'R': restartLevel(); break;
        case 'm': case 'M': gameState = STATE_MENU; selectedBottle = -1; animating = 0; demoMode = 0; break;
        case 27: exit(0);
    }
    glutPostRedisplay();
}

void special(int k, int x, int y) {
    if (gameState == STATE_MENU) {
        if (k == GLUT_KEY_UP) menuSelection = (menuSelection - 1 + 5) % 5;
        else if (k == GLUT_KEY_DOWN) menuSelection = (menuSelection + 1) % 5;
        glutPostRedisplay();
        return;
    }
    if (gameState == STATE_PLAYING) {
        switch (k) {
            case GLUT_KEY_LEFT: camY -= 5; break;
            case GLUT_KEY_RIGHT: camY += 5; break;
            case GLUT_KEY_UP: camX += 3; break;
            case GLUT_KEY_DOWN: camX -= 3; break;
        }
        if (camX > 50) camX = 50;
        if (camX < 5) camX = 5;
        glutPostRedisplay();
    }
}

void reshape(int w, int h) {
    winW = w; winH = h;
    if (h == 0) h = 1;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(36, (float)w/h, 0.1f, 100);
    glMatrixMode(GL_MODELVIEW);
}

void init() {
    // 初始化GLEW（必须在创建OpenGL上下文之后）
    GLenum glewErr = glewInit();
    if (glewErr != GLEW_OK) {
        printf("GLEW initialization failed: %s\n", glewGetErrorString(glewErr));
        printf("Falling back to fixed-function pipeline for liquid rendering.\n");
        useShaderLiquid = 0;
    } else {
        printf("GLEW initialized successfully. OpenGL version: %s\n", glGetString(GL_VERSION));
        
        // 检查shader支持
        if (GLEW_VERSION_2_0) {
            printf("OpenGL 2.0+ supported, initializing liquid shader...\n");
            initLiquidShader();
        } else {
            printf("OpenGL 2.0 not supported, using fixed-function pipeline.\n");
            useShaderLiquid = 0;
        }
    }
    
    glClearColor(0.08f, 0.09f, 0.12f, 1);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHT1);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_NORMALIZE);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    glEnable(GL_MULTISAMPLE);
    float spec[] = {0.75f, 0.75f, 0.8f, 1};
    float shin[] = {70.0f};
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, spec);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, shin);
    
    // 初始化AI Agent
    printf("\nInitializing AI Chat Agent...\n");
    g_aiInitialized = g_aiAgent.init("ai_config.txt", "knowledge.txt");
    if (g_aiInitialized) {
        g_aiAgent.setSystemPrompt(
            "你是Water Sort Puzzle游戏的AI助手。用简洁友好的中文回答玩家问题。"
            "回答控制在3句话以内。必要时参考提供的游戏知识和当前游戏状态。"
        );
        g_aiAgent.setContextProvider(getGameStateText);
        g_aiAgent.ragTopK = 3;
        printf("AI Agent initialized successfully!\n");
    } else {
        printf("AI Agent init failed (config missing?). Chat disabled.\n");
    }
}

// ========== Mouse Motion (Hover) ==========
void passiveMotion(int x, int y) {
    if (gameState != STATE_PLAYING || animating) {
        if (hoveredBottle != -1) {
            hoveredBottle = -1;
            glutPostRedisplay();
        }
        return;
    }
    
    // 使用颜色拾取检测悬停的瓶子
    unsigned char pix[3];
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    float cx = camDist * sinf(camY * PI/180) * cosf(camX * PI/180);
    float cy = camDist * sinf(camX * PI/180) + 0.7f;
    float cz = camDist * cosf(camY * PI/180) * cosf(camX * PI/180);
    gluLookAt(cx, cy, cz, 0, 0.7f, 0, 0, 1, 0);
    
    // 绘制拾取用的瓶子
    for (int i = 0; i < numBottles; i++) {
        Bottle* b = &bottles[i];
        glPushMatrix();
        glTranslatef(b->x + b->moveX, b->y + b->offsetY, b->z);
        glDisable(GL_LIGHTING);
        glDisable(GL_BLEND);
        glColor3ub((i+1)*20, (i+1)*20, (i+1)*20);
        glPushMatrix();
        glRotatef(-90, 1, 0, 0);
        GLUquadric* q = gluNewQuadric();
        gluCylinder(q, B_RAD * 1.5f, B_RAD * 1.5f, B_HGT + 0.4f, 12, 1);
        glPushMatrix();
        glRotatef(180, 1, 0, 0);
        gluDisk(q, 0, B_RAD * 1.5f, 12, 1);
        glPopMatrix();
        glTranslatef(0, 0, B_HGT + 0.4f);
        gluDisk(q, 0, B_RAD * 1.5f, 12, 1);
        gluDeleteQuadric(q);
        glPopMatrix();
        glEnable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glPopMatrix();
    }
    
    glReadPixels(x, winH - y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pix);
    int newHovered = pix[0] / 20 - 1;
    if (newHovered < 0 || newHovered >= numBottles) newHovered = -1;
    
    if (newHovered != hoveredBottle) {
        hoveredBottle = newHovered;
        glutPostRedisplay();
    }
}

int main(int argc, char** argv) {
    printf("\n  WATER SORT PUZZLE\n\n");
    printf("  Controls:\n");
    printf("    Mouse - Select/Pour (Hover to highlight)\n");
    printf("    Arrows - Menu/Camera\n");
    printf("    R - Restart, M - Menu\n");
    printf("    H - AI Chat Assistant\n");
    printf("    ESC - Exit\n\n");

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH | GLUT_MULTISAMPLE);
    glutInitWindowSize(winW, winH);
    glutInitWindowPosition(80, 50);
    glutCreateWindow("Water Sort Puzzle");
    init();
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutMouseFunc(mouse);
    glutPassiveMotionFunc(passiveMotion);  // 鼠标悬停回调
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(special);
    glutTimerFunc(16, timerCB, 0);
    glutMainLoop();
    return 0;
}
