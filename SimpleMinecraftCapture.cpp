// SimpleMinecraftCapture.cpp - 修复版本，解决头文件冲突
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <gdiplus.h>

// 在包含任何winsock头文件之前定义这个
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <thread>
#include <vector>
#include <string>
#include <atomic>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ws2_32.lib")

using namespace Gdiplus;

class SimpleMinecraftCapture {
private:
    HWND m_minecraftWindow;
    SOCKET m_serverSocket;
    std::atomic<bool> m_running{false};
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;

public:
    bool Initialize() {
        // 初始化GDI+
        if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
            printf("GDI+ 初始化失败\n");
            return false;
        }
        
        // 初始化Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
            printf("WSAStartup 失败\n");
            return false;
        }
        
        // 查找Minecraft窗口
        m_minecraftWindow = FindMinecraftWindow();
        if (!m_minecraftWindow) {
            printf("未找到Minecraft窗口！\n");
            printf("请确保Minecraft基岩版正在运行\n");
            printf("支持的窗口标题:\n");
            printf("- Minecraft\n");
            printf("- Minecraft Bedrock\n");
            printf("- Minecraft for Windows 10\n");
            printf("- Minecraft: Bedrock Edition\n");
            return false;
        }
        
        char windowTitle[256];
        GetWindowTextA(m_minecraftWindow, windowTitle, sizeof(windowTitle));
        printf("找到窗口: %s\n", windowTitle);
        
        return true;
    }
    
    void StartCapture() {
        m_running = true;
        
        // 启动HTTP服务器线程
        std::thread serverThread([this]() {
            StartHTTPServer();
        });
        
        // 启动捕获线程
        std::thread captureThread([this]() {
            CaptureLoop();
        });
        
        printf("服务器启动完成！\n");
        printf("在浏览器中打开: http://localhost:8080\n");
        printf("按任意键停止...\n");
        
        getchar();
        
        m_running = false;
        
        // 关闭socket以退出服务器线程
        if (m_serverSocket != INVALID_SOCKET) {
            closesocket(m_serverSocket);
        }
        
        if (serverThread.joinable()) {
            serverThread.join();
        }
        if (captureThread.joinable()) {
            captureThread.join();
        }
    }
    
    ~SimpleMinecraftCapture() {
        m_running = false;
        if (m_serverSocket != INVALID_SOCKET) {
            closesocket(m_serverSocket);
        }
        WSACleanup();
        GdiplusShutdown(gdiplusToken);
    }

