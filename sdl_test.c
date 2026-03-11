#include <SDL2/SDL.h>
 
int main(int argc, char* argv[]) {
    // 定义窗口尺寸
    const int SCREEN_WIDTH = 640;
    const int SCREEN_HEIGHT = 480;
 
    // 1. 初始化 SDL 视频子系统
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL 初始化失败: %s\n", SDL_GetError());
        return 1;
    }
 
    // 2. 创建应用程序窗口
    SDL_Window* window = SDL_CreateWindow(
        "我的第一个 SDL2 窗口",          // 窗口标题
        SDL_WINDOWPOS_UNDEFINED,       // 初始 x 位置（屏幕居中）
        SDL_WINDOWPOS_UNDEFINED,       // 初始 y 位置
        SCREEN_WIDTH,                  // 宽度，单位像素
        SCREEN_HEIGHT,                 // 高度，单位像素
        SDL_WINDOW_SHOWN               // 窗口创建后立即显示
    );
 
    if (window == NULL) {
        printf("窗口创建失败: %s\n", SDL_GetError());
        SDL_Quit(); // 清理 SDL
        return 1;
    }
 
    // 3. 获取窗口关联的“表面”（Surface），并填充为白色
    SDL_Surface* screenSurface = SDL_GetWindowSurface(window);
    SDL_FillRect(screenSurface, NULL, SDL_MapRGB(screenSurface->format, 0xFF, 0xFF, 0xFF));
    SDL_UpdateWindowSurface(window); // 更新窗口，使填充生效
 
    // 4. 让窗口保持显示一段时间（这里用简单延时）
    SDL_Delay(3000); // 延迟 3000 毫秒，即3秒
 
    // 5. 程序结束，销毁窗口并退出 SDL
    SDL_DestroyWindow(window);
    SDL_Quit();
 
    return 0;
}