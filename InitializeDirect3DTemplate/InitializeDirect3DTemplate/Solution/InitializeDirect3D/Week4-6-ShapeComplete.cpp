/** @file Week4-6-ShapeComplete.cpp
 *  @brief Shape Practice Solution.
 *
 *  Place all of the scene geometry in one big vertex and index buffer.
 * Then use the DrawIndexedInstanced method to draw one object at a time ((as the
 * world matrix needs to be changed between objects)
 *
 *   Controls:
 *   Hold down '1' key to view scene in wireframe mode.
 *   Hold the left mouse button down and move the mouse to rotate.
 *   Hold the right mouse button down and move the mouse to zoom in and out.
 *
 *  @author Hooman Salamat
 */


#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
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

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void BuildDescriptorHeaps();
	void BuildConstantBufferViews();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

private:

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

	PassConstants mMainPassCB;

	UINT mPassCbvOffset = 0;

	bool mIsWireframe = false;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * XM_PI;
	float mPhi = 0.2f * XM_PI;
	float mRadius = 15.0f;

	POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
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

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildConstantBufferViews();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void ShapesApp::OnResize()
{
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void ShapesApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
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

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
	auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
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
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;
}

void ShapesApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);

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
	mMainPassCB.EyePosW = mEyePos;
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void ShapesApp::BuildDescriptorHeaps()
{
	UINT objCount = (UINT)mOpaqueRitems.size();

	// Need a CBV descriptor for each object for each frame resource,
	// +1 for the perPass CBV for each frame resource.
	UINT numDescriptors = (objCount + 1) * gNumFrameResources;

	// Save an offset to the start of the pass CBVs.  These are the last 3 descriptors.
	mPassCbvOffset = objCount * gNumFrameResources;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
		IID_PPV_ARGS(&mCbvHeap)));
}

void ShapesApp::BuildConstantBufferViews()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	UINT objCount = (UINT)mOpaqueRitems.size();

	// Need a CBV descriptor for each object for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
		for (UINT i = 0; i < objCount; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

			// Offset to the ith object constant buffer in the buffer.
			cbAddress += i * objCBByteSize;

			// Offset to the object cbv in the descriptor heap.
			int heapIndex = frameIndex * objCount + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes = objCBByteSize;

			md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Last three descriptors are the pass CBVs for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();

		// Offset to the pass cbv in the descriptor heap.
		int heapIndex = mPassCbvOffset + frameIndex;
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = cbAddress;
		cbvDesc.SizeInBytes = passCBByteSize;

		md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}
}

void ShapesApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE cbvTable0;
	cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];

	// Create root CBVs.
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
	slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void ShapesApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\VS.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\PS.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}


void ShapesApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.5f, 3.0f, 20, 20);
	GeometryGenerator::MeshData cone = geoGen.CreateCone(1.0f, 1.0f, 20, 20);
	GeometryGenerator::MeshData torus = geoGen.CreateTorus(1.0f, 24, 16);
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.5f, 2.0f, 1.5f);
	GeometryGenerator::MeshData wedge = geoGen.CreateWedge(2.0f, 1.0f, 2.0f);
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(0.8f);
	GeometryGenerator::MeshData triPrism = geoGen.CreateTriPrism(1.5f, 1.5f, 2.0f);


	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
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


	// Cache the starting index for each object in the concatenated index buffer.
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


	// Define the SubmeshGeometry that cover different
	// regions of the vertex/index buffers.

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


	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.

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
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Gold);
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
	}
	
	// Cone
	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::OrangeRed);
	}

	// Torus
	for (size_t i = 0; i < torus.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = torus.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Orange);
	}

	// Pyramid
	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::YellowGreen);
	}

	// Wedge
	for (size_t i = 0; i < wedge.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = wedge.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::MediumPurple);
	}

	// Diamond
	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DeepSkyBlue);
	}

	// Triangular Prism
	for (size_t i = 0; i < triPrism.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = triPrism.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::LightPink);
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

void ShapesApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	// PSO for opaque objects.

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

	// PSO for opaque wireframe objects.

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));
}



void ShapesApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size()));
	}
}