private:
    HWND FindMinecraftWindow() {
        HWND hwnd = NULL;
        
        // 常见的Minecraft窗口标题
        const char* titles[] = {
            "Minecraft",
            "Minecraft Bedrock",
            "Minecraft for Windows 10",
            "Minecraft: Bedrock Edition"
        };
        
        for (const char* title : titles) {
            hwnd = FindWindowA(NULL, title);
            if (hwnd) {
                printf("通过标题找到窗口: %s\n", title);
                break;
            }
        }
        
        // 如果还没找到，枚举所有窗口
        if (!hwnd) {
            printf("尝试枚举所有窗口...\n");
            EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                char title[256];
                GetWindowTextA(hwnd, title, sizeof(title));
                
                // 检查是否包含minecraft关键词
                if (strlen(title) > 0) {
                    std::string titleStr(title);
                    std::transform(titleStr.begin(), titleStr.end(), titleStr.begin(), ::tolower);
                    
                    if (titleStr.find("minecraft") != std::string::npos) {
                        printf("找到可能的Minecraft窗口: %s\n", title);
                        *((HWND*)lParam) = hwnd;
                        return FALSE; // 停止枚举
                    }
                }
                return TRUE;
            }, (LPARAM)&hwnd);
        }
        
        return hwnd;
    }
    
    void CaptureLoop() {
        printf("开始捕获循环...\n");
        while (m_running) {
            try {
                CaptureFrame();
                Sleep(33); // ~30 FPS
            } catch (...) {
                printf("捕获帧时出错\n");
                Sleep(100);
            }
        }
        printf("捕获循环结束\n");
    }
    
    std::vector<BYTE> m_latestFrame;
    
    void CaptureFrame() {
        if (!IsWindow(m_minecraftWindow)) {
            printf("Minecraft窗口已关闭\n");
            m_running = false;
            return;
        }
        
        // 获取窗口尺寸
        RECT windowRect;
        if (!GetClientRect(m_minecraftWindow, &windowRect)) {
            return;
        }
        
        int width = windowRect.right - windowRect.left;
        int height = windowRect.bottom - windowRect.top;
        
        if (width <= 0 || height <= 0) {
            return;
        }
        
        // 获取窗口DC
        HDC windowDC = GetDC(m_minecraftWindow);
        if (!windowDC) {
            return;
        }
        
        HDC memDC = CreateCompatibleDC(windowDC);
        if (!memDC) {
            ReleaseDC(m_minecraftWindow, windowDC);
            return;
        }
        
        HBITMAP memBitmap = CreateCompatibleBitmap(windowDC, width, height);
        if (!memBitmap) {
            DeleteDC(memDC);
            ReleaseDC(m_minecraftWindow, windowDC);
            return;
        }
        
        HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);
        
        // 复制窗口内容
        if (BitBlt(memDC, 0, 0, width, height, windowDC, 0, 0, SRCCOPY)) {
            // 转换为GDI+ Bitmap
            Bitmap bitmap(memBitmap, NULL);
            
            if (bitmap.GetLastStatus() == Ok) {
                // 保存为JPEG到内存
                IStream* stream = NULL;
                if (SUCCEEDED(CreateStreamOnHGlobal(NULL, TRUE, &stream))) {
                    // 获取JPEG编码器
                    CLSID jpegClsid;
                    if (GetEncoderClsid(L"image/jpeg", &jpegClsid) >= 0) {
                        // 设置JPEG质量
                        EncoderParameters encoderParams;
                        encoderParams.Count = 1;
                        encoderParams.Parameter[0].Guid = EncoderQuality;
                        encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
                        encoderParams.Parameter[0].NumberOfValues = 1;
                        ULONG quality = 70; // 70% 质量
                        encoderParams.Parameter[0].Value = &quality;
                        
                        if (bitmap.Save(stream, &jpegClsid, &encoderParams) == Ok) {
                            // 获取数据
                            HGLOBAL hGlobal;
                            if (SUCCEEDED(GetHGlobalFromStream(stream, &hGlobal))) {
                                DWORD size = GlobalSize(hGlobal);
                                BYTE* data = (BYTE*)GlobalLock(hGlobal);
                                
                                if (data && size > 0) {
                                    // 保存最新帧
                                    m_latestFrame.assign(data, data + size);
                                }
                                
                                if (data) {
                                    GlobalUnlock(hGlobal);
                                }
                            }
                        }
                    }
                    stream->Release();
                }
            }
        }
        
        // 清理
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        ReleaseDC(m_minecraftWindow, windowDC);
    }
    
    void StartHTTPServer() {
        printf("启动HTTP服务器...\n");
        
        // 创建服务器socket
        m_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_serverSocket == INVALID_SOCKET) {
            printf("创建socket失败: %d\n", WSAGetLastError());
            return;
        }
        
        // 设置socket选项
        int optval = 1;
        setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&optval, sizeof(optval));
        
        // 绑定端口
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(8080);
        
        if (bind(m_serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            printf("绑定端口失败: %d\n", WSAGetLastError());
            printf("端口8080可能被占用，请关闭其他程序或修改源码中的端口号\n");
            return;
        }
        
        // 监听连接
        if (listen(m_serverSocket, 5) == SOCKET_ERROR) {
            printf("监听失败: %d\n", WSAGetLastError());
            return;
        }
        
        printf("HTTP服务器在端口8080监听中...\n");
        
        while (m_running) {
            sockaddr_in clientAddr;
            int clientAddrSize = sizeof(clientAddr);
            SOCKET clientSocket = accept(m_serverSocket, (sockaddr*)&clientAddr, &clientAddrSize);
            
            if (clientSocket != INVALID_SOCKET) {
                std::thread([this, clientSocket]() {
                    HandleClient(clientSocket);
                }).detach();
            } else if (m_running) {
                printf("Accept失败: %d\n", WSAGetLastError());
                Sleep(100);
            }
        }
        
        printf("HTTP服务器已停止\n");
    }
    
    void HandleClient(SOCKET clientSocket) {
        char buffer[2048];
        int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';
            
            // 解析HTTP请求
            std::string request(buffer);
            if (request.find("GET /image") != std::string::npos) {
                // 发送图片
                SendImage(clientSocket);
            } else {
                // 发送HTML页面
                SendHTML(clientSocket);
            }
        }
        
        closesocket(clientSocket);
    }
    
    void SendHTML(SOCKET clientSocket) {
        std::string html = R"(HTTP/1.1 200 OK
Content-Type: text/html; charset=utf-8
Connection: close

<!DOCTYPE html>
<html>
<head>
    <title>Minecraft 实时画面</title>
    <meta charset="utf-8">
    <style>
        body { 
            margin: 0; 
            background: #1a1a1a; 
            display: flex; 
            justify-content: center; 
            align-items: center; 
            height: 100vh;
            font-family: 'Segoe UI', Arial, sans-serif;
            overflow: hidden;
        }
        #gameFrame { 
            max-width: 95vw; 
            max-height: 95vh; 
            border: 3px solid #4CAF50;
            border-radius: 8px;
            box-shadow: 0 0 20px rgba(76, 175, 80, 0.3);
        }
        .info {
            position: absolute;
            top: 20px;
            left: 20px;
            color: #4CAF50;
            background: rgba(0,0,0,0.8);
            padding: 15px;
            border-radius: 8px;
            border: 1px solid #4CAF50;
        }
        .status {
            color: #81C784;
            margin-top: 5px;
        }
        .loading {
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            color: #4CAF50;
            font-size: 18px;
        }
    </style>
