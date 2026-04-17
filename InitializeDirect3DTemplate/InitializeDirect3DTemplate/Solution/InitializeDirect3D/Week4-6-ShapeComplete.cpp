#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "../../Common/Camera.h"
#include "FrameResource.h"
#include <DirectXCollision.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

struct RenderItem
{
    RenderItem() = default;

    XMFLOAT4X4 World = MathHelper::Identity4x4();
    int NumFramesDirty = gNumFrameResources;
    UINT ObjCBIndex = -1;

    Material* Mat = nullptr;
    MeshGeometry* Geo = nullptr;

    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

class ShapesApp : public D3DApp
{
public:
    ShapesApp(HINSTANCE hInstance);
    ShapesApp(const ShapesApp& rhs) = delete;
    ShapesApp& operator=(const ShapesApp& rhs) = delete;
    ~ShapesApp();

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void OnKeyboardInput(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateMaterialCBs(const GameTimer& gt);

    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildMazeGeometry();
    void BuildMaterials();
    void BuildRenderItems();
    void BuildFrameResources();
    void BuildPSOs();
    void BuildTextures();
    void BuildDescriptorHeaps();

    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
    bool CheckCollision(const DirectX::XMFLOAT3& position, float radius);

private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;
    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
    std::vector<std::unique_ptr<RenderItem>> mAllRitems;
    std::vector<RenderItem*> mOpaqueRitems;
    std::vector<RenderItem*> mTransparentRitems;
    PassConstants mMainPassCB;


    bool mIsWireframe = false;

    Camera mCamera;
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mStartX = 0.0f;
    float mStartY = 4.0f;
    float mStartZ = 0.0f;

    std::vector<DirectX::BoundingBox> mMazeWallBounds;
    float mCollisionRadius = 0.8f;

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        ShapesApp theApp(hInstance);
        if (!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch (DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

ShapesApp::ShapesApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

ShapesApp::~ShapesApp()
{
    if (md3dDevice != nullptr)
        FlushCommandQueue();
}

bool ShapesApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
    BuildMazeGeometry();
    BuildTextures();
    BuildMaterials();
    BuildRenderItems();
    BuildDescriptorHeaps();
    BuildFrameResources();
    BuildPSOs();

    mStartX = 0.0f;

    float castleCenterZ = 15.0f;
    float uD = 80.0f;
    float wallT = 1.2f;
    float innerDepth = 16.0f;

    float zFront = castleCenterZ + uD * 0.5f;
    float innerCenterZ = zFront - (wallT * 0.5f) - (innerDepth * 0.5f);

    mStartZ = (zFront + innerCenterZ) * 0.5f;
    mStartY = 4.0f;

    mCamera.LookAt(
        XMFLOAT3(mStartX, mStartY, mStartZ),
        XMFLOAT3(mStartX, mStartY, mStartZ - 1.0f),
        XMFLOAT3(0.0f, 1.0f, 0.0f));
    mCamera.Walk(100.0f);
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    FlushCommandQueue();

    return true;
}
void ShapesApp::OnResize()
{
    D3DApp::OnResize();
    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void ShapesApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
    mCamera.UpdateViewMatrix();

    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    UpdateObjectCBs(gt);
    UpdateMaterialCBs(gt);
    UpdateMainPassCB(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    ThrowIfFailed(cmdListAlloc->Reset());

    if (mIsWireframe)
    {
        ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
    }
    else
    {
        ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
    }

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET));

    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(
        DepthStencilView(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f,
        0,
        0,
        nullptr);

    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    ID3D12Resource* passCB = mCurrFrameResource->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

    mCommandList->SetPipelineState(mPSOs["transparent"].Get());
    DrawRenderItems(mCommandList.Get(), mTransparentRitems);

    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT));

    ThrowIfFailed(mCommandList->Close());

    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    mCurrFrameResource->Fence = ++mCurrentFence;
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);

}

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;
    SetCapture(mhMainWnd);
}

void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
    float dt = gt.DeltaTime();

    if (GetAsyncKeyState('1') & 0x8000)
        mIsWireframe = true;
    else
        mIsWireframe = false;

    XMFLOAT3 oldPos = mCamera.GetPosition3f();

    if (GetAsyncKeyState('W') & 0x8000)
        mCamera.Walk(6.0f * dt);

    if (GetAsyncKeyState('S') & 0x8000)
        mCamera.Walk(-6.0f * dt);

    if (GetAsyncKeyState('A') & 0x8000)
        mCamera.Strafe(-6.0f * dt);

    if (GetAsyncKeyState('D') & 0x8000)
        mCamera.Strafe(6.0f * dt);

    XMFLOAT3 newPos = mCamera.GetPosition3f();

    if (CheckCollision(newPos, mCollisionRadius))
    {
        mCamera.SetPosition(oldPos.x, oldPos.y, oldPos.z);
    }
}



bool ShapesApp::CheckCollision(const DirectX::XMFLOAT3& position, float radius)
{
    XMVECTOR pos = XMLoadFloat3(&position);

    for (const auto& box : mMazeWallBounds)
    {
        XMVECTOR boxCenter = XMLoadFloat3(&box.Center);
        XMVECTOR boxExtents = XMLoadFloat3(&box.Extents);

        XMVECTOR closestPoint = XMVectorClamp(
            pos,
            XMVectorSubtract(boxCenter, boxExtents),
            XMVectorAdd(boxCenter, boxExtents));

        XMVECTOR delta = XMVectorSubtract(closestPoint, pos);
        float distance = XMVectorGetX(XMVector3Length(delta));

        if (distance < radius)
            return true;
    }

    return false;
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();

    for (auto& e : mAllRitems)
    {
        if (e->NumFramesDirty > 0)
        {
            XMMATRIX world = XMLoadFloat4x4(&e->World);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);
            e->NumFramesDirty--;
        }
    }
}