void ShapesApp::BuildRenderItems()
{
	UINT objCBIndex = 0;

	// ---------- GRID ----------
	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjCBIndex = objCBIndex++;
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));

	// ---------- helper ----------
	auto AddItem = [&](const std::string& key, const XMMATRIX& world)
		{
			auto r = std::make_unique<RenderItem>();
			XMStoreFloat4x4(&r->World, world);

			r->ObjCBIndex = objCBIndex++;
			r->Geo = mGeometries["shapeGeo"].get();
			r->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

			r->IndexCount = r->Geo->DrawArgs[key].IndexCount;
			r->StartIndexLocation = r->Geo->DrawArgs[key].StartIndexLocation;
			r->BaseVertexLocation = r->Geo->DrawArgs[key].BaseVertexLocation;

			mAllRitems.push_back(std::move(r));
		};

	// =====================================================
	// BASE SETTINGS (Castle centered on green)
	// =====================================================
	const float castleZ = 0.0f;   // <-- yeşil alana ortalamak için 0 yaptık

	const float uW = 12.0f; // width X (outer)
	const float uD = 8.0f;  // depth Z (outer)

	const float wallH = 4.0f;
	const float wallT = 0.25f;
	const float wallY = wallH * 0.5f;

	const float xLeft = -uW * 0.5f;
	const float xRight = +uW * 0.5f;

	const float zBack = castleZ - uD * 0.5f;
	const float zFront = castleZ + uD * 0.5f;

	// Door gap width on the FRONT side
	const float gateGapW = 4.0f;

	// =====================================================
	// OUTER WALLS (rectangle around green)
	// =====================================================

	// Back wall (full along X)
	AddItem("box",
		XMMatrixScaling(uW, wallH, wallT) *
		XMMatrixTranslation(0.0f, wallY, zBack));

	// Left wall (full along Z)
	AddItem("box",
		XMMatrixScaling(wallT, wallH, uD) *
		XMMatrixTranslation(xLeft, wallY, castleZ));

	// Right wall (full along Z)
	AddItem("box",
		XMMatrixScaling(wallT, wallH, uD) *
		XMMatrixTranslation(xRight, wallY, castleZ));

	// Front wall split into two segments (leave gap in middle)
	const float frontSegLen = (uW - gateGapW) * 0.5f;
	const float frontSegCenterOffset = (gateGapW * 0.5f) + (frontSegLen * 0.5f);

	// Front-left segment
	AddItem("box",
		XMMatrixScaling(frontSegLen, wallH, wallT) *
		XMMatrixTranslation(-frontSegCenterOffset, wallY, zFront));

	// Front-right segment
	AddItem("box",
		XMMatrixScaling(frontSegLen, wallH, wallT) *
		XMMatrixTranslation(+frontSegCenterOffset, wallY, zFront));

	// =====================================================
	// INNER WALLS (connect to the front wall segments)
	// =====================================================
	const float innerDepth = 4.0f;
	const float innerCenterZ = zFront - (wallT * 0.5f) - (innerDepth * 0.5f);

	const float innerLeftX = -gateGapW * 0.5f;
	const float innerRightX = +gateGapW * 0.5f;

	AddItem("box",
		XMMatrixScaling(wallT, wallH, innerDepth) *
		XMMatrixTranslation(innerLeftX, wallY, innerCenterZ));

	AddItem("box",
		XMMatrixScaling(wallT, wallH, innerDepth) *
		XMMatrixTranslation(innerRightX, wallY, innerCenterZ));

	// =====================================================
	// WALL TEETH (dolu-bos)
	// =====================================================
	const float toothW = 1.0f;
	const float toothH = 0.6f;
	const float toothTopY = wallH + toothH * 0.5f;

	auto AddTeethAlongX = [&](float zWall, float xMin, float xMax)
		{
			int idx = 0;
			for (float x = xMin + toothW * 0.5f; x <= xMax - toothW * 0.5f; x += toothW, ++idx)
			{
				if ((idx % 2) == 0)
				{
					AddItem("box",
						XMMatrixScaling(toothW, toothH, wallT) *
						XMMatrixTranslation(x, toothTopY, zWall));
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
					AddItem("box",
						XMMatrixScaling(wallT, toothH, toothW) *
						XMMatrixTranslation(xWall, toothTopY, z));
				}
			}
		};

	AddTeethAlongX(zBack, xLeft, xRight);
	AddTeethAlongZ(xLeft, zBack, zFront);
	AddTeethAlongZ(xRight, zBack, zFront);
	AddTeethAlongX(zFront, xLeft, -gateGapW * 0.5f);
	AddTeethAlongX(zFront, +gateGapW * 0.5f, xRight);

	// =====================================================
	// CORNER TOWERS (CYLINDER + CONE) at 4 corners
	// =====================================================
	const float cylMeshH = 3.0f;
	const float cylMeshR = 0.5f;

	const float postWorldH = wallH + 0.6f;
	const float scaleY = postWorldH / cylMeshH;

	// silindiri incelt
	const float scaleXZ = 1.15f;

	XMMATRIX postS = XMMatrixScaling(scaleXZ, scaleY, scaleXZ);
	const float postY = (cylMeshH * scaleY) * 0.5f;

	const float towerOut = (wallT * 0.5f) + (cylMeshR * scaleXZ);

	const float TLx = xLeft - towerOut;
	const float TRx = xRight + towerOut;
	const float backZ2 = zBack - towerOut;
	const float frontZ2 = zFront + towerOut;

	AddItem("cylinder", postS * XMMatrixTranslation(TLx, postY, backZ2));
	AddItem("cylinder", postS * XMMatrixTranslation(TRx, postY, backZ2));
	AddItem("cylinder", postS * XMMatrixTranslation(TLx, postY, frontZ2));
	AddItem("cylinder", postS * XMMatrixTranslation(TRx, postY, frontZ2));

	const float postWorldR = cylMeshR * scaleXZ;
	const float coneWorldR = postWorldR * 1.5f;
	const float coneWorldH = wallH * 1.8f;

	XMMATRIX coneS = XMMatrixScaling(coneWorldR, coneWorldH, coneWorldR);
	const float coneY = postWorldH + (coneWorldH * 0.5f);

	AddItem("cone", coneS * XMMatrixTranslation(TLx, coneY, backZ2));
	AddItem("cone", coneS * XMMatrixTranslation(TRx, coneY, backZ2));
	AddItem("cone", coneS * XMMatrixTranslation(TLx, coneY, frontZ2));
	AddItem("cone", coneS * XMMatrixTranslation(TRx, coneY, frontZ2));

	// Diamonds on cones
	const float diamondS = 0.55f;
	const float diamondY = postWorldH + coneWorldH + 0.35f;

	auto AddDiamondOnCone = [&](float x, float z)
		{
			AddItem("diamond",
				XMMatrixScaling(diamondS, diamondS * 1.6f, diamondS) *
				XMMatrixTranslation(x, diamondY, z));
		};

	AddDiamondOnCone(TLx, backZ2);
	AddDiamondOnCone(TRx, backZ2);
	AddDiamondOnCone(TLx, frontZ2);
	AddDiamondOnCone(TRx, frontZ2);

	// =====================================================
	// FOUNTAIN (bird bath) - attach BEHIND the castle
	// (2 torus bowls + middle cylinder + bottom cylinder)
	// =====================================================
	{
		// place fountain center behind the back wall
		const float fountainX = 0.0f;
		const float fountainZ = zBack - 3.5f;     // <-- "kaleye takılı" arkaya itiyoruz

		// Torus mesh: major=1.0 (CreateTorus(1.0f,...))
		// We'll scale major radius by S, and also keep it "bowl-like" by scaling Y a bit.
		const float bowl1Major = 2.4f;   // bottom bowl wider
		const float bowl2Major = 1.6f;   // top bowl smaller
		const float bowlYScale = 0.45f;  // squish to feel like basin

		// Bottom pedestal cylinder
		const float baseCylH = 1.6f;
		const float baseCylR = 1.1f;
		AddItem("cylinder",
			XMMatrixScaling(baseCylR, baseCylH / cylMeshH, baseCylR) *
			XMMatrixTranslation(fountainX, (baseCylH * 0.5f), fountainZ));

		// Middle column cylinder (between bowls)
		const float colH = 2.2f;
		const float colR = 0.55f;
		AddItem("cylinder",
			XMMatrixScaling(colR, colH / cylMeshH, colR) *
			XMMatrixTranslation(fountainX, baseCylH + (colH * 0.5f), fountainZ));

		// Bottom bowl (torus)
		const float bowl1Y = baseCylH + colH + 0.55f;
		AddItem("torus",
			XMMatrixScaling(bowl1Major, bowlYScale, bowl1Major) *
			XMMatrixTranslation(fountainX, bowl1Y, fountainZ));

		// Small top column
		const float topColH = 1.2f;
		const float topColR = 0.35f;
		AddItem("cylinder",
			XMMatrixScaling(topColR, topColH / cylMeshH, topColR) *
			XMMatrixTranslation(fountainX, bowl1Y + 0.65f + (topColH * 0.5f), fountainZ));

		// Top bowl (torus)
		const float bowl2Y = bowl1Y + 1.55f;
		AddItem("torus",
			XMMatrixScaling(bowl2Major, bowlYScale, bowl2Major) *
			XMMatrixTranslation(fountainX, bowl2Y, fountainZ));
	}
	// =====================================================
