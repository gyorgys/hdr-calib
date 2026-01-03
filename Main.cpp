#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <xinput.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

// Global variables
ComPtr<ID3D11Device> g_d3dDevice;
ComPtr<ID3D11DeviceContext> g_d3dContext;
ComPtr<IDXGISwapChain3> g_swapChain;
ComPtr<ID2D1Factory1> g_d2dFactory;
ComPtr<ID2D1Device> g_d2dDevice;
ComPtr<ID2D1DeviceContext> g_d2dContext;
ComPtr<ID2D1Bitmap1> g_d2dTargetBitmap;
ComPtr<ID2D1SolidColorBrush> g_whiteBrush;
ComPtr<ID2D1SolidColorBrush> g_innerBrush;
ComPtr<ID2D1SolidColorBrush> g_textBrush;
ComPtr<IDWriteFactory> g_dwriteFactory;
ComPtr<IDWriteTextFormat> g_textFormat;

HWND g_hwnd = nullptr;
float g_innerBrightness = 800.0f; // nits
int g_screenWidth = 0;
int g_screenHeight = 0;

// Forward declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool InitD3D();
bool InitD2D();
void ProcessInput();
void UpdateInnerBrightness(float newBrightness);
void Render();
void CleanUp();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    // Get screen dimensions
    g_screenWidth = GetSystemMetrics(SM_CXSCREEN);
    g_screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"HDRCalibClass";
    RegisterClassExW(&wc);

    // Create fullscreen window
    g_hwnd = CreateWindowExW(
        0,
        L"HDRCalibClass",
        L"HDR Calibration",
        WS_POPUP,
        0, 0,
        g_screenWidth, g_screenHeight,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!g_hwnd)
        return -1;

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    // Initialize DirectX 11 and Direct2D
    if (!InitD3D() || !InitD2D())
    {
        CleanUp();
        return -1;
    }

    // Main message loop
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            ProcessInput();
            Render();
        }
    }

    CleanUp();
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
            PostQuitMessage(0);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

void ProcessInput()
{
    static bool leftWasPressed = false;
    static bool rightWasPressed = false;
    static bool bWasPressed = false;

    // Check keyboard input
    bool leftPressed = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
    bool rightPressed = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;

    // Check gamepad input
    XINPUT_STATE state = {};
    if (XInputGetState(0, &state) == ERROR_SUCCESS)
    {
        // D-Pad
        leftPressed = leftPressed || (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
        rightPressed = rightPressed || (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);

        // Left stick
        const SHORT STICK_THRESHOLD = 16000;
        if (state.Gamepad.sThumbLX < -STICK_THRESHOLD)
            leftPressed = true;
        if (state.Gamepad.sThumbLX > STICK_THRESHOLD)
            rightPressed = true;

        // B button to quit
        bool bPressed = (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
        if (bPressed && !bWasPressed)
            PostQuitMessage(0);
        bWasPressed = bPressed;
    }

    // Handle brightness adjustment with edge detection
    if (leftPressed && !leftWasPressed)
        UpdateInnerBrightness(g_innerBrightness - 50.0f);
    if (rightPressed && !rightWasPressed)
        UpdateInnerBrightness(g_innerBrightness + 50.0f);

    leftWasPressed = leftPressed;
    rightWasPressed = rightPressed;
}

void UpdateInnerBrightness(float newBrightness)
{
    // Clamp to 0-10000 range
    g_innerBrightness = max(0.0f, min(10000.0f, newBrightness));

    // Update the brush color
    float innerScRGB = g_innerBrightness / 80.0f;
    g_innerBrush->SetColor(D2D1::ColorF(innerScRGB, innerScRGB, innerScRGB, 1.0f));
}

bool InitD3D()
{
    HRESULT hr;

    // Create D3D11 device and context
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &device,
        nullptr,
        &context
    );

    if (FAILED(hr))
        return false;

    device.As(&g_d3dDevice);
    context.As(&g_d3dContext);

    // Create swap chain
    ComPtr<IDXGIDevice1> dxgiDevice;
    g_d3dDevice.As(&dxgiDevice);

    ComPtr<IDXGIAdapter> dxgiAdapter;
    dxgiDevice->GetAdapter(&dxgiAdapter);

    ComPtr<IDXGIFactory2> dxgiFactory;
    dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = g_screenWidth;
    swapChainDesc.Height = g_screenHeight;
    swapChainDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapChainDesc.Flags = 0;

    ComPtr<IDXGISwapChain1> swapChain1;
    hr = dxgiFactory->CreateSwapChainForHwnd(
        g_d3dDevice.Get(),
        g_hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain1
    );

    if (FAILED(hr))
        return false;

    // Get IDXGISwapChain3 for color space setting
    hr = swapChain1.As(&g_swapChain);
    if (FAILED(hr))
        return false;

    // Set scRGB color space
    hr = g_swapChain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);

    return SUCCEEDED(hr);
}

