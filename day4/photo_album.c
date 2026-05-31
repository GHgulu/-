#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/input.h>

/* ==========================================
 * 硬件参数
 * ========================================== */
#define LCD_WIDTH  800
#define LCD_HEIGHT 480
#define LCD_SIZE   (LCD_WIDTH * LCD_HEIGHT * 4)

#define TOUCH_MAX_X 1024
#define TOUCH_MAX_Y 600

#define TOUCH_TO_LCD_X(tx) ((tx) * LCD_WIDTH  / TOUCH_MAX_X)
#define TOUCH_TO_LCD_Y(ty) ((ty) * LCD_HEIGHT / TOUCH_MAX_Y)

/* ==========================================
 * 程序状态机
 * ========================================== */
#define STATE_MAIN      0  /* 主界面 */
#define STATE_ALBUM     1  /* 相册浏览 */
#define STATE_UNLOCK    2  /* 开机解锁界面 */
#define STATE_PASSWORD  3  /* 密码锁界面 */

/* ==========================================
 * 音乐播放状态
 * ========================================== */
#define MUSIC_STOPPED 0
#define MUSIC_PLAYING 1
#define MUSIC_PAUSED  2

/* ==========================================
 * 图片路径
 * ========================================== */
#define ALBUM_DIR       "/Embeded/album/"
#define MAIN_FACE_PATH  ALBUM_DIR "face.bmp"
#define UNLOCK_PATH     ALBUM_DIR "unlock.bmp"
#define MUSIC_PATH      "/Embeded/faded.mp3"
#define PHOTO_COUNT     7

/* 密码锁参数 */
#define PASSWORD_CORRECT "123456"
#define PASSWORD_LEN     6

const char *photo_list[PHOTO_COUNT] = {
    ALBUM_DIR "c1.bmp",
    ALBUM_DIR "c2.bmp",
    ALBUM_DIR "c3.bmp",
    ALBUM_DIR "c4.bmp",
    ALBUM_DIR "c5.bmp",
    ALBUM_DIR "c6.bmp",
    ALBUM_DIR "c7.bmp"
};

/* 全局 LCD 显存指针 */
int *lcd_ptr = NULL;

/* ==========================================
 * 像素/图形绘制基础函数
 * ========================================== */
static inline void draw_pixel(int x, int y, unsigned int color)
{
    if (x >= 0 && x < LCD_WIDTH && y >= 0 && y < LCD_HEIGHT)
        lcd_ptr[y * LCD_WIDTH + x] = color;
}

static inline unsigned int get_pixel(int x, int y)
{
    if (x >= 0 && x < LCD_WIDTH && y >= 0 && y < LCD_HEIGHT)
        return (unsigned int)lcd_ptr[y * LCD_WIDTH + x];
    return 0;
}

void draw_filled_rect(int x0, int y0, int x1, int y1, unsigned int color)
{
    int x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            draw_pixel(x, y, color);
}

void draw_rect_border(int x0, int y0, int x1, int y1,
                      unsigned int color, int thickness)
{
    int t;
    for (t = 0; t < thickness; t++)
    {
        int l = x0 + t, r = x1 - t, u = y0 + t, d = y1 - t;
        int x;
        for (x = l; x <= r; x++) { draw_pixel(x, u, color); draw_pixel(x, d, color); }
        for (x = u; x <= d; x++) { draw_pixel(l, x, color); draw_pixel(r, x, color); }
    }
}

void draw_translucent_rect(int x0, int y0, int x1, int y1,
                           unsigned int overlay_color, int alpha)
{
    int x, y;
    unsigned char or = (overlay_color >> 16) & 0xFF;
    unsigned char og = (overlay_color >>  8) & 0xFF;
    unsigned char ob = (overlay_color >>  0) & 0xFF;

    for (y = y0; y <= y1; y++)
    {
        for (x = x0; x <= x1; x++)
        {
            unsigned int base = get_pixel(x, y);
            unsigned char br = (base >> 16) & 0xFF;
            unsigned char bg = (base >>  8) & 0xFF;
            unsigned char bb = (base >>  0) & 0xFF;
            draw_pixel(x, y,
                (((or * alpha + br * (255 - alpha)) / 255) << 16) |
                (((og * alpha + bg * (255 - alpha)) / 255) <<  8) |
                (((ob * alpha + bb * (255 - alpha)) / 255) <<  0));
        }
    }
}

void draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3,
                   unsigned int color)
{
    int min_x = x1 < x2 ? (x1 < x3 ? x1 : x3) : (x2 < x3 ? x2 : x3);
    int max_x = x1 > x2 ? (x1 > x3 ? x1 : x3) : (x2 > x3 ? x2 : x3);
    int min_y = y1 < y2 ? (y1 < y3 ? y1 : y3) : (y2 < y3 ? y2 : y3);
    int max_y = y1 > y2 ? (y1 > y3 ? y1 : y3) : (y2 > y3 ? y2 : y3);

    for (int y = min_y; y <= max_y; y++)
    {
        for (int x = min_x; x <= max_x; x++)
        {
            int d1 = (x - x2) * (y1 - y2) - (x1 - x2) * (y - y2);
            int d2 = (x - x3) * (y2 - y3) - (x2 - x3) * (y - y3);
            int d3 = (x - x1) * (y3 - y1) - (x3 - x1) * (y - y1);
            int neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
            int pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
            if (!(neg && pos))
                draw_pixel(x, y, color);
        }
    }
}