</head>
<body>
    <div class="info">
        <h3 style="margin:0 0 10px 0;">🎮 Minecraft 实时画面</h3>
        <div class="status">状态: <span id="status">连接中...</span></div>
        <div class="status">刷新率: 约30帧/秒</div>
    </div>
    
    <div class="loading" id="loading">加载中...</div>
    <img id="gameFrame" src="/image" alt="Minecraft画面" style="display:none;">
    
    <script>
        const img = document.getElementById('gameFrame');
        const loading = document.getElementById('loading');
        const status = document.getElementById('status');
        let isLoaded = false;
        
        function updateImage() {
            const newImg = new Image();
            newImg.onload = function() {
                img.src = newImg.src;
                if (!isLoaded) {
                    loading.style.display = 'none';
                    img.style.display = 'block';
                    status.textContent = '正常运行';
                    isLoaded = true;
                }
            };
            newImg.onerror = function() {
                status.textContent = '连接错误';
            };
            newImg.src = '/image?' + new Date().getTime();
        }
        
        // 每33毫秒刷新图片 (~30 FPS)
        updateImage();
        setInterval(updateImage, 33);
        
        // 错误处理
        img.onerror = function() {
            status.textContent = '图像加载失败';
        };
    </script>
</body>
</html>)";
        
        send(clientSocket, html.c_str(), (int)html.length(), 0);
    }
    
    void SendImage(SOCKET clientSocket) {
        if (m_latestFrame.empty()) {
            // 发送404
            std::string response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(clientSocket, response.c_str(), (int)response.length(), 0);
            return;
        }
        
        // 发送HTTP头
        std::string header = "HTTP/1.1 200 OK\r\n";
        header += "Content-Type: image/jpeg\r\n";
        header += "Content-Length: " + std::to_string(m_latestFrame.size()) + "\r\n";
        header += "Cache-Control: no-cache\r\n";
        header += "Connection: close\r\n\r\n";
        
        send(clientSocket, header.c_str(), (int)header.length(), 0);
        send(clientSocket, (const char*)m_latestFrame.data(), (int)m_latestFrame.size(), 0);
    }
    
    // 获取JPEG编码器CLSID
    int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
        UINT num = 0;
        UINT size = 0;
        
        ImageCodecInfo* pImageCodecInfo = NULL;
        GetImageEncodersSize(&num, &size);
        if (size == 0) return -1;
        
        pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
        if (pImageCodecInfo == NULL) return -1;
        
        GetImageEncoders(num, size, pImageCodecInfo);
        
        for (UINT j = 0; j < num; ++j) {
            if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
                *pClsid = pImageCodecInfo[j].Clsid;
                free(pImageCodecInfo);
                return j;
            }
        }
        
        free(pImageCodecInfo);
        return -1;
    }
};

int main() {
    printf("=================================\n");
    printf("Minecraft基岩版画面捕获程序 v1.0\n");
    printf("=================================\n");
    printf("请确保Minecraft基岩版正在运行\n\n");
    
    SimpleMinecraftCapture capture;
    
    if (!capture.Initialize()) {
        printf("\n初始化失败！\n");
        printf("请检查：\n");
        printf("1. Minecraft基岩版是否正在运行\n");
        printf("2. 窗口是否可见（未最小化）\n");
        printf("3. 程序是否有足够权限\n");
        printf("\n按任意键退出...\n");
        getchar();
        return -1;
    }
    
    capture.StartCapture();
    return 0;
}
