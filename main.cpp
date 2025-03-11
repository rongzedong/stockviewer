/***
2025 年 3月 11日 作者 荣泽东

代码主要实现了 从 网上获取 股票行情的 png文件 并把背景设置为透明色，置顶显示在前台，工作的时候也可以偶尔摸鱼看看股票的走势。

bug:
目前可能存在一些bug， 在某些时候可能会因为某些错误 异常退出需要修改完善。

*/
#include <windows.h>
#include <gdiplus.h>
#include <wininet.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "json.hpp"
// #include <ctime> // 包含 <ctime> 头文件以使用 time 和 localtime_s

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")

using json = nlohmann::json;
using namespace Gdiplus;

// 定时器ID
#define TIMER_ID 1

// 用于保存图片数据的结构体
struct ImageData {
    std::vector<BYTE> data;
};

// 用于保存配置参数的结构体
struct Config {
    std::vector<std::wstring> stockIds;
    int windowX;
    int windowY;
    std::wstring referer; // 增加 referer 参数
    std::wstring baseUrl; // 增加 base_url 参数
};

// 全局变量
Config config;

// 窗口过程函数声明
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// 初始化GDI+的帮助函数
void InitGDIPlus(GdiplusStartupInput& gdiplusStartupInput, ULONG_PTR& gdiplusToken) {
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}

// 清理GDI+的帮助函数
void CleanupGDIPlus(ULONG_PTR gdiplusToken) {
    GdiplusShutdown(gdiplusToken);
}


// 使用WinINet下载图片的函数
bool DownloadImage(const std::wstring& url, ImageData& imgData, const std::wstring& referer) {
    HINTERNET hInternet = InternetOpenW(L"Image Downloader", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) {
        return false;
    }

    HINTERNET hConnect = InternetOpenUrlW(hInternet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hConnect) {
        InternetCloseHandle(hInternet);
        return false;
    }
    if (!referer.empty()) {
        HttpAddRequestHeadersW(hConnect, (L"Referer: " + referer).c_str(), -1L, HTTP_ADDREQ_FLAG_ADD | HTTP_ADDREQ_FLAG_REPLACE);
    }

    char buffer[4096];
    DWORD bytesRead;
    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead != 0) {
        imgData.data.insert(imgData.data.end(), buffer, buffer + bytesRead);
    }

    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    return true;
}

// 将原始图片数据转换为GDI+ Image的函数
Image* LoadImageFromMemory(const ImageData& imgData) {
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, imgData.data.size());
    if (hGlobal) {
        void* pData = GlobalLock(hGlobal);
        if (pData) {
            memcpy(pData, imgData.data.data(), imgData.data.size());
            GlobalUnlock(hGlobal);

            IStream* pStream = NULL;
            if (CreateStreamOnHGlobal(hGlobal, TRUE, &pStream) == S_OK) {
                Image* image = new Image(pStream);
                pStream->Release();
                return image;
            }
        }
        GlobalFree(hGlobal);
    }
    return NULL;
}

// 使用新图片更新分层窗口的函数
void UpdateLayeredWindowWithImages(HWND hwnd, const std::vector<Image*>& images) {
    if (images.empty()) return;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    int totalHeight = 0;
    int imageWidth = 0;

    // 计算总高度和宽度
    for (Image* image : images) {
        totalHeight += image->GetHeight();
        imageWidth = max(imageWidth, static_cast<int>(image->GetWidth()));
    }

    BITMAPINFO bmpInfo = {0};
    bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmpInfo.bmiHeader.biWidth = imageWidth;
    bmpInfo.bmiHeader.biHeight = -totalHeight; // 负值表示自上而下的DIB
    bmpInfo.bmiHeader.biPlanes = 1;
    bmpInfo.bmiHeader.biBitCount = 32;
    bmpInfo.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBitmap = CreateDIBSection(hdcScreen, &bmpInfo, DIB_RGB_COLORS, &pBits, NULL, 0);
    SelectObject(hdcMem, hBitmap);

    Graphics graphics(hdcMem);

    // 绘制所有图像
    int yOffset = 0;
    for (Image* image : images) {
        graphics.DrawImage(image, 0, yOffset);
        yOffset += image->GetHeight();
    }

    SIZE sizeWindow = { imageWidth, totalHeight };
    POINT pointSource = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    POINT ptZero = { 0, 0 };

    UpdateLayeredWindow(hwnd, hdcScreen, NULL, &sizeWindow, hdcMem, &pointSource, 0, &blend, ULW_ALPHA);

    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    // 调整窗口高度以适应所有图片
    RECT rect;
    GetWindowRect(hwnd, &rect);
    SetWindowPos(hwnd, HWND_TOPMOST, rect.left, rect.top, imageWidth, totalHeight, SWP_NOZORDER | SWP_SHOWWINDOW);
}