/* 绘制实心圆 */
void draw_filled_circle(int cx, int cy, int r, unsigned int color)
{
    int x, y;
    for (y = cy - r; y <= cy + r; y++)
        for (x = cx - r; x <= cx + r; x++)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r)
                draw_pixel(x, y, color);
}

/* 绘制空心圆环 */
void draw_circle_ring(int cx, int cy, int r, unsigned int color, int thickness)
{
    int x, y;
    int r_outer = r + thickness / 2;
    int r_inner = r - thickness / 2;
    for (y = cy - r_outer; y <= cy + r_outer; y++)
        for (x = cx - r_outer; x <= cx + r_outer; x++)
        {
            int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
            if (d2 <= r_outer * r_outer && d2 >= r_inner * r_inner)
                draw_pixel(x, y, color);
        }
}

/* 绘制圆角实心矩形（简单实现：本体加四个角球） */
void draw_rounded_rect(int x0, int y0, int x1, int y1, int r, unsigned int color)
{
    draw_filled_rect(x0 + r, y0, x1 - r, y1, color);         /* 中间横条 */
    draw_filled_rect(x0, y0 + r, x1, y1 - r, color);         /* 中间竖条 */
    draw_filled_circle(x0 + r, y0 + r, r, color);             /* 左上角 */
    draw_filled_circle(x1 - r, y0 + r, r, color);             /* 右上角 */
    draw_filled_circle(x0 + r, y1 - r, r, color);             /* 左下角 */
    draw_filled_circle(x1 - r, y1 - r, r, color);             /* 右下角 */
}

/* ==========================================
 * 图标绘制函数
 * ========================================== */

/* 左箭头（小号，用于翻页按钮） */
void draw_left_arrow(int cx, int cy, int size, unsigned int color)
{
    draw_triangle(cx + size / 2, cy - size,
                  cx + size / 2, cy + size,
                  cx - size / 2, cy, color);
}

/* 右箭头 */
void draw_right_arrow(int cx, int cy, int size, unsigned int color)
{
    draw_triangle(cx - size / 2, cy - size,
                  cx - size / 2, cy + size,
                  cx + size / 2, cy, color);
}

/* 播放三角 */
void draw_play_triangle(int cx, int cy, int size, unsigned int color)
{
    draw_triangle(cx - size / 3, cy - size,
                  cx - size / 3, cy + size,
                  cx + size * 2 / 3, cy, color);
}

/* 暂停双竖线 */
void draw_pause_bars(int cx, int cy, int size, unsigned int color)
{
    int bar_w = size / 3;
    int bar_h = size;
    draw_filled_rect(cx - size / 2, cy - bar_h, cx - size / 2 + bar_w, cy + bar_h, color);
    draw_filled_rect(cx + size / 2 - bar_w, cy - bar_h, cx + size / 2, cy + bar_h, color);
}

/* 房子图标（小号） */
void draw_home_icon(int cx, int cy, int size, unsigned int color)
{
    draw_triangle(cx, cy - size, cx - size, cy + size / 3, cx + size, cy + size / 3, color);
    draw_filled_rect(cx - size * 2 / 3, cy + size / 3, cx + size * 2 / 3, cy + size, color);
}

/* 音乐音符图标 */
void draw_music_note(int cx, int cy, int size, unsigned int color)
{
    /* 音符圆形头部 */
    draw_filled_circle(cx - size / 2, cy + size / 2, size / 2 + 1, color);
    /* 音符竖线 */
    draw_filled_rect(cx + size / 4, cy - size, cx + size / 4 + 2, cy + size / 2, color);
    /* 音符旗子 */
    draw_triangle(cx + size / 4 + 2, cy - size,
                  cx + size / 4 + 2, cy - size / 2,
                  cx + size, cy - size / 2, color);
}

/* ==========================================
 * 装饰图案（叠加在照片上增添趣味）
 * ========================================== */

/* 笑脸 — 画在照片右下角装饰 */
void draw_smiley(int cx, int cy, int r, unsigned int color)
{
    /* 脸 — 黄色填充圆 */
    draw_filled_circle(cx, cy, r, 0x00FFDD00);
    /* 轮廓 */
    draw_circle_ring(cx, cy, r, 0x00333300, 2);
    /* 左眼 */
    draw_filled_circle(cx - r / 3, cy - r / 4, r / 6, 0x00333300);
    /* 右眼 */
    draw_filled_circle(cx + r / 3, cy - r / 4, r / 6, 0x00333300);
    /* 嘴巴（弧形，用像素点近似） */
    for (int x = -r / 2; x <= r / 2; x++)
        for (int y = 0; y <= r / 3; y++)
            if (x * x + (y + r / 6) * (y + r / 6) >= r * r / 4 - 2 &&
                x * x + (y + r / 6) * (y + r / 6) <= r * r / 4 + 2)
                draw_pixel(cx + x, cy + r / 5 + y, 0x00333300);
}