// 2 WEDGES (mor) - duvarlara yaslanan rampalar
// - uzunluk: dayandigi duvar ile paralel
// - biri sol-front segment duvarina, biri sag-front segment duvarina
// =====================================================

// Wedge mesh: CreateWedge(2.0f, 1.0f, 2.0f) (senin BuildShapeGeometry’de)
// Burada sadece SCALE + ROT ile rampaya benzetiyoruz.
	const float wedgeH = 1.1f;           // rampa yuksekligi
	const float wedgeThick = 1.6f;       // rampa genisligi (Z ya da X’e gore)
	const float wedgeLen = frontSegLen;  // dayandigi duvar parcasinin uzunlugu kadar

	// Wedge’i “duvara dogru yukselen” hale getirmek icin hafif donduruyoruz.
	// Eger ters durursa XMMatrixRotationZ(+/-) isaretini degistir.
	XMMATRIX wedgeTilt = XMMatrixRotationZ(+0.25f * XM_PI); // ~45 dereceye yakin (istege gore)

	// Sol wedge: front-left segment'e yaslansin
	{
		// Front-left segment’in merkezi: (-frontSegCenterOffset, zFront)
		// Wedge'i biraz iceri aliyoruz ki duvarla temas etsin.
		float wx = -frontSegCenterOffset;
		float wz = zFront - (wallT * 0.6f);  // duvarin icine/ustune yaslanma hissi
		float wy = wedgeH * 0.5f;            // zemine otursun

		// Uzunluk X boyunca (front segment X boyunca)
		XMMATRIX S = XMMatrixScaling(wedgeLen, wedgeH, wedgeThick);
		AddItem("wedge", S * wedgeTilt * XMMatrixTranslation(wx, wy, wz));
	}

	// Sag wedge: front-right segment'e yaslansin
	{
		float wx = +frontSegCenterOffset;
		float wz = zFront - (wallT * 0.6f);
		float wy = wedgeH * 0.5f;

		// Ayni rampayi simetrik yapmak icin tilt’i ters ceviriyoruz.
		XMMATRIX wedgeTiltR = XMMatrixRotationZ(-0.25f * XM_PI);

		XMMATRIX S = XMMatrixScaling(wedgeLen, wedgeH, wedgeThick);
		AddItem("wedge", S * wedgeTiltR * XMMatrixTranslation(wx, wy, wz));
	}

	// =====================================================
	// MINI TOWERS (cylinder + cone) - inner walls uclarina
	// Inner wall'lar: innerLeftX ve innerRightX'te, Z boyunca innerDepth
	// Uc (arkaya bakan): centerZ - innerDepth/2
	// =====================================================

	const float innerEndZ = innerCenterZ - (innerDepth * 0.5f); // inner duvarin arka ucu

	// mini cylinder ayarlari (kose kulelerin minik versiyonu)
	const float miniPostWorldH = wallH * 0.75f;
	const float miniScaleY = miniPostWorldH / cylMeshH;
	const float miniScaleXZ = scaleXZ * 0.65f;     // inceltilmis mini
	XMMATRIX miniPostS = XMMatrixScaling(miniScaleXZ, miniScaleY, miniScaleXZ);

	const float miniPostY = (cylMeshH * miniScaleY) * 0.5f;

	// mini cone
	const float miniPostWorldR = cylMeshR * miniScaleXZ;
	const float miniConeWorldR = miniPostWorldR * 1.5f;
	const float miniConeWorldH = miniPostWorldH * 0.9f; // daha kisa

	XMMATRIX miniConeS = XMMatrixScaling(miniConeWorldR, miniConeWorldH, miniConeWorldR);
	const float miniConeY = miniPostWorldH + (miniConeWorldH * 0.5f);

	// Sol inner wall ucu mini kule
	AddItem("cylinder", miniPostS* XMMatrixTranslation(innerLeftX, miniPostY, innerEndZ));
	AddItem("cone", miniConeS* XMMatrixTranslation(innerLeftX, miniConeY, innerEndZ));

	// Sag inner wall ucu mini kule
	AddItem("cylinder", miniPostS* XMMatrixTranslation(innerRightX, miniPostY, innerEndZ));
	AddItem("cone", miniConeS* XMMatrixTranslation(innerRightX, miniConeY, innerEndZ));


	// ---------- All opaque ----------
	for (auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}














void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	// For each render item...

	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];
		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		// Offset to the CBV in the descriptor heap for this object and for this frame resource.

		UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mOpaqueRitems.size() + ri->ObjCBIndex;

		auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());

		cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);

		cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