// 从JSON文件读取配置的函数
Config GetConfig() {
    std::ifstream file("config.json");
    if (!file.is_open()) {
        MessageBoxW(NULL, L"无法打开 config.json", L"错误", MB_ICONERROR);
        return {{}, 50, 50, L"", L""}; // 默认值设为50，并初始化 referer 和 base_url 为空字符串
    }

    json j;
    try {
        file >> j;
    } catch (json::parse_error& e) {
        std::string errorMsg = "解析 config.json 时出错: ";
        errorMsg += e.what();
        MessageBoxA(NULL, errorMsg.c_str(), "错误", MB_ICONERROR);
        
        return {{}, 50, 50, L"", L""}; // 默认值设为50，并初始化 referer 和 base_url 为空字符串
    }
    file.close();

    Config config;
    config.windowX = 50; // 默认值设为50
    config.windowY = 50; // 默认值设为50

    try {
        if (j.contains("stock_list") && j["stock_list"].is_array()) {
            for (const std::string& stockId : j["stock_list"]) {
                config.stockIds.push_back(std::wstring(stockId.begin(), stockId.end()));
            }
        } else {
            MessageBoxW(NULL, L"stock_list 不是数组或缺失", L"错误", MB_ICONERROR);
        }
        
        if (j.contains("window_position") && j["window_position"].is_object()) {
            if (j["window_position"].contains("x")) {
                config.windowX = j["window_position"]["x"];
            } else {
                config.windowX = 50; // 设置默认值
            }
            if (j["window_position"].contains("y")) {
                config.windowY = j["window_position"]["y"];
            } else {
                config.windowY = 50; // 设置默认值
            }

        } else {
            config.windowX = 50; // 设置默认值
            config.windowY = 50; // 设置默认值
        }

        if (j.contains("referer") && j["referer"].is_string()) {
            std::string referer = j["referer"];
            config.referer = std::wstring(referer.begin(), referer.end());
        } else {
            config.referer = L""; // 设置默认值
        }

        if (j.contains("base_url") && j["base_url"].is_string()) {
            std::string baseUrl = j["base_url"];
            config.baseUrl = std::wstring(baseUrl.begin(), baseUrl.end());
        } else {
            config.baseUrl = L""; // 设置默认值
        }
    } catch (json::type_error& e) {
        std::string errorMsg = "读取 config.json 时出错: ";
        errorMsg += e.what();
                 MessageBoxA(NULL, errorMsg.c_str(), "错误", MB_ICONERROR);
        config = {{}, 50, 50, L"", L""}; // 恢复默认值
    }

    return config;
}


// 将窗口位置保存到JSON文件的函数
void SaveWindowPosition(int x, int y) {
    json j;
    j["stock_list"] = json::array();
    for (const auto& stockId : config.stockIds) {
        j["stock_list"].push_back(std::string(stockId.begin(), stockId.end()));
    }
    j["window_position"] = {{"x", x}, {"y", y}};
    j["referer"] = std::string(config.referer.begin(), config.referer.end());
    j["base_url"] = std::string(config.baseUrl.begin(), config.baseUrl.end());

    std::ofstream fileOut("config.json");
    fileOut << j.dump(4);
    fileOut.close();
}