/* 小星星装饰（纯整数绘制，无需 math.h） */
void draw_star_deco(int cx, int cy, int r, unsigned int color)
{
    /* 用上下两个叠加三角形拼出六角星（大衛星風格） */
    draw_triangle(cx, cy - r, cx - r * 3 / 4, cy + r / 2, cx + r * 3 / 4, cy + r / 2, color);
    draw_triangle(cx, cy + r, cx - r * 3 / 4, cy - r / 2, cx + r * 3 / 4, cy - r / 2, color);
    /* 中心小圆点 */
    draw_filled_circle(cx, cy, r / 4, color);
}

/* ==========================================
 * UI 叠加层（精致小巧的设计）
 * ========================================== */

/* 右上角音乐按钮 — 圆形小按钮 */
void draw_music_button(int music_state)
{
    int cx = 770, cy = 38, r = 24;
    unsigned int bg_color, icon_color;

    if (music_state == MUSIC_PLAYING)
    {
        bg_color   = 0x0022AA22;  /* 绿色底 — 播放中 */
        icon_color = 0x00FFFFFF;
    }
    else if (music_state == MUSIC_PAUSED)
    {
        bg_color   = 0x00DDAA00;  /* 橙色底 — 暂停中 */
        icon_color = 0x00FFFFFF;
    }
    else
    {
        bg_color   = 0x00555555;  /* 灰色底 — 未播放 */
        icon_color = 0x00CCCCCC;
    }

    /* 半透明底色圆 */
    draw_translucent_rect(cx - r, cy - r, cx + r, cy + r, 0x00333333, 80);
    /* 圆形按钮背景 */
    draw_filled_circle(cx, cy, r, bg_color);
    /* 边框 */
    draw_circle_ring(cx, cy, r, 0x00FFFFFF, 2);

    /* 图标 */
    if (music_state == MUSIC_PLAYING)
        draw_music_note(cx, cy, 12, icon_color);
    else if (music_state == MUSIC_PAUSED)
        draw_pause_bars(cx, cy, 10, icon_color);
    else
        draw_music_note(cx, cy, 12, icon_color);
}

/* ==========================================
 * 开机解锁界面 UI
 * 在 unlock.bmp 上叠加一个锁图标 + 提示文字横条
 * ========================================== */
void draw_unlock_ui()
{
    /* 锁体（矩形） */
    draw_rounded_rect(370, 200, 430, 280, 8, 0x00CCAA00);
    draw_rect_border(370, 200, 430, 280, 0x00FFFFFF, 2);
    /* 锁梁（半圆弧） */
    draw_filled_circle(400, 200, 22, 0x00CCAA00);
    draw_circle_ring(400, 200, 22, 0x00FFFFFF, 2);
    /* 锁孔 */
    draw_filled_circle(400, 245, 6, 0x00333300);
    /* 底部提示横条 */
    draw_translucent_rect(250, 360, 550, 400, 0x00333333, 100);
    draw_rect_border(250, 360, 550, 400, 0x00FFFFFF, 1);
    /* 右指三角（提示点击进入） */
    draw_play_triangle(400, 380, 14, 0x0000FF00);
}

/* ==========================================
 * 密码锁界面 UI
 * 4×3 数字键盘 + 密码圆点 + 标题
 * ========================================== */

/* 绘制单个键盘按键 */
void draw_key(int cx, int cy, int w, int h, unsigned int bg, unsigned int border)
{
    int x0 = cx - w / 2, y0 = cy - h / 2;
    int x1 = cx + w / 2, y1 = cy + h / 2;
    draw_rounded_rect(x0, y0, x1, y1, 6, bg);
    draw_rect_border(x0 + 1, y0 + 1, x1 - 1, y1 - 1, border, 1);
}

/* 在按键中央画数字（用简易 7 段数码管风格像素点） */
void draw_digit(int cx, int cy, int digit, unsigned int color)
{
    /* 简易 5×7 点阵数字（只画 0-9），尺寸约 18×24 像素 */
    /* 每个数字用 7 段管的方式：3 横条 + 4 竖条 */
    int x0 = cx - 9, y0 = cy - 12;
    /* 上横 */ int s_top = (digit != 1 && digit != 4);
    /* 中横 */ int s_mid = (digit != 0 && digit != 1 && digit != 7);
    /* 下横 */ int s_bot = (digit != 1 && digit != 4 && digit != 7);
    /* 左上 */ int s_lt = (digit != 1 && digit != 2 && digit != 3 && digit != 7);
    /* 左下 */ int s_lb = (digit == 0 || digit == 2 || digit == 6 || digit == 8);
    /* 右上 */ int s_rt = (digit != 5 && digit != 6);
    /* 右下 */ int s_rb = (digit != 2);

    if (s_top) draw_filled_rect(x0 + 3, y0,     x0 + 15, y0 + 2,  color);
    if (s_mid) draw_filled_rect(x0 + 3, y0 + 11, x0 + 15, y0 + 13, color);
    if (s_bot) draw_filled_rect(x0 + 3, y0 + 22, x0 + 15, y0 + 24, color);
    if (s_lt)  draw_filled_rect(x0,     y0 + 3,  x0 + 2,  y0 + 11, color);
    if (s_lb)  draw_filled_rect(x0,     y0 + 14, x0 + 2,  y0 + 21, color);
    if (s_rt)  draw_filled_rect(x0 + 16, y0 + 3,  x0 + 18, y0 + 11, color);
    if (s_rb)  draw_filled_rect(x0 + 16, y0 + 14, x0 + 18, y0 + 21, color);
}

