#include "renderer/Dx12Renderer.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace evolve {
using namespace DirectX;
namespace {
void check(HRESULT hr,const char* operation) {
    if(FAILED(hr)) { std::ostringstream s;s<<operation<<" failed (0x"<<std::hex<<static_cast<unsigned long>(hr)<<").";throw std::runtime_error(s.str()); }
}
D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES h{};h.Type=type;h.CreationNodeMask=1;h.VisibleNodeMask=1;return h;
}
D3D12_RESOURCE_DESC buffer(UINT64 size) {
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;
    d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;return d;
}
D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource=resource;b.Transition.StateBefore=from;b.Transition.StateAfter=to;
    b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;return b;
}
constexpr const char* Shader=R"(
cbuffer Scene : register(b0) { float4x4 viewProjection; float4 eye; float4 settings; };
struct VSInput { float3 position:POSITION; float3 normal:NORMAL; float3 color:COLOR; };
struct PSInput { float4 position:SV_POSITION; float3 world:POSITION; float3 normal:NORMAL; float3 color:COLOR; };
PSInput VSMain(VSInput v) {
    PSInput o;o.position=mul(float4(v.position,1),viewProjection);o.world=v.position;o.normal=v.normal;o.color=v.color;return o;
}
float4 PSMain(PSInput p):SV_TARGET {
    float3 n=normalize(p.normal),v=normalize(eye.xyz-p.world);
    float3 key=normalize(float3(-0.6,0.9,-0.8));
    float diffuse=saturate(dot(n,key));
    float fill=saturate(dot(n,normalize(float3(0.8,0.2,0.5))));
    float spec=pow(saturate(dot(n,normalize(key+v))),48);
    float rim=pow(1-saturate(dot(n,v)),3);
    float3 color=p.color*(0.25+settings.x*(0.7*diffuse+0.3*fill));
    color+=settings.x*(0.25*spec+float3(0.04,0.08,0.12)*rim);
    return float4(saturate(color),1);
}
)";
}

void OrbitCamera::frame(std::size_t bases,bool compare,float aspect) {
    target={};yaw=0.45f;pitch=0.10f;
    float height=static_cast<float>(bases)*0.62f+2.5f;
    distance=std::max(height,(compare ? 12.0f:6.0f)/std::max(0.2f,aspect))*1.45f;
}
void OrbitCamera::orbit(float dx,float dy) {yaw+=dx*0.007f;pitch=std::clamp(pitch+dy*0.007f,-1.45f,1.45f);}
void OrbitCamera::pan(float dx,float dy,int height) {
    Vec3 forward=normalized(target-eye());
    Vec3 right=normalized(cross({0,1,0},forward)),up=cross(forward,right);
    float scale=distance*0.8f/static_cast<float>(std::max(height,1));
    target=target+right*(-dx*scale)+up*(dy*scale);
}
void OrbitCamera::zoom(float wheelSteps) {distance=std::clamp(distance*std::exp(-wheelSteps*0.12f),2.0f,800.0f);}
Vec3 OrbitCamera::eye() const {return target+Vec3{std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)}*distance;}