void UpdateConfig() {
    config = GetConfig();
}

void DownloadAndDisplayImages(HWND hwnd) {
    UpdateConfig();
    if (config.stockIds.empty() || config.baseUrl.empty()) {
         MessageBoxW(NULL, L"config.stockIds.empty() || config.baseUrl.empty()", L"替换后的 URL", MB_OK);
        return;
    }

    std::vector<Image*> images;

    for (const auto& stockId : config.stockIds) {
        std::wstring url = config.baseUrl;
        std::wstring::size_type pos = url.find(L"{stock_id}");
        if (pos != std::wstring::npos) {
            url.replace(pos, wcslen(L"{stock_id}"), std::wstring(stockId.begin(), stockId.end()));
        }

        // 显示替换后的 URL
        // MessageBoxW(NULL, url.c_str(), L"替换后的 URL", MB_OK);


        ImageData imgData;
        if (DownloadImage(url, imgData, config.referer)) {
            Image* image = LoadImageFromMemory(imgData);
            if (image) {
                images.push_back(image);

            }
        }
    }

    // 使用新图片更新分层窗口
    UpdateLayeredWindowWithImages(hwnd, images);

    // 清理图像对象
    for (auto image : images) {
        delete image;
    }
}

// WinMain函数：程序的入口点
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    InitGDIPlus(gdiplusStartupInput, gdiplusToken);

 
    // 注册窗口类
    const wchar_t CLASS_NAME[] = L"Sample Window Class";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    // 获取屏幕宽度
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int windowWidth = 800; // 默认窗口宽度
    int windowHeight = 600; // 默认窗口高度

    // 从配置文件读取窗口位置
    // Sleep(1);
    UpdateConfig();
    // Sleep(1);
        int windowX = config.windowX;
    int windowY = config.windowY;

    // 如果配置文件中没有位置，计算窗口初始位置，使得窗口右边靠屏幕右侧 80px
    if (windowX == CW_USEDEFAULT || windowX == 50) {
        windowX = screenWidth - windowWidth - 80;
    }
    if (windowY == CW_USEDEFAULT) {
        windowY = 50;
    }

    // 创建窗口
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST,
        CLASS_NAME,
        L"stock viewer", // 设置窗口标题为 "stock viewer"
        WS_POPUP, // 使用WS_POPUP样式，以隐藏窗口标题栏
        windowX, windowY, windowWidth, windowHeight,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (hwnd == NULL) {
        return 0;
    }

    // 初次下载并显示图片
    DownloadAndDisplayImages(hwnd);

    ShowWindow(hwnd, nCmdShow);

    // 设置定时器，每3秒钟触发一次
    SetTimer(hwnd, TIMER_ID, 3000, NULL);

    // 运行消息循环
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理
    CleanupGDIPlus(gdiplusToken);
    return 0;
}

// 窗口过程函数：处理窗口的消息
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static POINTS lastPos = { 0, 0 };

    switch (uMsg) {
        case WM_TIMER: {
            if (wParam == TIMER_ID) {
                // 定时器触发，更新图片
                DownloadAndDisplayImages(hwnd);
            }
        } break;

        case WM_LBUTTONDOWN: {
            // 点击图片，重新下载并刷新图片
            DownloadAndDisplayImages(hwnd);
            // 使窗口可以拖动
            SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        } break;

        case WM_NCHITTEST: {
            // 使窗口可以拖动
            LRESULT hit = DefWindowProc(hwnd, uMsg, wParam, lParam);
            if (hit == HTCLIENT) hit = HTCAPTION;
            return hit;
        } break;

        case WM_EXITSIZEMOVE: {
            // 拖动窗口后保存位置
            RECT rect;
            GetWindowRect(hwnd, &rect);
            SaveWindowPosition(rect.left, rect.top);
        } break;

        case WM_DESTROY: {
            KillTimer(hwnd, TIMER_ID);
            PostQuitMessage(0);
        } break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}