/* 绘制删除符号 "←" */
void draw_delete_icon(int cx, int cy, unsigned int color)
{
    draw_left_arrow(cx, cy, 10, color);
}

/* 绘制确认符号 "✓" */
void draw_confirm_icon(int cx, int cy, unsigned int color)
{
    /* 简化对勾：两条短线 */
    draw_filled_rect(cx - 8, cy + 2, cx - 2, cy + 8, color);  /* 左斜 */
    draw_filled_rect(cx - 1, cy + 8, cx + 7, cy - 2, color);  /* 右斜 */
}

/* 绘制密码圆点（已输入几位就亮几个） */
void draw_password_dots(int count, int total)
{
    int i;
    int start_x = 400 - total * 18 + 9;  /* 居中 */
    int y = 80;

    for (i = 0; i < total; i++)
    {
        int cx = start_x + i * 36;
        if (i < count)
            draw_filled_circle(cx, y, 8, 0x0000FF00);   /* 已输入 — 绿色亮 */
        else
            draw_circle_ring(cx, y, 8, 0x00666666, 2);  /* 未输入 — 灰色空心 */
    }
}

/* 完整密码锁界面 */
void draw_password_ui(int pw_count)
{
    int i, j;
    /* 深色全屏半透明蒙版 */
    draw_translucent_rect(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, 0x00111111, 160);

    /* 顶部标题 "输入密码" — 用横条示意 */
    draw_filled_rect(300, 30, 500, 34, 0x00FFFFFF);

    /* 密码圆点 */
    draw_password_dots(pw_count, PASSWORD_LEN);

    /* ---- 4×3 键盘布局 ---- */
    /* 按键尺寸 95×64，间距 8px，左上角 (250, 100) */
    int kx0 = 250, ky0 = 100;
    int kw = 95, kh = 64, gap = 8;
    int key_labels[4][3] = {
        {1, 2, 3},
        {4, 5, 6},
        {7, 8, 9},
        {-1, 0, -2}   /* -1=删除, -2=确认 */
    };

    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 3; j++)
        {
            int cx = kx0 + j * (kw + gap) + kw / 2;
            int cy = ky0 + i * (kh + gap) + kh / 2;
            draw_key(cx, cy, kw, kh, 0x00333333, 0x00888888);

            int label = key_labels[i][j];
            if (label >= 0 && label <= 9)
                draw_digit(cx, cy, label, 0x00FFFFFF);
            else if (label == -1)
                draw_delete_icon(cx, cy, 0x00FF6666);
            else if (label == -2)
                draw_confirm_icon(cx, cy, 0x0000FF00);
        }
    }
}

void draw_main_ui()
{
    /* 中央进入按钮 — 小号圆角矩形 */
    int bx = 400, by = 240;
    draw_translucent_rect(310, 160, 490, 320, 0x00222222, 80);
    draw_rounded_rect(310, 160, 490, 320, 20, 0x00004422);
    draw_rect_border(310, 160, 490, 320, 0x00FFFFFF, 2);
    draw_play_triangle(bx - 8, by, 40, 0x0000FF00);

    /* 底部小字提示条 */
    draw_filled_rect(280, 370, 520, 374, 0x00888888);
}

void draw_album_ui()
{
    /* 左侧翻页 — 窄条 + 小箭头 */
    draw_translucent_rect(0, 0, 55, 480, 0x00111111, 50);
    draw_left_arrow(28, 240, 16, 0x00FFFFFF);

    /* 右侧翻页 — 窄条 + 小箭头 */
    draw_translucent_rect(745, 0, 800, 480, 0x00111111, 50);
    draw_right_arrow(772, 240, 16, 0x00FFFFFF);

    /* 底部返回按钮 — 小号圆角矩形 */
    draw_translucent_rect(360, 430, 440, 475, 0x00222222, 70);
    draw_rounded_rect(360, 430, 440, 475, 12, 0x00444444);
    draw_rect_border(360, 430, 440, 475, 0x00FFFFFF, 1);
    draw_home_icon(400, 452, 10, 0x00FFFF00);
}

/* 在相册照片上绘制装饰图案 */
void draw_photo_decorations(int photo_index)
{
    /* 每张照片右下角画一个笑脸，颜色随索引变化 */
    unsigned int colors[] = { 0x00FFDD00, 0x00FF88AA, 0x0088DDFF, 0x00FFAA44,
                               0x00AAFF66, 0x00FF66FF, 0x0066FFDD };
    draw_smiley(730, 410, 22, colors[photo_index]);

    /* 左上角小星星 */
    draw_star_deco(55, 40, 14, colors[(photo_index + 3) % PHOTO_COUNT]);
}

/* ==========================================
 * 音乐控制（通过 system() 调用 madplay）
 * ========================================== */
void music_control(int *music_state)
{
    if (*music_state == MUSIC_STOPPED)
    {
        /* 后台启动播放 */
        system("madplay " MUSIC_PATH " &");
        *music_state = MUSIC_PLAYING;
        printf("-> 音乐开始播放\n");
    }
    else if (*music_state == MUSIC_PLAYING)
    {
        /* 暂停 */
        system("killall -SIGSTOP madplay");
        *music_state = MUSIC_PAUSED;
        printf("-> 音乐暂停\n");
    }
    else if (*music_state == MUSIC_PAUSED)
    {
        /* 恢复 */
        system("killall -SIGCONT madplay");
        *music_state = MUSIC_PLAYING;
        printf("-> 音乐恢复播放\n");
    }
}

