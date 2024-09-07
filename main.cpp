#ifndef UNICODE
#define UNICODE
#endif 

#include <windows.h>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <directxmath.h>
#include <wrl/client.h>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;
using Microsoft::WRL::ComPtr;

const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;
const int MAX_PARTICLES = 5000000;

struct Particle {
    XMFLOAT2 position;
    XMFLOAT2 velocity;
    XMFLOAT4 color;
};

class ParticleSystem {
private:
    std::vector<Particle> particles;
    std::mt19937 rng;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID3D11RenderTargetView> renderTargetView;
    ComPtr<ID3D11Buffer> vertexBuffer;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11InputLayout> inputLayout;

    float highestFPS = 0.0f;

public:
    ParticleSystem(HWND hwnd) : rng(std::random_device{}()) {
        initializeDirectX(hwnd);
        createShaders();
        createVertexBuffer();
    }

    void update(float dt) {
        for (auto& p : particles) {
            p.position.x += p.velocity.x * dt;
            p.position.y += p.velocity.y * dt;

            if (p.position.x < 0 || p.position.x > WINDOW_WIDTH) p.velocity.x *= -1;
            if (p.position.y < 0 || p.position.y > WINDOW_HEIGHT) p.velocity.y *= -1;
        }
    }

    void addParticles(int count) {
        std::uniform_real_distribution<float> posDist(0, WINDOW_WIDTH);
        std::uniform_real_distribution<float> velDist(-100, 100);
        std::uniform_real_distribution<float> colorDist(0, 1);

        for (int i = 0; i < count && particles.size() < MAX_PARTICLES; ++i) {
            Particle p;
            p.position = XMFLOAT2(posDist(rng), posDist(rng));
            p.velocity = XMFLOAT2(velDist(rng), velDist(rng));
            p.color = XMFLOAT4(colorDist(rng), colorDist(rng), colorDist(rng), 1.0f);
            particles.push_back(p);
        }
    }

    void draw() {
        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        context->ClearRenderTargetView(renderTargetView.Get(), clearColor);

        if (!particles.empty()) {
            D3D11_MAPPED_SUBRESOURCE mappedResource;
            context->Map(vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
            memcpy(mappedResource.pData, particles.data(), sizeof(Particle) * particles.size());
            context->Unmap(vertexBuffer.Get(), 0);

            UINT stride = sizeof(Particle);
            UINT offset = 0;
            context->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
            context->IASetInputLayout(inputLayout.Get());

            context->VSSetShader(vertexShader.Get(), nullptr, 0);
            context->PSSetShader(pixelShader.Get(), nullptr, 0);

            context->Draw(static_cast<UINT>(particles.size()), 0);
        }

        swapChain->Present(1, 0);
    }

    size_t getCount() const { return particles.size(); }

    float trackFPS(float fps) {
        if (particles.size() >= MAX_PARTICLES) {
            if (fps > highestFPS) {
                highestFPS = fps;
            }
        }
        return highestFPS;
    }

private:
    void initializeDirectX(HWND hwnd) {
        DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
        swapChainDesc.BufferCount = 1;
        swapChainDesc.BufferDesc.Width = WINDOW_WIDTH;
        swapChainDesc.BufferDesc.Height = WINDOW_HEIGHT;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.OutputWindow = hwnd;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.Windowed = TRUE;

        D3D_FEATURE_LEVEL featureLevel;
        D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &swapChainDesc, &swapChain, &device, &featureLevel, &context);

        ComPtr<ID3D11Texture2D> backBuffer;
        swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), &backBuffer);
        device->CreateRenderTargetView(backBuffer.Get(), nullptr, &renderTargetView);
        context->OMSetRenderTargets(1, renderTargetView.GetAddressOf(), nullptr);

        D3D11_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(WINDOW_WIDTH), static_cast<float>(WINDOW_HEIGHT), 0.0f, 1.0f };
        context->RSSetViewports(1, &viewport);
    }

    void createShaders() {
        const char* vertexShaderSource = R"(
            struct Particle {
                float2 position : POSITION;
                float2 velocity : VELOCITY;
                float4 color : COLOR;
            };
            struct PixelInput {
                float4 position : SV_POSITION;
                float4 color : COLOR;
            };
            PixelInput main(Particle input) {
                PixelInput output;
                output.position = float4(input.position.x / 400 - 1, -input.position.y / 300 + 1, 0, 1);
                output.color = input.color;
                return output;
            }
        )";

        const char* pixelShaderSource = R"(
            struct PixelInput {
                float4 position : SV_POSITION;
                float4 color : COLOR;
            };
            float4 main(PixelInput input) : SV_TARGET {
                return input.color;
            }
        )";

        ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

        D3DCompile(vertexShaderSource, strlen(vertexShaderSource), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vsBlob, &errorBlob);
        D3DCompile(pixelShaderSource, strlen(pixelShaderSource), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &psBlob, &errorBlob);

        device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vertexShader);
        device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &pixelShader);

        D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "VELOCITY", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        device->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout);
    }

    void createVertexBuffer() {
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.ByteWidth = sizeof(Particle) * MAX_PARTICLES;
        bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        device->CreateBuffer(&bufferDesc, nullptr, &vertexBuffer);
    }
};

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"Particle Simulator Window Class";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, L"Particle Simulator", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL) return 0;

    ShowWindow(hwnd, nCmdShow);

    ParticleSystem particleSystem(hwnd);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&particleSystem));

    auto lastTime = std::chrono::high_resolution_clock::now();
    auto lastParticleAddTime = lastTime;

    MSG msg = {};
    while (true) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            auto currentTime = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            if (std::chrono::duration<float>(currentTime - lastParticleAddTime).count() > 0.1f) {
                particleSystem.addParticles(100000);
                lastParticleAddTime = currentTime;
            }

            particleSystem.update(dt);
            particleSystem.draw();

            wchar_t title[100];
            float fps = 1.0f / dt;

            swprintf_s(title, L"Particles: %zu, FPS: %.2f, Max FPS: %.2f", particleSystem.getCount(), fps, particleSystem.trackFPS(fps));
            SetWindowText(hwnd, title);
        }
    }

    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