void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
{
    XMMATRIX view = mCamera.GetView();
    XMMATRIX proj = mCamera.GetProj();

    XMMATRIX viewProj = XMMatrixMultiply(view, proj);
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));

    mMainPassCB.EyePosW = mCamera.GetPosition3f();
    mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.NearZ = 1.0f;
    mMainPassCB.FarZ = 1000.0f;
    mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.DeltaTime = gt.DeltaTime();

    mMainPassCB.AmbientLight = XMFLOAT4(0.12f, 0.12f, 0.16f, 1.0f);

    // Main directional light
    mMainPassCB.Lights[0].Direction = { 0.577f, -0.577f, 0.577f };
    mMainPassCB.Lights[0].Strength = { 0.75f, 0.72f, 0.68f };

    // Red point light
    mMainPassCB.Lights[1].Position = { -7.0f, 2.2f, -5.0f };
    mMainPassCB.Lights[1].Strength = { 1.0f, 0.2f, 0.2f };
    mMainPassCB.Lights[1].FalloffStart = 1.0f;
    mMainPassCB.Lights[1].FalloffEnd = 10.0f;

    // Blue point light
    mMainPassCB.Lights[2].Position = { 7.0f, 2.2f, -5.0f };
    mMainPassCB.Lights[2].Strength = { 0.2f, 0.4f, 1.0f };
    mMainPassCB.Lights[2].FalloffStart = 1.0f;
    mMainPassCB.Lights[2].FalloffEnd = 10.0f;

    // Purple point light
    mMainPassCB.Lights[3].Position = { -7.0f, 2.2f, 5.0f };
    mMainPassCB.Lights[3].Strength = { 0.8f, 0.2f, 1.0f };
    mMainPassCB.Lights[3].FalloffStart = 1.0f;
    mMainPassCB.Lights[3].FalloffEnd = 10.0f;

    // Cyan point light
    mMainPassCB.Lights[4].Position = { 7.0f, 2.2f, 5.0f };
    mMainPassCB.Lights[4].Strength = { 0.2f, 1.0f, 1.0f };
    mMainPassCB.Lights[4].FalloffStart = 1.0f;
    mMainPassCB.Lights[4].FalloffEnd = 10.0f;

    // STRONG WHITE LIGHT (torus center highlight)
    mMainPassCB.Lights[5].Position = { 0.0f, 2.8f, 0.0f };
    mMainPassCB.Lights[5].Strength = { 2.5f, 2.5f, 2.5f };
    mMainPassCB.Lights[5].FalloffStart = 0.5f;
    mMainPassCB.Lights[5].FalloffEnd = 4.0f;
    auto currPassCB = mCurrFrameResource->PassCB.get();

    // Maze corner lights
    mMainPassCB.Lights[6].Position = { -28.0f, 3.0f, -43.0f };
    mMainPassCB.Lights[6].Strength = { 1.0f, 0.2f, 0.2f };   // red
    mMainPassCB.Lights[6].FalloffStart = 1.0f;
    mMainPassCB.Lights[6].FalloffEnd = 18.0f;

    mMainPassCB.Lights[7].Position = { 20.0f, 3.0f, -43.0f };
    mMainPassCB.Lights[7].Strength = { 0.2f, 0.4f, 1.0f };   // blue
    mMainPassCB.Lights[7].FalloffStart = 1.0f;
    mMainPassCB.Lights[7].FalloffEnd = 18.0f;

    mMainPassCB.Lights[8].Position = { -28.0f, 3.0f, 5.0f };
    mMainPassCB.Lights[8].Strength = { 0.8f, 0.2f, 1.0f };   // purple
    mMainPassCB.Lights[8].FalloffStart = 1.0f;
    mMainPassCB.Lights[8].FalloffEnd = 18.0f;

    mMainPassCB.Lights[9].Position = { 20.0f, 3.0f, 5.0f };
    mMainPassCB.Lights[9].Strength = { 0.2f, 1.0f, 1.0f };   // cyan
    mMainPassCB.Lights[9].FalloffStart = 1.0f;
    mMainPassCB.Lights[9].FalloffEnd = 18.0f;

    // Castle corner lights
    mMainPassCB.Lights[10].Position = { -27.0f, 8.0f, 17.0f };
    mMainPassCB.Lights[10].Strength = { 1.0f, 0.5f, 0.2f };   // orange
    mMainPassCB.Lights[10].FalloffStart = 1.0f;
    mMainPassCB.Lights[10].FalloffEnd = 20.0f;

    mMainPassCB.Lights[11].Position = { 27.0f, 8.0f, 17.0f };
    mMainPassCB.Lights[11].Strength = { 0.3f, 1.0f, 0.3f };   // green
    mMainPassCB.Lights[11].FalloffStart = 1.0f;
    mMainPassCB.Lights[11].FalloffEnd = 20.0f;

    mMainPassCB.Lights[12].Position = { -27.0f, 8.0f, 43.0f };
    mMainPassCB.Lights[12].Strength = { 1.0f, 0.2f, 0.8f };   // pink
    mMainPassCB.Lights[12].FalloffStart = 1.0f;
    mMainPassCB.Lights[12].FalloffEnd = 20.0f;

    mMainPassCB.Lights[13].Position = { 27.0f, 8.0f, 43.0f };
    mMainPassCB.Lights[13].Strength = { 1.0f, 1.0f, 0.2f };   // yellow
    mMainPassCB.Lights[13].FalloffStart = 1.0f;
    mMainPassCB.Lights[13].FalloffEnd = 20.0f;
    currPassCB->CopyData(0, mMainPassCB);

    // STRONG WHITE LIGHT (castle interior)
    mMainPassCB.Lights[14].Position = { 0.0f, 6.0f, -15.0f };
    mMainPassCB.Lights[14].Strength = { 2.5f, 2.5f, 2.5f };
    mMainPassCB.Lights[14].FalloffStart = 0.5f;
    mMainPassCB.Lights[14].FalloffEnd = 10.0f;
    int lightIndex = 6;
    int maxLights = 16;

    // MAZE lights (grid system)
    for (int x = -25; x < 20; x += 10)
    {
        for (int z = -40; z < 10; z += 10)
        {
            if (lightIndex >= maxLights)
                break;

            mMainPassCB.Lights[lightIndex].Position = { (float)x, 3.0f, (float)z };

            // random color
            float r = (rand() % 100) / 100.0f;
            float g = (rand() % 100) / 100.0f;
            float b = (rand() % 100) / 100.0f;

            mMainPassCB.Lights[lightIndex].Strength = { r, g, b };

            mMainPassCB.Lights[lightIndex].FalloffStart = 1.0f;
            mMainPassCB.Lights[lightIndex].FalloffEnd = 15.0f;

            lightIndex++;
        }
    }
    if (lightIndex < maxLights)
    {
        mMainPassCB.Lights[lightIndex].Position = { 0.0f, 6.0f, -15.0f };
        mMainPassCB.Lights[lightIndex].Strength = { 2.5f, 2.2f, 2.0f };
        mMainPassCB.Lights[lightIndex].FalloffStart = 0.5f;
        mMainPassCB.Lights[lightIndex].FalloffEnd = 12.0f;

        lightIndex++;
    }
}
void ShapesApp::UpdateMaterialCBs(const GameTimer& gt)
{
    auto currMaterialCB = mCurrFrameResource->MaterialCB.get();

    for (auto& e : mMaterials)
    {
        Material* mat = e.second.get();

        if (mat->NumFramesDirty > 0)
        {
            MaterialConstants matConstants;
            matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
            matConstants.FresnelR0 = mat->FresnelR0;
            matConstants.Roughness = mat->Roughness;

            currMaterialCB->CopyData(mat->MatCBIndex, matConstants);
            mat->NumFramesDirty--;
        }
    }
}

void ShapesApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER slotRootParameter[4];
    slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[1].InitAsConstantBufferView(0); // ObjectCB
    slotRootParameter[2].InitAsConstantBufferView(1); // PassCB
    slotRootParameter[3].InitAsConstantBufferView(2); // MaterialCB

    auto sampler = CD3DX12_STATIC_SAMPLER_DESC(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        4,
        slotRootParameter,
        1,
        &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(),
        errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());

    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void ShapesApp::BuildShadersAndInputLayout()
{
    mShaders["standardVS"] = d3dUtil::CompileShader(
        L"Shaders\\VS.hlsl", nullptr, "VS", "vs_5_1");

    mShaders["opaquePS"] = d3dUtil::CompileShader(
        L"Shaders\\PS.hlsl", nullptr, "PS", "ps_5_1");

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void ShapesApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
    auto box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);
    auto grid = geoGen.CreateGrid(80.0f, 120.0f, 160, 120);
    auto sphere = geoGen.CreateSphere(0.5f, 20, 20);
    auto cylinder = geoGen.CreateCylinder(0.5f, 0.5f, 3.0f, 20, 20);
    auto cone = geoGen.CreateCone(1.0f, 1.0f, 20, 20);
    auto torus = geoGen.CreateTorus(1.0f, 24, 16);
    auto pyramid = geoGen.CreatePyramid(1.5f, 2.0f, 1.5f);
    auto wedge = geoGen.CreateWedge(2.0f, 1.0f, 2.0f);
    auto diamond = geoGen.CreateDiamond(0.8f);
    auto triPrism = geoGen.CreateTriPrism(1.5f, 1.5f, 2.0f);

    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT)box.Vertices.size();
    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
    UINT coneVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
    UINT torusVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
    UINT pyramidVertexOffset = torusVertexOffset + (UINT)torus.Vertices.size();
    UINT wedgeVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();
    UINT diamondVertexOffset = wedgeVertexOffset + (UINT)wedge.Vertices.size();
    UINT triPrismVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();

    UINT boxIndexOffset = 0;
    UINT gridIndexOffset = (UINT)box.Indices32.size();
    UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
    UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
    UINT coneIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
    UINT torusIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
    UINT pyramidIndexOffset = torusIndexOffset + (UINT)torus.Indices32.size();
    UINT wedgeIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();
    UINT diamondIndexOffset = wedgeIndexOffset + (UINT)wedge.Indices32.size();
    UINT triPrismIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();

    SubmeshGeometry boxSubmesh;
    boxSubmesh.IndexCount = (UINT)box.Indices32.size();
    boxSubmesh.StartIndexLocation = boxIndexOffset;
    boxSubmesh.BaseVertexLocation = boxVertexOffset;

    SubmeshGeometry gridSubmesh;
    gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
    gridSubmesh.StartIndexLocation = gridIndexOffset;
    gridSubmesh.BaseVertexLocation = gridVertexOffset;

    SubmeshGeometry sphereSubmesh;
    sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
    sphereSubmesh.StartIndexLocation = sphereIndexOffset;
    sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

    SubmeshGeometry cylinderSubmesh;
    cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
    cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
    cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

    SubmeshGeometry coneSubmesh;
    coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
    coneSubmesh.StartIndexLocation = coneIndexOffset;
    coneSubmesh.BaseVertexLocation = coneVertexOffset;

    SubmeshGeometry torusSubmesh;
    torusSubmesh.IndexCount = (UINT)torus.Indices32.size();
    torusSubmesh.StartIndexLocation = torusIndexOffset;
    torusSubmesh.BaseVertexLocation = torusVertexOffset;

    SubmeshGeometry pyramidSubmesh;
    pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
    pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
    pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

    SubmeshGeometry wedgeSubmesh;
    wedgeSubmesh.IndexCount = (UINT)wedge.Indices32.size();
    wedgeSubmesh.StartIndexLocation = wedgeIndexOffset;
    wedgeSubmesh.BaseVertexLocation = wedgeVertexOffset;

    SubmeshGeometry diamondSubmesh;
    diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
    diamondSubmesh.StartIndexLocation = diamondIndexOffset;
    diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

    SubmeshGeometry triPrismSubmesh;
    triPrismSubmesh.IndexCount = (UINT)triPrism.Indices32.size();
    triPrismSubmesh.StartIndexLocation = triPrismIndexOffset;
    triPrismSubmesh.BaseVertexLocation = triPrismVertexOffset;

    auto totalVertexCount =
        box.Vertices.size() +
        grid.Vertices.size() +
        sphere.Vertices.size() +
        cylinder.Vertices.size() +
        cone.Vertices.size() +
        torus.Vertices.size() +
        pyramid.Vertices.size() +
        wedge.Vertices.size() +
        diamond.Vertices.size() +
        triPrism.Vertices.size();

    std::vector<Vertex> vertices(totalVertexCount);

    UINT k = 0;

    for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = box.Vertices[i].Position;
        vertices[k].Normal = box.Vertices[i].Normal;

        float localX = box.Vertices[i].Position.x;
        float localY = box.Vertices[i].Position.y;
        float localZ = box.Vertices[i].Position.z;

        float texScaleSide = 1.0f;
        float texScaleTop = 0.4f;

        if (fabs(box.Vertices[i].Normal.y) > 0.9f)
        {
            // top / bottom
            vertices[k].TexC = XMFLOAT2(
                localX * texScaleTop + 0.5f,
                localZ * texScaleTop + 0.5f
            );
        }
        else if (fabs(box.Vertices[i].Normal.x) > 0.9f)
        {
            // left / right
            vertices[k].TexC = XMFLOAT2(
                localZ * texScaleSide + 0.5f,
                localY * texScaleSide + 0.5f
            );
        }
        else
        {
            // front / back
            vertices[k].TexC = XMFLOAT2(
                localX * texScaleSide + 0.5f,
                localY * texScaleSide + 0.5f
            );
        }
    }
    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Normal = grid.Vertices[i].Normal;
        vertices[k].TexC = grid.Vertices[i].TexC;
    }
    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = sphere.Vertices[i].Position;
        vertices[k].Normal = sphere.Vertices[i].Normal;
        vertices[k].TexC = sphere.Vertices[i].TexC;
    }
    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = cylinder.Vertices[i].Position;
        vertices[k].Normal = cylinder.Vertices[i].Normal;
        vertices[k].TexC = cylinder.Vertices[i].TexC;
    }
    for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = cone.Vertices[i].Position;
        vertices[k].Normal = cone.Vertices[i].Normal;
        vertices[k].TexC = cone.Vertices[i].TexC;
    }
    for (size_t i = 0; i < torus.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = torus.Vertices[i].Position;
        vertices[k].Normal = torus.Vertices[i].Normal;
        vertices[k].TexC = torus.Vertices[i].TexC;
    }
    for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = pyramid.Vertices[i].Position;
        vertices[k].Normal = pyramid.Vertices[i].Normal;
        vertices[k].TexC = pyramid.Vertices[i].TexC;
    }
    for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = wedge.Vertices[i].Position;
        vertices[k].Normal = wedge.Vertices[i].Normal;
        vertices[k].TexC = wedge.Vertices[i].TexC;
    }
    for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = diamond.Vertices[i].Position;
        vertices[k].Normal = diamond.Vertices[i].Normal;
        vertices[k].TexC = diamond.Vertices[i].TexC;
    }
    for (size_t i = 0; i < triPrism.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = triPrism.Vertices[i].Position;
        vertices[k].Normal = triPrism.Vertices[i].Normal;
        vertices[k].TexC = triPrism.Vertices[i].TexC;
    }

    std::vector<std::uint16_t> indices;
    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
    indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
    indices.insert(indices.end(), begin(cone.GetIndices16()), end(cone.GetIndices16()));
    indices.insert(indices.end(), begin(torus.GetIndices16()), end(torus.GetIndices16()));
    indices.insert(indices.end(), begin(pyramid.GetIndices16()), end(pyramid.GetIndices16()));
    indices.insert(indices.end(), begin(wedge.GetIndices16()), end(wedge.GetIndices16()));
    indices.insert(indices.end(), begin(diamond.GetIndices16()), end(diamond.GetIndices16()));
    indices.insert(indices.end(), begin(triPrism.GetIndices16()), end(triPrism.GetIndices16()));

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    geo->DrawArgs["box"] = boxSubmesh;
    geo->DrawArgs["grid"] = gridSubmesh;
    geo->DrawArgs["sphere"] = sphereSubmesh;
    geo->DrawArgs["cylinder"] = cylinderSubmesh;
    geo->DrawArgs["cone"] = coneSubmesh;
    geo->DrawArgs["torus"] = torusSubmesh;
    geo->DrawArgs["pyramid"] = pyramidSubmesh;
    geo->DrawArgs["wedge"] = wedgeSubmesh;
    geo->DrawArgs["diamond"] = diamondSubmesh;
    geo->DrawArgs["triPrism"] = triPrismSubmesh;

    mGeometries[geo->Name] = std::move(geo);
}