/* 停止音乐（程序使用，放在 cleanup 或退出时） */
void music_stop()
{
    system("killall -SIGINT madplay");
    printf("-> 音乐已停止\n");
}

/* 辅助：加载 BMP 到 32 位 RGB 缓冲区（已修正上下颠倒） */
int bmp_load_to_buf(const char *path, int *buf)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("打开 BMP 失败"); return -1; }

    lseek(fd, 54, SEEK_SET);

    char raw[LCD_WIDTH * LCD_HEIGHT * 3];
    int temp[LCD_WIDTH * LCD_HEIGHT];
    memset(raw, 0, sizeof(raw));
    memset(temp, 0, sizeof(temp));

    int ret = read(fd, raw, sizeof(raw));
    close(fd);
    if (ret < (int)sizeof(raw))
        printf("警告: 读取 %d 字节, 预期 %d\n", ret, (int)sizeof(raw));

    /* 24位 BGR → 32位 RGB */
    int i, j;
    for (i = 0, j = 0; i < LCD_WIDTH * LCD_HEIGHT; i++, j += 3)
        temp[i] = (raw[j + 2] << 16) | (raw[j + 1] << 8) | raw[j];

    /* 上下翻转修正 */
    int x, y;
    for (y = 0; y < LCD_HEIGHT; y++)
        for (x = 0; x < LCD_WIDTH; x++)
            buf[LCD_WIDTH * y + x] = temp[(LCD_HEIGHT - 1 - y) * LCD_WIDTH + x];

    return 0;
}

/* ==========================================
 * BMP 图片显示（无特效，用于主界面/解锁界面）
 * ========================================== */
void show_bmp(const char *path)
{
    int bmp_fd = open(path, O_RDONLY);
    if (bmp_fd < 0)
    {
        perror("打开 BMP 图片失败");
        printf("出错路径: %s\n", path);
        return;
    }

    lseek(bmp_fd, 54, SEEK_SET);

    char bmp_buf[LCD_WIDTH * LCD_HEIGHT * 3];
    int bytes_read = read(bmp_fd, bmp_buf, sizeof(bmp_buf));
    close(bmp_fd);

    if (bytes_read < (int)sizeof(bmp_buf))
        printf("警告: 只读取了 %d 字节，预期 %d 字节\n", bytes_read, (int)sizeof(bmp_buf));

    for (int y = 0; y < LCD_HEIGHT; y++)
    {
        for (int x = 0; x < LCD_WIDTH; x++)
        {
            int lcd_idx = y * LCD_WIDTH + x;
            int bmp_idx = ((LCD_HEIGHT - 1 - y) * LCD_WIDTH + x) * 3;
            unsigned char b = bmp_buf[bmp_idx + 0];
            unsigned char g = bmp_buf[bmp_idx + 1];
            unsigned char r = bmp_buf[bmp_idx + 2];
            lcd_ptr[lcd_idx] = (r << 16) | (g << 8) | b;
        }
    }

    printf("成功显示: %s\n", path);
}

/* ==========================================
 * 图片切换特效（11 种动画效果）
 * 共用 bmp_load_to_buf() 加载图片后，逐像素动画写入 lcd_ptr
 * ========================================== */

/* 特效编号 */
#define EFFECT_CIRCULAR_SPREAD   0   /* 圆形扩散 */
#define EFFECT_CIRCULAR_SHRINK   1   /* 圆形收缩 */
#define EFFECT_FLY_DOWN          2   /* 向下飞入 */
#define EFFECT_FLY_UP            3   /* 向上飞入 */
#define EFFECT_FLY_LEFT          4   /* 向左飞入 */
#define EFFECT_FLY_RIGHT         5   /* 向右飞入 */
#define EFFECT_H_BLINDS          6   /* 横百叶窗 */
#define EFFECT_V_BLINDS          7   /* 竖百叶窗 */
#define EFFECT_LR_MERGE          8   /* 左右相合 */
#define EFFECT_CENTER_SPREAD     9   /* 中间展开 */
#define EFFECT_DIAGONAL          10  /* 斜方块 */
#define EFFECT_COUNT             11

/* 1) 圆形扩散 */
void effect_circular_spread(const char *path)
{
    int buf[480 * 800];
    memset(buf, 0, sizeof(buf));
    if (bmp_load_to_buf(path, buf) < 0) return;

    int i, j, k;
    for (k = 0; k < 467; k += 3)
    {
        for (i = 0; i < 480; i++)
            for (j = 0; j < 800; j++)
                if ((j - 400) * (j - 400) + (i - 240) * (i - 240) <= k * k)
                    lcd_ptr[800 * i + j] = buf[800 * i + j];
    }
}

/* 2) 圆形收缩 */
void effect_circular_shrink(const char *path)
{
    int buf[480 * 800];
    memset(buf, 0, sizeof(buf));
    if (bmp_load_to_buf(path, buf) < 0) return;

    int i, j, k;
    for (k = 468; k >= 0; k -= 3)
    {
        for (i = 0; i < 480; i++)
            for (j = 0; j < 800; j++)
                if ((j - 400) * (j - 400) + (i - 240) * (i - 240) >= k * k)
                    lcd_ptr[800 * i + j] = buf[800 * i + j];
    }
}