bool InitD2D()
{
    HRESULT hr;

    // Create D2D factory
    D2D1_FACTORY_OPTIONS options = {};
#ifdef _DEBUG
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

    hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        options,
        g_d2dFactory.GetAddressOf()
    );

    if (FAILED(hr))
        return false;

    // Create D2D device
    ComPtr<IDXGIDevice> dxgiDevice;
    g_d3dDevice.As(&dxgiDevice);

    hr = g_d2dFactory->CreateDevice(dxgiDevice.Get(), &g_d2dDevice);
    if (FAILED(hr))
        return false;

    // Create D2D device context
    hr = g_d2dDevice->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
        &g_d2dContext
    );

    if (FAILED(hr))
        return false;

    // Get back buffer and create D2D bitmap
    ComPtr<IDXGISurface> dxgiBackBuffer;
    hr = g_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiBackBuffer));
    if (FAILED(hr))
        return false;

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = {};
    bitmapProperties.pixelFormat.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    bitmapProperties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmapProperties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;

    hr = g_d2dContext->CreateBitmapFromDxgiSurface(
        dxgiBackBuffer.Get(),
        &bitmapProperties,
        &g_d2dTargetBitmap
    );

    if (FAILED(hr))
        return false;

    g_d2dContext->SetTarget(g_d2dTargetBitmap.Get());

    // Create white brush at 10000 nits (10000/80 = 125.0 in scRGB)
    hr = g_d2dContext->CreateSolidColorBrush(
        D2D1::ColorF(125.0f, 125.0f, 125.0f, 1.0f),
        &g_whiteBrush
    );

    if (FAILED(hr))
        return false;

    // Create inner brush with variable brightness
    float innerScRGB = g_innerBrightness / 80.0f;
    hr = g_d2dContext->CreateSolidColorBrush(
        D2D1::ColorF(innerScRGB, innerScRGB, innerScRGB, 1.0f),
        &g_innerBrush
    );

    if (FAILED(hr))
        return false;

    // Create dark blue brush for text
    hr = g_d2dContext->CreateSolidColorBrush(
        D2D1::ColorF(0.0f, 0.0f, 0.5f, 1.0f),
        &g_textBrush
    );

    if (FAILED(hr))
        return false;

    // Create DirectWrite factory
    hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_dwriteFactory.GetAddressOf())
    );

    if (FAILED(hr))
        return false;

    // Create text format
    hr = g_dwriteFactory->CreateTextFormat(
        L"Arial",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        24.0f,
        L"en-us",
        &g_textFormat
    );

    if (FAILED(hr))
        return false;

    g_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    g_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

    return SUCCEEDED(hr);
}

void Render()
{
    g_d2dContext->BeginDraw();

    // Clear to black
    g_d2dContext->Clear(D2D1::ColorF(D2D1::ColorF::Black));

    // Draw white rectangle in the center
    float rectWidth = g_screenHeight / 6.0f;
    float rectHeight = rectWidth;
    float x = (g_screenWidth - rectWidth) / 2.0f;
    float y = (g_screenHeight - rectHeight) / 2.0f;

    D2D1_RECT_F rect = D2D1::RectF(x, y, x + rectWidth, y + rectHeight);
    g_d2dContext->FillRectangle(&rect, g_whiteBrush.Get());

    // Draw inner rectangle (1/2 size) centered in the outer rectangle
    float innerWidth = rectWidth / 2.0f;
    float innerHeight = rectHeight / 2.0f;
    float innerX = x + (rectWidth - innerWidth) / 2.0f;
    float innerY = y + (rectHeight - innerHeight) / 2.0f;

    D2D1_RECT_F innerRect = D2D1::RectF(innerX, innerY, innerX + innerWidth, innerY + innerHeight);
    g_d2dContext->FillRectangle(&innerRect, g_innerBrush.Get());

    // Draw brightness text below large rectangle (same gap as to inner rectangle)
    float gap = (rectWidth - innerWidth) / 2.0f;
    std::wstring text = std::to_wstring(static_cast<int>(g_innerBrightness)) + L" nits";
    D2D1_RECT_F textRect = D2D1::RectF(
        x,
        y + rectHeight + gap,
        x + rectWidth,
        y + rectHeight + gap + 40.0f
    );
    g_d2dContext->DrawText(
        text.c_str(),
        static_cast<UINT32>(text.length()),
        g_textFormat.Get(),
        &textRect,
        g_textBrush.Get()
    );

    g_d2dContext->EndDraw();

    // Present
    g_swapChain->Present(1, 0);
}

void CleanUp()
{
    g_textFormat.Reset();
    g_dwriteFactory.Reset();
    g_textBrush.Reset();
    g_innerBrush.Reset();
    g_whiteBrush.Reset();
    g_d2dTargetBitmap.Reset();
    g_d2dContext.Reset();
    g_d2dDevice.Reset();
    g_d2dFactory.Reset();
    g_swapChain.Reset();
    g_d3dContext.Reset();
    g_d3dDevice.Reset();
}
