#pragma once
#include "core/Geometry.h"
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <array>
#include <string>

namespace evolve {
struct OrbitCamera {
    float yaw{0.45f}, pitch{0.10f}, distance{24.0f};
    Vec3 target{};
    void frame(std::size_t bases, bool compare, float aspect);
    void orbit(float dx, float dy);
    void pan(float dx, float dy, int height);
    void zoom(float wheelSteps);
    Vec3 eye() const;
};

class Dx12Renderer {
public:
    ~Dx12Renderer();
    void initialize(HWND window, bool forceWarp);
    void resize(int width, int height);
    void setMesh(const SceneMesh& mesh);
    // Optional BMP capture performs readback before presentation for CI smoke testing.
    void render(const OrbitCamera& camera, float lighting, const std::wstring& capture = {});
    int pick(int x, int y, const OrbitCamera& camera) const;
    float aspect() const { return static_cast<float>(width_)/static_cast<float>(height_); }
    const std::wstring& adapterName() const { return adapterName_; }
    unsigned captureNonBackgroundPixels() const { return capturePixels_; }
private:
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    void waitForGpu();
    void createTargets();
    DirectX::XMMATRIX view(const OrbitCamera& camera) const;
    DirectX::XMMATRIX projection() const;
    Ptr<IDXGIFactory4> factory_;
    Ptr<ID3D12Device> device_;
    Ptr<ID3D12CommandQueue> queue_;
    Ptr<IDXGISwapChain3> swapChain_;
    Ptr<ID3D12DescriptorHeap> rtvHeap_,dsvHeap_;
    std::array<Ptr<ID3D12Resource>,2> targets_;
    Ptr<ID3D12Resource> depth_,vertices_;
    Ptr<ID3D12CommandAllocator> allocator_;
    Ptr<ID3D12GraphicsCommandList> commands_;
    Ptr<ID3D12RootSignature> root_;
    Ptr<ID3D12PipelineState> pipeline_;
    Ptr<ID3D12Fence> fence_;
    HANDLE fenceEvent_{};
    UINT64 fenceValue_{};
    UINT descriptorSize_{},vertexCount_{};
    D3D12_VERTEX_BUFFER_VIEW vertexView_{};
    int width_{1},height_{1};
    unsigned capturePixels_{};
    std::vector<PickPoint> pickPoints_;
    std::wstring adapterName_;
};
}