/* 3) 向下飞入 */
void effect_fly_down(const char *path)
{
    int buf[480 * 800];
    memset(buf, 0, sizeof(buf));
    if (bmp_load_to_buf(path, buf) < 0) return;

    int i, j;
    for (i = 0; i < 480; i++)
    {
        for (j = 0; j < 800; j++)
            lcd_ptr[800 * i + j] = buf[800 * i + j];
        usleep(1000);
    }
}

/* 4) 向上飞入 */
void effect_fly_up(const char *path)
{
    int buf[480 * 800];
    memset(buf, 0, sizeof(buf));
    if (bmp_load_to_buf(path, buf) < 0) return;

    int i, j;
    for (i = 479; i >= 0; i--)
    {
        for (j = 0; j < 800; j++)
            lcd_ptr[800 * i + j] = buf[800 * i + j];
        usleep(500);
    }
}

/* 5) 向左飞入 */
void effect_fly_left(const char *path)
{
    int buf[480 * 800];
    memset(buf, 0, sizeof(buf));
    if (bmp_load_to_buf(path, buf) < 0) return;

    int i, j;
    for (j = 799; j >= 0; j--)
    {
        for (i = 0; i < 480; i++)
            lcd_ptr[800 * i + j] = buf[800 * i + j];
        usleep(500);
    }
}

/* 6) 向右飞入 */
void effect_fly_right(const char *path)
{
    int buf[480 * 800];
    memset(buf, 0, sizeof(buf));
    if (bmp_load_to_buf(path, buf) < 0) return;

    int i, j;
    for (j = 0; j < 800; j++)
    {
        for (i = 0; i < 480; i++)
            lcd_ptr[800 * i + j] = buf[800 * i + j];
        usleep(500);
    }
}

/* 7) 横百叶窗 */
void effect_horizontal_blinds(const char *path)
{
    int buf[480 * 800];
    memset(buf, 0, sizeof(buf));
    if (bmp_load_to_buf(path, buf) < 0) return;

    int i, j, k;
    for (j = 0; j < 100; j++)
    {
        for (i = 0; i < 480; i++)
            for (k = 0; k < 8; k++)
                lcd_ptr[800 * i + j + k * 100] = buf[800 * i + j + k * 100];
        usleep(500);
    }
}

/* 8) 竖百叶窗 */
void effect_vertical_blinds(const char *path)
{
    int buf[480 * 800];
    memset(buf, 0, sizeof(buf));
    if (bmp_load_to_buf(path, buf) < 0) return;

    int i, j, k;
    for (i = 0; i < 60; i++)
    {
        for (j = 0; j < 800; j++)
            for (k = 0; k < 8; k++)
                lcd_ptr[800 * (i + k * 60) + j] = buf[800 * (i + k * 60) + j];
        usleep(500);
    }
}

/* 9) 左右相合 */
void effect_left_right_merge(const char *path)
{
    int buf[480 * 800];
    memset(buf, 0, sizeof(buf));
    if (bmp_load_to_buf(path, buf) < 0) return;

    int block, i, j;
    for (block = 0; block < 10; block++)
    {
        for (i = 40 * block; i < (block + 1) * 40; i++)
            for (j = 0; j < 480; j++)
                lcd_ptr[j * 800 + i] = buf[j * 800 + i];
        for (i = 799 - block * 40; i > 799 - (block + 1) * 40; i--)
            for (j = 0; j < 480; j++)
                lcd_ptr[j * 800 + i] = buf[j * 800 + i];
        usleep(100000);
    }
}

/* 10) 中间展开 */
void effect_center_spread(const char *path)
{
    int buf[480 * 800];
    memset(buf, 0, sizeof(buf));
    if (bmp_load_to_buf(path, buf) < 0) return;

    int i, j;
    for (i = 0; i < 400; i++)
    {
        for (j = 0; j < 480; j++)
        {
            lcd_ptr[800 * j + 400 + i] = buf[800 * j + 400 + i];
            lcd_ptr[800 * j + 399 - i] = buf[800 * j + 399 - i];
        }
        usleep(500);
    }
}

/* 11) 斜方块 */
void effect_diagonal_blocks(const char *path)
{
    int buf[480 * 800];
    memset(buf, 0, sizeof(buf));
    if (bmp_load_to_buf(path, buf) < 0) return;

    int i, j, k, m, n;
    for (k = 0; k <= 14; k++)
    {
        for (i = 0; i < 8; i++)
        {
            for (j = 0; j < 8; j++)
            {
                if (i + j <= k)
                {
                    for (m = 100 * i; m < 100 * (i + 1); m++)
                        for (n = 60 * j; n < 60 * (j + 1); n++)
                            lcd_ptr[n * 800 + m] = buf[n * 800 + m];
                    usleep(200);
                }
            }
        }
    }
}