Dx12Renderer::~Dx12Renderer() {
    try {if(queue_ && fence_ && fenceEvent_) waitForGpu();} catch(...) {}
    if(fenceEvent_) CloseHandle(fenceEvent_);
}
void Dx12Renderer::initialize(HWND window,bool forceWarp) {
#if defined(_DEBUG)
    Ptr<ID3D12Debug> debug;
    if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
#endif
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory_)),"Create DXGI factory");
    if(!forceWarp) {
        for(UINT i=0;;++i) {
            Ptr<IDXGIAdapter1> adapter;
            HRESULT hr=factory_->EnumAdapters1(i,&adapter);
            if(hr==DXGI_ERROR_NOT_FOUND) break;
            check(hr,"Enumerate adapter");
            DXGI_ADAPTER_DESC1 desc{};check(adapter->GetDesc1(&desc),"Read adapter");
            if(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
            if(SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device_)))) {
                adapterName_=desc.Description;break;
            }
        }
    }
    if(!device_) {
        Ptr<IDXGIAdapter> warp;check(factory_->EnumWarpAdapter(IID_PPV_ARGS(&warp)),"Create WARP adapter");
        check(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device_)),"Create DirectX 12 device");
        adapterName_=L"WARP software rendering";
    }
    RECT client{};GetClientRect(window,&client);width_=std::max(1L,client.right);height_=std::max(1L,client.bottom);
    D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    check(device_->CreateCommandQueue(&q,IID_PPV_ARGS(&queue_)),"Create command queue");
    DXGI_SWAP_CHAIN_DESC1 desc{};desc.Width=static_cast<UINT>(width_);desc.Height=static_cast<UINT>(height_);
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=2;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    Ptr<IDXGISwapChain1> swap;
    check(factory_->CreateSwapChainForHwnd(queue_.Get(),window,&desc,nullptr,nullptr,&swap),"Create swap chain");
    check(swap.As(&swapChain_),"Get swap chain interface");
    check(factory_->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER),"Configure window association");
    D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.NumDescriptors=2;
    check(device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&rtvHeap_)),"Create render target heap");
    descriptorSize_=device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=1;
    check(device_->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsvHeap_)),"Create depth heap");
    createTargets();
    check(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator_)),"Create command allocator");
    D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameter.Constants.Num32BitValues=24;parameter.Constants.ShaderRegister=0;parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};rootDesc.NumParameters=1;rootDesc.pParameters=&parameter;
    rootDesc.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Ptr<ID3DBlob> signature,error;
    check(D3D12SerializeRootSignature(&rootDesc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error),"Serialize root signature");
    check(device_->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root_)),"Create root signature");
    Ptr<ID3DBlob> vs,ps;
    auto compile=[&](const char* entry,const char* target,Ptr<ID3DBlob>& output) {
        HRESULT hr=D3DCompile(Shader,std::strlen(Shader),"EvolveLighting",nullptr,nullptr,entry,target,
            D3DCOMPILE_ENABLE_STRICTNESS,0,&output,&error);
        if(FAILED(hr) && error) throw std::runtime_error(std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()));
        check(hr,"Compile lighting shader");
    };
    compile("VSMain","vs_5_0",vs);compile("PSMain","ps_5_0",ps);
    D3D12_INPUT_ELEMENT_DESC layout[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};pipeline.pRootSignature=root_.Get();
    pipeline.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pipeline.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    pipeline.InputLayout={layout,3};pipeline.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pipeline.RasterizerState.DepthClipEnable=TRUE;
    auto& blend=pipeline.BlendState.RenderTarget[0];blend.SrcBlend=D3D12_BLEND_ONE;blend.DestBlend=D3D12_BLEND_ZERO;
    blend.BlendOp=D3D12_BLEND_OP_ADD;blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlendAlpha=D3D12_BLEND_ZERO;
    blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.DepthStencilState.DepthEnable=TRUE;pipeline.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
    pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
    pipeline.DepthStencilState.StencilReadMask=D3D12_DEFAULT_STENCIL_READ_MASK;
    pipeline.DepthStencilState.StencilWriteMask=D3D12_DEFAULT_STENCIL_WRITE_MASK;
    pipeline.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    pipeline.DepthStencilState.BackFace=pipeline.DepthStencilState.FrontFace;
    pipeline.SampleMask=UINT_MAX;pipeline.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets=1;pipeline.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;
    pipeline.DSVFormat=DXGI_FORMAT_D32_FLOAT;pipeline.SampleDesc.Count=1;
    check(device_->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(&pipeline_)),"Create graphics pipeline");
    check(device_->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator_.Get(),pipeline_.Get(),IID_PPV_ARGS(&commands_)),"Create command list");
    check(commands_->Close(),"Close initial command list");
    check(device_->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence_)),"Create GPU fence");
    fenceEvent_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!fenceEvent_) throw std::runtime_error("Cannot create GPU synchronization event.");
}