void ShapesApp::BuildMazeGeometry()
{
    GeometryGenerator geoGen;

    mMazeWallBounds.clear(); // clear old collision boxes

    std::vector<Vertex> allVertices;
    std::vector<std::uint16_t> allIndices;
    UINT vertexOffset = 0;

    // 2D maze layout (1 = wall, 0 = empty space)
    const int maze[15][15] =
    {
        {1,1,1,1,1,1,1,1,1,0,1,1,1,1,1},
        {1,0,0,0,1,0,0,0,0,0,1,0,0,0,1},
        {1,0,1,0,1,0,1,1,1,0,1,1,1,0,1},
        {1,0,1,0,0,0,1,0,0,0,0,0,1,0,1},
        {1,0,1,1,1,0,1,0,1,1,1,0,1,0,1},
        {1,0,0,0,1,0,0,0,1,0,0,0,1,0,1},
        {1,1,1,0,1,1,1,0,1,0,1,1,1,0,1},
        {1,0,0,0,0,0,1,0,0,0,1,0,0,0,1},
        {1,0,1,1,1,0,1,1,1,0,1,0,1,1,1},
        {1,0,1,0,0,0,0,0,1,0,1,0,0,0,0},
        {1,0,1,0,1,1,1,0,1,1,1,1,1,0,1},
        {1,0,0,0,1,0,0,0,1,0,0,0,1,0,1},
        {1,1,1,0,1,1,1,1,1,1,1,0,1,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
    };

    const int rows = 15;
    const int cols = 15;

    float cellSize = 3.0f;   // size of each cell
    float wallHeight = 7.0f; // height of walls
    float wallThickness = 1.0f;

    float startX = -25.0f;   // maze position in world
    float startZ = -40.0f;

    // function to add one wall cube
    auto addWall = [&](float x, float z)
        {
            float groundY = -0.5f;

            // collision box for this wall
            DirectX::BoundingBox box;
            box.Center = XMFLOAT3(x, groundY + wallHeight * 0.5f, z);
            box.Extents = XMFLOAT3(cellSize * 0.5f, wallHeight * 0.5f, cellSize * 0.5f);
            mMazeWallBounds.push_back(box);

            // create cube mesh
            auto cube = geoGen.CreateBox(cellSize, wallHeight, cellSize, 0);

            for (auto& v : cube.Vertices)
            {
                Vertex vert;

                // convert local vertex to world position
                float worldX = v.Position.x + x;
                float worldY = v.Position.y + groundY + wallHeight * 0.5f;
                float worldZ = v.Position.z + z;

                vert.Pos = XMFLOAT3(worldX, worldY, worldZ);
                vert.Normal = v.Normal;

                // simple texture mapping
                float texScale = 0.2f;

                if (fabs(v.Normal.y) > 0.9f)
                {
                    // top/bottom faces
                    vert.TexC = XMFLOAT2(worldX * texScale, worldZ * texScale);
                }
                else if (fabs(v.Normal.x) > 0.9f)
                {
                    // side faces (left/right)
                    vert.TexC = XMFLOAT2(worldZ * texScale, worldY * texScale);
                }
                else
                {
                    // front/back faces
                    vert.TexC = XMFLOAT2(worldX * texScale, worldY * texScale);
                }

                allVertices.push_back(vert);
            }

            // add indices with offset
            for (auto idx : cube.Indices32)
            {
                allIndices.push_back((std::uint16_t)(vertexOffset + idx));
            }

            vertexOffset += (UINT)cube.Vertices.size();
        };

    // loop through maze and place walls
    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            if (maze[r][c] == 1) // if wall
            {
                float x = startX + c * cellSize;
                float z = startZ + r * cellSize;
                addWall(x, z);
            }
        }
    }

    // create GPU buffers
    const UINT vbByteSize = (UINT)allVertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)allIndices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "mazeGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), allVertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), allIndices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        allVertices.data(),
        vbByteSize,
        geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        allIndices.data(),
        ibByteSize,
        geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    // define submesh
    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)allIndices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["mazeWalls"] = submesh;

    mGeometries["mazeGeo"] = std::move(geo);
}