/* 特效分发函数：根据 effect_id 调用对应特效 */
void show_bmp_with_effect(const char *path, int effect_id)
{
    printf("特效切换(%d): %s\n", effect_id, path);

    switch (effect_id)
    {
        case EFFECT_CIRCULAR_SPREAD:  effect_circular_spread(path);    break;
        case EFFECT_CIRCULAR_SHRINK:  effect_circular_shrink(path);    break;
        case EFFECT_FLY_DOWN:         effect_fly_down(path);            break;
        case EFFECT_FLY_UP:           effect_fly_up(path);              break;
        case EFFECT_FLY_LEFT:         effect_fly_left(path);            break;
        case EFFECT_FLY_RIGHT:        effect_fly_right(path);           break;
        case EFFECT_H_BLINDS:         effect_horizontal_blinds(path);   break;
        case EFFECT_V_BLINDS:         effect_vertical_blinds(path);     break;
        case EFFECT_LR_MERGE:         effect_left_right_merge(path);    break;
        case EFFECT_CENTER_SPREAD:    effect_center_spread(path);       break;
        case EFFECT_DIAGONAL:         effect_diagonal_blocks(path);     break;
        default:                      effect_circular_spread(path);     break;
    }
    printf("特效完成\n");
}

/* ==========================================
 * 判断坐标是否在矩形区域内
 * ========================================== */
int in_rect(int x, int y, int rx0, int ry0, int rx1, int ry1)
{
    return (x >= rx0 && x <= rx1 && y >= ry0 && y <= ry1);
}

/* ==========================================
 * 统一页面切换：显示图片 + 叠加 UI + 装饰
 * ========================================== */
void show_page(const char *bmp_path, int state, int music_state, int photo_index,
               int pw_count)
{
    show_bmp(bmp_path);

    /* 各状态专属 UI */
    if (state == STATE_UNLOCK)
    {
        draw_unlock_ui();
        return;
    }
    if (state == STATE_PASSWORD)
    {
        draw_password_ui(pw_count);
        return;
    }

    /* 相册状态下叠加装饰图案 */
    if (state == STATE_ALBUM)
        draw_photo_decorations(photo_index);

    /* 叠加音乐按钮（主界面 + 相册） */
    draw_music_button(music_state);

    if (state == STATE_MAIN)
        draw_main_ui();
    else
        draw_album_ui();
}

/* ==========================================
 * 主函数
 * ========================================== */