void Dx12Renderer::waitForGpu() {
    check(queue_->Signal(fence_.Get(),++fenceValue_),"Signal GPU fence");
    if(fence_->GetCompletedValue()<fenceValue_) {
        check(fence_->SetEventOnCompletion(fenceValue_,fenceEvent_),"Set GPU completion event");
        if(WaitForSingleObject(fenceEvent_,15000)!=WAIT_OBJECT_0) throw std::runtime_error("GPU did not complete work within 15 seconds.");
    }
}
void Dx12Renderer::createTargets() {
    auto handle=rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for(UINT i=0;i<2;++i) {
        check(swapChain_->GetBuffer(i,IID_PPV_ARGS(&targets_[i])),"Get back buffer");
        device_->CreateRenderTargetView(targets_[i].Get(),nullptr,handle);handle.ptr+=descriptorSize_;
    }
    D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=static_cast<UINT64>(width_);
    d.Height=static_cast<UINT>(height_);d.DepthOrArraySize=1;d.MipLevels=1;d.Format=DXGI_FORMAT_D32_FLOAT;
    d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    auto properties=heap(D3D12_HEAP_TYPE_DEFAULT);D3D12_CLEAR_VALUE clear{};clear.Format=d.Format;clear.DepthStencil.Depth=1;
    check(device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth_)),"Create depth buffer");
    device_->CreateDepthStencilView(depth_.Get(),nullptr,dsvHeap_->GetCPUDescriptorHandleForHeapStart());
}
void Dx12Renderer::resize(int width,int height) {
    if(width<=0 || height<=0 || (width==width_ && height==height_)) return;
    waitForGpu();for(auto& target:targets_) target.Reset();depth_.Reset();
    width_=width;height_=height;
    check(swapChain_->ResizeBuffers(2,static_cast<UINT>(width),static_cast<UINT>(height),DXGI_FORMAT_R8G8B8A8_UNORM,0),"Resize swap chain");
    createTargets();
}
void Dx12Renderer::setMesh(const SceneMesh& mesh) {
    if(mesh.vertices.empty()) throw std::invalid_argument("Cannot upload an empty scene.");
    waitForGpu();
    const UINT64 bytes=mesh.vertices.size()*sizeof(Vertex);
    if(bytes>UINT_MAX) throw std::invalid_argument("Mesh exceeds vertex buffer limit.");
    auto d=buffer(bytes);auto properties=heap(D3D12_HEAP_TYPE_UPLOAD);
    Ptr<ID3D12Resource> next;
    check(device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&next)),"Create geometry buffer");
    void* mapped{};D3D12_RANGE noReads{0,0};check(next->Map(0,&noReads,&mapped),"Map geometry buffer");
    std::memcpy(mapped,mesh.vertices.data(),static_cast<std::size_t>(bytes));next->Unmap(0,nullptr);
    vertices_=next;vertexView_={vertices_->GetGPUVirtualAddress(),static_cast<UINT>(bytes),sizeof(Vertex)};
    vertexCount_=static_cast<UINT>(mesh.vertices.size());pickPoints_=mesh.pickPoints;
}
XMMATRIX Dx12Renderer::view(const OrbitCamera& camera) const {
    auto e=camera.eye(),t=camera.target;
    return XMMatrixLookAtLH(XMVectorSet(e.x,e.y,e.z,1),XMVectorSet(t.x,t.y,t.z,1),XMVectorSet(0,1,0,0));
}
XMMATRIX Dx12Renderer::projection() const {return XMMatrixPerspectiveFovLH(XM_PIDIV4,aspect(),0.05f,2000.0f);}