void ShapesApp::BuildMaterials()
{
    auto grass = std::make_unique<Material>();
    grass->Name = "grass";
    grass->MatCBIndex = 0;
    grass->DiffuseSrvHeapIndex = 0;
    grass->DiffuseAlbedo = XMFLOAT4(1, 1, 1, 1);
    grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    grass->Roughness = 0.90f;

    auto stone = std::make_unique<Material>();
    stone->Name = "stone";
    stone->MatCBIndex = 1;
    stone->DiffuseSrvHeapIndex = 1;
    stone->DiffuseAlbedo = XMFLOAT4(1, 1, 1, 1);
    stone->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    stone->Roughness = 0.30f;

    auto crystal = std::make_unique<Material>();
    crystal->Name = "crystal";
    crystal->MatCBIndex = 2;
    crystal->DiffuseSrvHeapIndex = 2;
    crystal->DiffuseAlbedo = XMFLOAT4(0.2f, 0.8f, 1.0f, 1.0f);
    crystal->FresnelR0 = XMFLOAT3(0.08f, 0.08f, 0.08f);
    crystal->Roughness = 0.10f;

    auto water = std::make_unique<Material>();
    water->Name = "water";
    water->MatCBIndex = 3;
    water->DiffuseSrvHeapIndex = 3;
    water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.45f);
    water->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    water->Roughness = 0.05f;

    auto tile = std::make_unique<Material>();
    tile->Name = "tile";
    tile->MatCBIndex = 4;
    tile->DiffuseSrvHeapIndex = 4;
    tile->DiffuseAlbedo = XMFLOAT4(1, 1, 1, 1);
    tile->FresnelR0 = XMFLOAT3(0.03f, 0.03f, 0.03f);
    tile->Roughness = 0.40f;

    mMaterials["grass"] = std::move(grass);
    mMaterials["stone"] = std::move(stone);
    mMaterials["crystal"] = std::move(crystal);
    mMaterials["water"] = std::move(water);
    mMaterials["tile"] = std::move(tile);
}
void ShapesApp::BuildRenderItems()
{
    UINT objCBIndex = 0;

    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
    gridRitem->ObjCBIndex = objCBIndex++;
    gridRitem->Geo = mGeometries["shapeGeo"].get();
    gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
    gridRitem->Mat = mMaterials["grass"].get();
    mAllRitems.push_back(std::move(gridRitem));

    XMMATRIX castleRot = XMMatrixRotationY(XMConvertToRadians(180.0f));

    auto AddItem = [&](const std::string& key, const XMMATRIX& world, const std::string& matName)
        {
            auto r = std::make_unique<RenderItem>();

            //  TÜM KALEYE TEK NOKTADAN ROTATION
            XMMATRIX finalWorld = world * castleRot;

            XMStoreFloat4x4(&r->World, finalWorld);

            r->ObjCBIndex = objCBIndex++;
            r->Geo = mGeometries["shapeGeo"].get();
            r->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            r->IndexCount = r->Geo->DrawArgs[key].IndexCount;
            r->StartIndexLocation = r->Geo->DrawArgs[key].StartIndexLocation;
            r->BaseVertexLocation = r->Geo->DrawArgs[key].BaseVertexLocation;
            r->Mat = mMaterials[matName].get();

            mAllRitems.push_back(std::move(r));
        };

    const float castleX = 0.0f;
    const float castleZ = -30.0f;
    const float castleCenterX = castleX;
    const float castleCenterZ = castleZ;
    const float uW = 50.0f;
    const float uD = 30.0f;
    const float wallH = 16.0f;
    const float wallT = 1.2f;
    const float wallY = wallH * 0.5f;
    const float xLeft = castleCenterX - uW * 0.5f;
    const float xRight = castleCenterX + uW * 0.5f;
    const float zBack = castleCenterZ - uD * 0.5f;
    const float zFront = castleCenterZ + uD * 0.5f;

    const float gateGapW = 16.0f;

    AddItem("box", XMMatrixScaling(uW, wallH, wallT) * XMMatrixTranslation(castleCenterX, wallY, zBack), "stone");
    AddItem("box", XMMatrixScaling(wallT, wallH, uD) * XMMatrixTranslation(xLeft, wallY, castleCenterZ), "stone");
    AddItem("box", XMMatrixScaling(wallT, wallH, uD) * XMMatrixTranslation(xRight, wallY, castleCenterZ), "stone");

    const float frontSegLen = (uW - gateGapW) * 0.5f;
    const float frontSegCenterOffset = (gateGapW * 0.5f) + (frontSegLen * 0.5f);

    AddItem("box", XMMatrixScaling(frontSegLen, wallH, wallT) * XMMatrixTranslation(-frontSegCenterOffset, wallY, zFront), "stone");
    AddItem("box", XMMatrixScaling(frontSegLen, wallH, wallT) * XMMatrixTranslation(+frontSegCenterOffset, wallY, zFront), "stone");

    const float innerDepth = 16.0f;
    const float innerCenterZ = zFront - (wallT * 0.5f) - (innerDepth * 0.5f);
    const float innerLeftX = castleCenterX - gateGapW * 0.5f;
    const float innerRightX = castleCenterX + gateGapW * 0.5f;

    AddItem("box", XMMatrixScaling(wallT, wallH, innerDepth) * XMMatrixTranslation(innerLeftX, wallY, innerCenterZ), "stone");
    AddItem("box", XMMatrixScaling(wallT, wallH, innerDepth) * XMMatrixTranslation(innerRightX, wallY, innerCenterZ), "stone");

    const float toothW = 3.5f;
    const float toothH = 2.0f;
    const float toothTopY = wallH + toothH * 0.5f;

    auto AddTeethAlongX = [&](float zWall, float xMin, float xMax)
        {
            int idx = 0;
            for (float x = xMin + toothW * 0.5f; x <= xMax - toothW * 0.5f; x += toothW, ++idx)
            {
                if ((idx % 2) == 0)
                {
                    AddItem("box", XMMatrixScaling(toothW, toothH, wallT) * XMMatrixTranslation(x, toothTopY, zWall), "stone");
                }
            }
        };

    auto AddTeethAlongZ = [&](float xWall, float zMin, float zMax)
        {
            int idx = 0;
            for (float z = zMin + toothW * 0.5f; z <= zMax - toothW * 0.5f; z += toothW, ++idx)
            {
                if ((idx % 2) == 0)
                {
                    AddItem("box", XMMatrixScaling(wallT, toothH, toothW) * XMMatrixTranslation(xWall, toothTopY, z), "stone");
                }
            }
        };

    AddTeethAlongX(zBack, xLeft, xRight);
    AddTeethAlongZ(xLeft, zBack, zFront);
    AddTeethAlongZ(xRight, zBack, zFront);
    AddTeethAlongX(zFront, xLeft, -gateGapW * 0.5f);
    AddTeethAlongX(zFront, +gateGapW * 0.5f, xRight);

    const float cylMeshH = 3.0f;
    const float cylMeshR = 0.5f;
    const float postWorldH = wallH + 6.0f;
    const float scaleY = postWorldH / cylMeshH;
    const float scaleXZ = 3.6f;
    XMMATRIX postS = XMMatrixScaling(scaleXZ, scaleY, scaleXZ);
    const float postY = (cylMeshH * scaleY) * 0.5f;
    const float towerOut = (wallT * 0.5f) + (cylMeshR * scaleXZ);

    const float TLx = xLeft - towerOut;
    const float TRx = xRight + towerOut;
    const float backZ2 = zBack - towerOut;
    const float frontZ2 = zFront + towerOut;

    AddItem("cylinder", postS * XMMatrixTranslation(TLx, postY, backZ2), "stone");
    AddItem("cylinder", postS * XMMatrixTranslation(TRx, postY, backZ2), "stone");
    AddItem("cylinder", postS * XMMatrixTranslation(TLx, postY, frontZ2), "stone");
    AddItem("cylinder", postS * XMMatrixTranslation(TRx, postY, frontZ2), "stone");

    const float postWorldR = cylMeshR * scaleXZ;
    const float coneWorldR = postWorldR * 2.0f;
    const float coneWorldH = wallH * 2.6f;
    XMMATRIX coneS = XMMatrixScaling(coneWorldR, coneWorldH, coneWorldR);
    const float coneY = postWorldH + (coneWorldH * 0.5f);

    AddItem("cone", coneS * XMMatrixTranslation(TLx, coneY, backZ2), "stone");
    AddItem("cone", coneS * XMMatrixTranslation(TRx, coneY, backZ2), "stone");
    AddItem("cone", coneS * XMMatrixTranslation(TLx, coneY, frontZ2), "stone");
    AddItem("cone", coneS * XMMatrixTranslation(TRx, coneY, frontZ2), "stone");

    const float diamondS = 2.5f;
    const float diamondY = postWorldH + coneWorldH + 0.35f;

    auto AddDiamondOnCone = [&](float x, float z)
        {
            AddItem("diamond", XMMatrixScaling(diamondS, diamondS * 1.6f, diamondS) * XMMatrixTranslation(x, diamondY, z), "crystal");
        };

    AddDiamondOnCone(TLx, backZ2);
    AddDiamondOnCone(TRx, backZ2);
    AddDiamondOnCone(TLx, frontZ2);
    AddDiamondOnCone(TRx, frontZ2);

    {
        const float fountainX = 0;
        const float fountainZ =  50.0f;

        const float bowl1Major = 8.0f;
        const float bowl2Major = 5.5f;
        const float bowlYScale = 1.0f;

        const float baseCylH = 5.5f;
        const float baseCylR = 3.5f;
        AddItem("cylinder", XMMatrixScaling(baseCylR, baseCylH / cylMeshH, baseCylR) * XMMatrixTranslation(fountainX, (baseCylH * 0.5f), fountainZ), "tile");

        const float colH = 7.0f;
        const float colR = 1.8f;
        AddItem("cylinder", XMMatrixScaling(colR, colH / cylMeshH, colR) * XMMatrixTranslation(fountainX, baseCylH + (colH * 0.5f), fountainZ), "tile");

        const float bowl1Y = baseCylH + colH + 0.55f;
        AddItem("torus", XMMatrixScaling(bowl1Major, bowlYScale, bowl1Major) * XMMatrixTranslation(fountainX, bowl1Y, fountainZ), "crystal");

        const float torusMinor = bowl1Major * 0.30f;
        const float sphereR = torusMinor * 0.85f;
        AddItem("sphere", XMMatrixScaling(sphereR, sphereR, sphereR) * XMMatrixTranslation(fountainX, bowl1Y, fountainZ), "tile");

        const float topColH = 1.2f;
        const float topColR = 0.35f;
        AddItem("cylinder", XMMatrixScaling(topColR, topColH / cylMeshH, topColR) * XMMatrixTranslation(fountainX, bowl1Y + 0.65f + (topColH * 0.5f), fountainZ), "tile");

        const float bowl2Y = bowl1Y + 1.55f;
        AddItem("torus", XMMatrixScaling(bowl2Major, bowlYScale, bowl2Major) * XMMatrixTranslation(fountainX, bowl2Y, fountainZ), "crystal");

        const float ring3Major = 1.0f;
        const float ring3Y = bowl2Y + 0.85f;
        AddItem("torus", XMMatrixScaling(ring3Major, bowlYScale, ring3Major) * XMMatrixTranslation(fountainX, ring3Y, fountainZ), "crystal");
    }

    const float triMeshW = 1.5f;
    const float triMeshH = 1.5f;
    const float triMeshD = 2.0f;

    const float tentW = 16.0f;
    const float tentH = 10.0f;
    const float tentD = 20.0f;

    const float sX = tentW / triMeshW;
    const float sY = tentH / triMeshH;
    const float sZ = tentD / triMeshD;

    const float tentX = castleCenterX;
    const float tentZ = zFront + 10.0f;
    const float tentY = tentH * 0.5f;


    const float groundW = 80.0f;
    const float groundD = 120.0f;
    float halfW = groundW * 0.5f;
    float halfD = groundD * 0.5f;

    float h = 2.0f;
    float s = 1.5f;
    XMMATRIX S = XMMatrixScaling(s, h, s);

    float y = h * 1.1f;
    float inset = 0.5f * s;

    AddItem("pyramid", S * XMMatrixTranslation(-(halfW - inset), y, -(halfD - inset)), "stone");
    AddItem("pyramid", S * XMMatrixTranslation(+(halfW - inset), y, -(halfD - inset)), "stone");
    AddItem("pyramid", S * XMMatrixTranslation(-(halfW - inset), y, +(halfD - inset)), "stone");
    AddItem("pyramid", S * XMMatrixTranslation(+(halfW - inset), y, +(halfD - inset)), "stone");

    const float innerEndZ = innerCenterZ - (innerDepth * 0.5f);

    const float miniPostWorldH = wallH * 1.0f;
    const float miniScaleY = miniPostWorldH / cylMeshH;
    const float miniScaleXZ = scaleXZ * 0.8f;
    XMMATRIX miniPostS = XMMatrixScaling(miniScaleXZ, miniScaleY, miniScaleXZ);

    const float miniPostY = (cylMeshH * miniScaleY) * 0.5f;
    const float miniPostWorldR = cylMeshR * miniScaleXZ;
    const float miniConeWorldR = miniPostWorldR * 1.5f;
    const float miniConeWorldH = miniPostWorldH * 1.3f;

    XMMATRIX miniConeS = XMMatrixScaling(miniConeWorldR, miniConeWorldH, miniConeWorldR);
    const float miniConeY = miniPostWorldH + (miniConeWorldH * 0.5f);

    AddItem("cylinder", miniPostS * XMMatrixTranslation(innerLeftX, miniPostY, innerEndZ), "stone");
    AddItem("cone", miniConeS * XMMatrixTranslation(innerLeftX, miniConeY, innerEndZ), "stone");
    AddItem("cylinder", miniPostS * XMMatrixTranslation(innerRightX, miniPostY, innerEndZ), "stone");
    AddItem("cone", miniConeS * XMMatrixTranslation(innerRightX, miniConeY, innerEndZ), "stone");

    {
        const float wedgeMeshW = 2.0f;
        const float wedgeMeshH = 1.0f;
        const float wedgeMeshD = 2.0f;

        const float wedgeW = 1.2f;
        const float wedgeH = 0.6f;
        const float wedgeD = 1.6f;

        const float wsX = wedgeW / wedgeMeshW;
        const float wsY = wedgeH / wedgeMeshH;
        const float wsZ = wedgeD / wedgeMeshD;

        const float wedgeX = tentX;
        const float wedgeZ = tentZ - (tentD * 0.5f) - 0.9f;
        const float wedgeY = wedgeH * 0.5f;

        XMMATRIX W = XMMatrixScaling(wsX, wsY, wsZ) *
            XMMatrixRotationY(XMConvertToRadians(45.0f)) *
            XMMatrixTranslation(wedgeX, wedgeY, wedgeZ);

        AddItem("wedge", W, "stone");
    }
    AddItem("box", XMMatrixScaling(90.0f, 0.2f, 130.0f) * XMMatrixTranslation(0.0f, -0.15f, 0.0f), "water");

    auto mazeRitem = std::make_unique<RenderItem>();
    mazeRitem->World = MathHelper::Identity4x4();
    mazeRitem->ObjCBIndex = objCBIndex++;
    mazeRitem->Geo = mGeometries["mazeGeo"].get();
    mazeRitem->Mat = mMaterials["stone"].get();
    mazeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    mazeRitem->IndexCount = mazeRitem->Geo->DrawArgs["mazeWalls"].IndexCount;
    mazeRitem->StartIndexLocation = mazeRitem->Geo->DrawArgs["mazeWalls"].StartIndexLocation;
    mazeRitem->BaseVertexLocation = mazeRitem->Geo->DrawArgs["mazeWalls"].BaseVertexLocation;
    mAllRitems.push_back(std::move(mazeRitem));


    for (auto& e : mAllRitems)
    {
        if (e->Mat && e->Mat->Name == "water")
            mTransparentRitems.push_back(e.get());
        else
            mOpaqueRitems.push_back(e.get());
    }
}

void ShapesApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(
            std::make_unique<FrameResource>(
                md3dDevice.Get(),
                1,
                (UINT)mAllRitems.size(),
                (UINT)mMaterials.size()));
    }
}

void ShapesApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

    opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    opaquePsoDesc.pRootSignature = mRootSignature.Get();

    opaquePsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
        mShaders["standardVS"]->GetBufferSize()
    };

    opaquePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
        mShaders["opaquePS"]->GetBufferSize()
    };

    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePsoDesc.SampleMask = UINT_MAX;
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaquePsoDesc.NumRenderTargets = 1;
    opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
    opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    opaquePsoDesc.DSVFormat = mDepthStencilFormat;

    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

    D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
    transparencyBlendDesc.BlendEnable = true;
    transparencyBlendDesc.LogicOpEnable = false;
    transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
    transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
    transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;

    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
        &transparentPsoDesc,
        IID_PPV_ARGS(&mPSOs["transparent"])));

}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();
    auto matCB = mCurrFrameResource->MaterialCB->Resource();

    for (size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(
            mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        texHandle.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvUavDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress =
            objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

        D3D12_GPU_VIRTUAL_ADDRESS matCBAddress =
            matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

        cmdList->SetGraphicsRootDescriptorTable(0, texHandle);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        cmdList->DrawIndexedInstanced(
            ri->IndexCount,
            1,
            ri->StartIndexLocation,
            ri->BaseVertexLocation,
            0);
    }
}
void ShapesApp::BuildTextures()
{
    auto grassTex = std::make_unique<Texture>();
    grassTex->Name = "grassTex";
    grassTex->Filename = L"Textures/grass.dds";

    auto brickTex = std::make_unique<Texture>();
    brickTex->Name = "brickTex";
    brickTex->Filename = L"Textures/bricks3.dds";

    auto iceTex = std::make_unique<Texture>();
    iceTex->Name = "iceTex";
    iceTex->Filename = L"Textures/ice.dds";

    auto waterTex = std::make_unique<Texture>();
    waterTex->Name = "waterTex";
    waterTex->Filename = L"Textures/mywatertexture.dds";

    auto tileTex = std::make_unique<Texture>();
    tileTex->Name = "tileTex";
    tileTex->Filename = L"Textures/tile.dds";

    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(), mCommandList.Get(),
        grassTex->Filename.c_str(),
        grassTex->Resource, grassTex->UploadHeap));

    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(), mCommandList.Get(),
        brickTex->Filename.c_str(),
        brickTex->Resource, brickTex->UploadHeap));

    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(), mCommandList.Get(),
        iceTex->Filename.c_str(),
        iceTex->Resource, iceTex->UploadHeap));

    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(), mCommandList.Get(),
        waterTex->Filename.c_str(),
        waterTex->Resource, waterTex->UploadHeap));

    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
        md3dDevice.Get(), mCommandList.Get(),
        tileTex->Filename.c_str(),
        tileTex->Resource, tileTex->UploadHeap));

    mTextures["grassTex"] = std::move(grassTex);
    mTextures["brickTex"] = std::move(brickTex);
    mTextures["iceTex"] = std::move(iceTex);
    mTextures["waterTex"] = std::move(waterTex);
    mTextures["tileTex"] = std::move(tileTex);
}
void ShapesApp::BuildDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 5;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
        &srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
        mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    auto grassTex = mTextures["grassTex"]->Resource;
    auto brickTex = mTextures["brickTex"]->Resource;
    auto iceTex = mTextures["iceTex"]->Resource;
    auto waterTex = mTextures["waterTex"]->Resource;
    auto tileTex = mTextures["tileTex"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    srvDesc.Format = grassTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = grassTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    srvDesc.Format = brickTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = brickTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(brickTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    srvDesc.Format = iceTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = iceTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    srvDesc.Format = waterTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = waterTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
    srvDesc.Format = tileTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);
}