int main()
{
    /* ---- 1. 初始化 LCD ---- */
    int lcd_fd = open("/dev/fb0", O_RDWR);
    if (lcd_fd < 0) { perror("打开 /dev/fb0 失败"); return -1; }

    lcd_ptr = (int *)mmap(NULL, LCD_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, lcd_fd, 0);
    if (lcd_ptr == MAP_FAILED) { perror("mmap 映射失败"); close(lcd_fd); return -1; }

    /* ---- 2. 初始化触摸屏 ---- */
    int touch_fd = open("/dev/input/event0", O_RDONLY);
    if (touch_fd < 0) { perror("打开触摸屏失败"); munmap(lcd_ptr, LCD_SIZE); close(lcd_fd); return -1; }

    /* ---- 3. 初始状态 ---- */
    int state       = STATE_UNLOCK;
    int photo_index = 0;
    int music_state = MUSIC_STOPPED;
    char password_input[PASSWORD_LEN + 1] = {0};  /* 密码输入缓冲区 */
    int  pw_count = 0;                             /* 已输入密码位数 */
    int  effect_id = 0;                            /* 当前特效编号（循环使用） */

    show_page(UNLOCK_PATH, state, music_state, photo_index, pw_count);
    printf("=== 数码相册启动 ===\n");
    printf("触摸锁图标以进入密码界面\n");

    /* ---- 4. 触摸事件循环 ---- */
    struct input_event ev;
    int lcd_x = 0, lcd_y = 0;
    int touch_down = 0;

    while (1)
    {
        if (read(touch_fd, &ev, sizeof(ev)) != sizeof(ev))
            continue;

        /* 捕获坐标（无论按压状态，始终更新最新坐标） */
        if (ev.type == EV_ABS && ev.code == ABS_X)
            lcd_x = TOUCH_TO_LCD_X(ev.value);
        if (ev.type == EV_ABS && ev.code == ABS_Y)
            lcd_y = TOUCH_TO_LCD_Y(ev.value);

        /* 检测触摸释放：touch_down 从 1 变为 0 的下降沿 */
        if (ev.type == EV_KEY && ev.code == BTN_TOUCH)
        {
            int was_down = touch_down;
            touch_down = ev.value;
            if (was_down == 1 && touch_down == 0)  /* 手指抬起的瞬间 */
            {
            printf("[触摸] (%d, %d)\n", lcd_x, lcd_y);

            /* ---- 音乐按钮（右上角圆形按钮，所有页面共用） ---- */
            if (in_rect(lcd_x, lcd_y, 740, 5, 800, 75))
            {
                int prev_music = music_state;
                music_control(&music_state);
                if (music_state != prev_music)
                {
                    /* 只刷新音乐按钮区域（局部更新） */
                    draw_music_button(music_state);
                    printf("  音乐状态: %d\n", music_state);
                }
                continue;
            }

            /* ---- 开机解锁界面 ---- */
            if (state == STATE_UNLOCK)
            {
                /* 点击锁图标区域 -> 进入密码界面 */
                if (in_rect(lcd_x, lcd_y, 250, 180, 550, 420))
                {
                    show_bmp(UNLOCK_PATH);
                    draw_password_ui(0);
                    state = STATE_PASSWORD;
                    pw_count = 0;
                    printf("-> 进入密码锁界面\n");
                }
            }
            /* ---- 密码锁界面 ---- */
            else if (state == STATE_PASSWORD)
            {
                int kx0 = 250, ky0 = 100;
                int kw = 95, kh = 64, gap = 8;
                int key_found = 0;

                for (int i = 0; i < 4 && !key_found; i++)
                {
                    for (int j = 0; j < 3 && !key_found; j++)
                    {
                        int kx = kx0 + j * (kw + gap);
                        int ky = ky0 + i * (kh + gap);
                        if (in_rect(lcd_x, lcd_y, kx, ky, kx + kw, ky + kh))
                        {
                            /* 数字键 0-9 */
                            if (i < 3 || j == 1)
                            {
                                int digit = (i < 3) ? (i * 3 + j + 1) : 0;
                                if (pw_count < PASSWORD_LEN)
                                {
                                    password_input[pw_count++] = '0' + digit;
                                    draw_password_dots(pw_count, PASSWORD_LEN);
                                }
                            }
                            /* 删除键（左下角） */
                            else if (j == 0)
                            {
                                if (pw_count > 0)
                                {
                                    pw_count--;
                                    password_input[pw_count] = '\0';
                                    draw_password_dots(pw_count, PASSWORD_LEN);
                                }
                            }
                            /* 确认键（右下角） */
                            else if (j == 2)
                            {
                                if (strcmp(password_input, PASSWORD_CORRECT) == 0)
                                {
                                    printf("-> 密码正确，进入主界面！\n");
                                    show_page(MAIN_FACE_PATH, STATE_MAIN, music_state, 0, pw_count);
                                    state = STATE_MAIN;
                                }
                                else
                                {
                                    printf("-> 密码错误！请重试\n");
                                    /* 错误提示：全屏红色闪一下 */
                                    draw_translucent_rect(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, 0x00FF0000, 120);
                                    /* 中央大叉号 */
                                    draw_filled_rect(330, 180, 470, 300, 0x00FF0000);
                                    draw_rect_border(330, 180, 470, 300, 0x00FFFFFF, 3);
                                    /* "ERR" 横条 */
                                    draw_filled_rect(350, 220, 450, 228, 0x00FFFFFF);
                                    draw_filled_rect(350, 240, 450, 248, 0x00FFFFFF);
                                    draw_filled_rect(350, 255, 450, 263, 0x00FFFFFF);
                                    usleep(600000);
                                    /* 清空重置 */
                                    pw_count = 0;
                                    memset(password_input, 0, sizeof(password_input));
                                    draw_password_ui(0);
                                }
                            }
                            key_found = 1;
                        }
                    }
                }
            }
            /* ---- 主界面逻辑 ---- */
            else if (state == STATE_MAIN)
            {
                /* 中央进入按钮 */
                if (in_rect(lcd_x, lcd_y, 310, 160, 490, 320))
                {
                    photo_index = 0;
                    show_bmp_with_effect(photo_list[photo_index], effect_id);
                    effect_id = (effect_id + 1) % EFFECT_COUNT;
                    draw_album_ui();
                    draw_photo_decorations(photo_index);
                    draw_music_button(music_state);
                    state = STATE_ALBUM;
                    printf("-> 进入相册，第 %d/%d 张，特效 %d\n", photo_index + 1, PHOTO_COUNT, effect_id - 1);
                }
            }
            /* ---- 相册浏览逻辑 ---- */
            else if (state == STATE_ALBUM)
            {
                /* 左侧窄条 — 上一张 */
                if (in_rect(lcd_x, lcd_y, 0, 0, 55, 480))
                {
                    photo_index--;
                    if (photo_index < 0) photo_index = PHOTO_COUNT - 1;
                    show_bmp_with_effect(photo_list[photo_index], effect_id);
                    effect_id = (effect_id + 1) % EFFECT_COUNT;
                    draw_album_ui();
                    draw_photo_decorations(photo_index);
                    draw_music_button(music_state);
                    printf("-> 上一张，第 %d/%d 张\n", photo_index + 1, PHOTO_COUNT);
                }
                /* 右侧窄条 — 下一张 */
                else if (in_rect(lcd_x, lcd_y, 745, 0, 800, 480))
                {
                    photo_index++;
                    if (photo_index >= PHOTO_COUNT) photo_index = 0;
                    show_bmp_with_effect(photo_list[photo_index], effect_id);
                    effect_id = (effect_id + 1) % EFFECT_COUNT;
                    draw_album_ui();
                    draw_photo_decorations(photo_index);
                    draw_music_button(music_state);
                    printf("-> 下一张，第 %d/%d 张\n", photo_index + 1, PHOTO_COUNT);
                }
                /* 底部返回按钮 — 返回主界面 */
                else if (in_rect(lcd_x, lcd_y, 360, 430, 440, 475))
                {
                    show_page(MAIN_FACE_PATH, STATE_MAIN, music_state, 0, pw_count);
                    state = STATE_MAIN;
                    printf("-> 返回主界面\n");
                }
            }
            }  /* 闭合 was_down == 1 */
        }      /* 闭合 EV_KEY */
    }          /* 闭合 while */

    /* ---- 5. 释放资源 ---- */
    music_stop();
    close(touch_fd);
    munmap(lcd_ptr, LCD_SIZE);
    close(lcd_fd);
    return 0;
}
