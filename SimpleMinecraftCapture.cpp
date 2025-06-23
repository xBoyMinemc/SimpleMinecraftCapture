// SimpleMinecraftCapture.cpp - 简化版，只需要Windows SDK
#include <windows.h>
#include <gdiplus.h>
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
        GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
        
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
        serverThread.join();
        captureThread.join();
    }
    
    ~SimpleMinecraftCapture() {
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
            if (hwnd) break;
        }
        
        // 如果还没找到，枚举所有窗口
        if (!hwnd) {
            EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                char title[256];
                GetWindowTextA(hwnd, title, sizeof(title));
                if (strstr(title, "Minecraft") || strstr(title, "minecraft")) {
                    *((HWND*)lParam) = hwnd;
                    return FALSE; // 停止枚举
                }
                return TRUE;
            }, (LPARAM)&hwnd);
        }
        
        return hwnd;
    }
    
    void CaptureLoop() {
        while (m_running) {
            CaptureFrame();
            Sleep(33); // ~30 FPS
        }
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
        GetClientRect(m_minecraftWindow, &windowRect);
        int width = windowRect.right - windowRect.left;
        int height = windowRect.bottom - windowRect.top;
        
        if (width <= 0 || height <= 0) return;
        
        // 获取窗口DC
        HDC windowDC = GetDC(m_minecraftWindow);
        HDC memDC = CreateCompatibleDC(windowDC);
        HBITMAP memBitmap = CreateCompatibleBitmap(windowDC, width, height);
        HGDIOBJ oldBitmap = SelectObject(memDC, memBitmap);
        
        // 复制窗口内容
        BitBlt(memDC, 0, 0, width, height, windowDC, 0, 0, SRCCOPY);
        
        // 转换为GDI+ Bitmap
        Bitmap bitmap(memBitmap, NULL);
        
        // 保存为JPEG到内存
        IStream* stream = NULL;
        CreateStreamOnHGlobal(NULL, TRUE, &stream);
        
        // 获取JPEG编码器
        CLSID jpegClsid;
        GetEncoderClsid(L"image/jpeg", &jpegClsid);
        
        // 设置JPEG质量
        EncoderParameters encoderParams;
        encoderParams.Count = 1;
        encoderParams.Parameter[0].Guid = EncoderQuality;
        encoderParams.Parameter[0].Type = EncoderParameterValueTypeLong;
        encoderParams.Parameter[0].NumberOfValues = 1;
        ULONG quality = 70; // 70% 质量
        encoderParams.Parameter[0].Value = &quality;
        
        bitmap.Save(stream, &jpegClsid, &encoderParams);
        
        // 获取数据
        HGLOBAL hGlobal;
        GetHGlobalFromStream(stream, &hGlobal);
        DWORD size = GlobalSize(hGlobal);
        BYTE* data = (BYTE*)GlobalLock(hGlobal);
        
        // 保存最新帧
        m_latestFrame.assign(data, data + size);
        
        // 清理
        GlobalUnlock(hGlobal);
        stream->Release();
        SelectObject(memDC, oldBitmap);
        DeleteObject(memBitmap);
        DeleteDC(memDC);
        ReleaseDC(m_minecraftWindow, windowDC);
    }
    
    void StartHTTPServer() {
        // 创建服务器socket
        m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_serverSocket == INVALID_SOCKET) {
            printf("创建socket失败\n");
            return;
        }
        
        // 绑定端口
        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(8080);
        
        if (bind(m_serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            printf("绑定端口失败\n");
            return;
        }
        
        // 监听连接
        listen(m_serverSocket, 5);
        
        while (m_running) {
            sockaddr_in clientAddr;
            int clientAddrSize = sizeof(clientAddr);
            SOCKET clientSocket = accept(m_serverSocket, (sockaddr*)&clientAddr, &clientAddrSize);
            
            if (clientSocket != INVALID_SOCKET) {
                std::thread([this, clientSocket]() {
                    HandleClient(clientSocket);
                }).detach();
            }
        }
    }
    
    void HandleClient(SOCKET clientSocket) {
        char buffer[1024];
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
        std::string html = R"(
HTTP/1.1 200 OK
Content-Type: text/html
Connection: close

<!DOCTYPE html>
<html>
<head>
    <title>Minecraft 实时画面</title>
    <style>
        body { 
            margin: 0; 
            background: #000; 
            display: flex; 
            justify-content: center; 
            align-items: center; 
            height: 100vh;
            font-family: Arial;
        }
        #gameFrame { 
            max-width: 90vw; 
            max-height: 90vh; 
            border: 2px solid #333;
        }
        .info {
            position: absolute;
            top: 10px;
            left: 10px;
            color: white;
        }
    </style>
</head>
<body>
    <div class="info">
        <h3>Minecraft 实时画面</h3>
        <div>刷新率: 每秒约30帧</div>
    </div>
    <img id="gameFrame" src="/image" alt="Minecraft画面">
    
    <script>
        // 每33毫秒刷新图片 (~30 FPS)
        setInterval(() => {
            const img = document.getElementById('gameFrame');
            img.src = '/image?' + new Date().getTime();
        }, 33);
    </script>
</body>
</html>
        )";
        
        send(clientSocket, html.c_str(), html.length(), 0);
    }
    
    void SendImage(SOCKET clientSocket) {
        if (m_latestFrame.empty()) {
            // 发送404
            std::string response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            send(clientSocket, response.c_str(), response.length(), 0);
            return;
        }
        
        // 发送HTTP头
        std::string header = "HTTP/1.1 200 OK\r\n";
        header += "Content-Type: image/jpeg\r\n";
        header += "Content-Length: " + std::to_string(m_latestFrame.size()) + "\r\n";
        header += "Connection: close\r\n\r\n";
        
        send(clientSocket, header.c_str(), header.length(), 0);
        send(clientSocket, (const char*)m_latestFrame.data(), m_latestFrame.size(), 0);
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
    printf("Minecraft基岩版画面捕获程序\n");
    printf("=================================\n");
    printf("请确保Minecraft基岩版正在运行\n\n");
    
    SimpleMinecraftCapture capture;
    
    if (!capture.Initialize()) {
        printf("初始化失败！\n");
        printf("请检查：\n");
        printf("1. Minecraft基岩版是否正在运行\n");
        printf("2. 程序是否有足够权限\n");
        return -1;
    }
    
    capture.StartCapture();
    return 0;
}

// 编译命令 (在Visual Studio Developer Command Prompt中运行):
// cl /EHsc SimpleMinecraftCapture.cpp gdiplus.lib ws2_32.lib