void Dx12Renderer::render(const OrbitCamera& camera,float lighting,const std::wstring& capture) {
    if(!vertexCount_) return;
    UINT index=swapChain_->GetCurrentBackBufferIndex();
    check(allocator_->Reset(),"Reset command allocator");check(commands_->Reset(allocator_.Get(),pipeline_.Get()),"Reset command list");
    commands_->SetGraphicsRootSignature(root_.Get());
    struct Constants {XMFLOAT4X4 matrix;XMFLOAT4 eye;XMFLOAT4 settings;} constants{};
    XMStoreFloat4x4(&constants.matrix,XMMatrixTranspose(view(camera)*projection()));
    auto eye=camera.eye();constants.eye={eye.x,eye.y,eye.z,1};constants.settings={lighting,0,0,0};
    commands_->SetGraphicsRoot32BitConstants(0,24,&constants,0);
    D3D12_VIEWPORT vp{0,0,static_cast<float>(width_),static_cast<float>(height_),0,1};D3D12_RECT rect{0,0,width_,height_};
    commands_->RSSetViewports(1,&vp);commands_->RSSetScissorRects(1,&rect);
    auto barrier=transition(targets_[index].Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);
    commands_->ResourceBarrier(1,&barrier);
    auto rtv=rtvHeap_->GetCPUDescriptorHandleForHeapStart();rtv.ptr+=index*descriptorSize_;
    auto dsv=dsvHeap_->GetCPUDescriptorHandleForHeapStart();commands_->OMSetRenderTargets(1,&rtv,FALSE,&dsv);
    const float clear[]={0.027f,0.045f,0.067f,1};
    commands_->ClearRenderTargetView(rtv,clear,0,nullptr);commands_->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
    commands_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);commands_->IASetVertexBuffers(0,1,&vertexView_);
    commands_->DrawInstanced(vertexCount_,1,0,0);
    Ptr<ID3D12Resource> readback;D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 total{};
    if(!capture.empty()) {
        auto targetDesc=targets_[index]->GetDesc();device_->GetCopyableFootprints(&targetDesc,0,1,0,&footprint,nullptr,nullptr,&total);
        auto d=buffer(total);auto properties=heap(D3D12_HEAP_TYPE_READBACK);
        check(device_->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"Create screenshot readback");
        barrier=transition(targets_[index].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);commands_->ResourceBarrier(1,&barrier);
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=footprint;
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=targets_[index].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        commands_->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        barrier=transition(targets_[index].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_PRESENT);
    } else barrier=transition(targets_[index].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);
    commands_->ResourceBarrier(1,&barrier);check(commands_->Close(),"Close render commands");
    ID3D12CommandList* lists[]={commands_.Get()};queue_->ExecuteCommandLists(1,lists);
    check(swapChain_->Present(1,0),"Present frame");
    // Deliberately serialize frames in v0.1: safe, simple resource lifetime management.
    waitForGpu();
    if(readback) {
        std::ofstream output(std::filesystem::path(capture),std::ios::binary);
        if(!output) throw std::runtime_error("Cannot write screenshot.");
        BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=width_;info.biHeight=-height_;
        info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;info.biSizeImage=static_cast<DWORD>(width_*height_*4);
        file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+info.biSizeImage;
        output.write(reinterpret_cast<const char*>(&file),sizeof(file));output.write(reinterpret_cast<const char*>(&info),sizeof(info));
        void* mapped{};D3D12_RANGE reads{0,static_cast<SIZE_T>(total)};check(readback->Map(0,&reads,&mapped),"Read screenshot");
        std::vector<unsigned char> row(static_cast<std::size_t>(width_)*4);capturePixels_=0;
        for(int y=0;y<height_;++y) {
            auto* source=static_cast<const unsigned char*>(mapped)+footprint.Offset+static_cast<std::size_t>(y)*footprint.Footprint.RowPitch;
            for(int x=0;x<width_;++x) {
                const auto i=static_cast<std::size_t>(x)*4;
                row[i]=source[i+2];row[i+1]=source[i+1];row[i+2]=source[i];row[i+3]=255;
                if(source[i]>45 || source[i+1]>45 || source[i+2]>45) ++capturePixels_;
            }
            output.write(reinterpret_cast<const char*>(row.data()),static_cast<std::streamsize>(row.size()));
        }
        D3D12_RANGE noWrites{0,0};readback->Unmap(0,&noWrites);
        if(!output) throw std::runtime_error("Screenshot write failed.");
    }
}
int Dx12Renderer::pick(int x,int y,const OrbitCamera& camera) const {
    float best=24.0f*24.0f,depth=1;int index=-1;
    for(const auto& point:pickPoints_) {
        XMFLOAT3 p;XMStoreFloat3(&p,XMVector3Project(XMVectorSet(point.position.x,point.position.y,point.position.z,1),
            0,0,static_cast<float>(width_),static_cast<float>(height_),0,1,projection(),view(camera),XMMatrixIdentity()));
        float dx=p.x-static_cast<float>(x),dy=p.y-static_cast<float>(y),distance=dx*dx+dy*dy;
        if(p.z<0 || p.z>1) continue;
        if(distance<best || (std::abs(distance-best)<0.5f && p.z<depth)) {best=distance;depth=p.z;index=static_cast<int>(point.index);}
    }
    return index;
}
}
