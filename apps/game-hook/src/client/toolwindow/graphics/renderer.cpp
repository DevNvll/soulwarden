#include "client/toolwindow/graphics/renderer.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cwchar>

#include <imgui.h>
#include <imgui_impl_dx12.h>

#include "client/executable/image.h"
#include "client/toolwindow/client_targets.h"
#include "client/toolwindow/input/window_input.h"
#include "client/toolwindow/tool_window.h"
#include "client/toolwindow/ui/window_layout.h"
#include "core/logging/log.h"

namespace soulwarden::client::toolwindow::graphics {
namespace {

/** Command allocators of the tool window: a frame uses the next one when the queue completed its last list. */
constexpr UINT kFrameContexts = 3;
/** The game makes a swap chain with 4 buffers (live). A larger count than this limit turns the tool window off. */
constexpr UINT kMaxBackBuffers = 16;
/** Descriptors for the textures of Dear ImGui (the font atlas). */
constexpr UINT kTextureDescriptors = 64;
/** The longest wait for the command queue before a frame is skipped. */
constexpr DWORD kQueueWaitMilliseconds = 500;
/** The layout is made for this picture height. A larger picture gets larger text and controls. */
constexpr float kReferenceHeight = 720.0F;
constexpr float kLargestScale = 3.0F;

struct FrameContext {
    ID3D12CommandAllocator* allocator{};
    UINT64 fenceValue{};
};

struct Renderer {
    ID3D12Device* device{};
    ID3D12CommandQueue* queue{};
    ID3D12DescriptorHeap* targetHeap{};
    ID3D12DescriptorHeap* textureHeap{};
    ID3D12GraphicsCommandList* list{};
    ID3D12Fence* fence{};
    HANDLE fenceEvent{};
    UINT64 fenceValue{};
    FrameContext frames[kFrameContexts]{};
    UINT frameIndex{};
    UINT targetStride{};
    UINT textureStride{};
    UINT bufferCount{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    bool textureUsed[kTextureDescriptors]{};
    LARGE_INTEGER lastCounter{};
    LARGE_INTEGER frequency{};
    /** The style at the scale 1. Made at the start of the renderer. */
    ImGuiStyle* baseStyle{};
    float scale{0.0F};
};

Renderer g_renderer;
std::atomic<bool> g_ready{false};
std::atomic<HWND> g_window{nullptr};
std::atomic<bool> g_forgetInput{false};
/** A scale in percent that a test sets, or 0: the scale of the picture height. */
std::atomic<unsigned> g_forcedScale{0};

bool readable(std::uintptr_t address, std::size_t size) noexcept {
    MEMORY_BASIC_INFORMATION information{};
    if (address < 0x10000 || VirtualQuery(reinterpret_cast<const void*>(address), &information, sizeof(information)) == 0
        || information.State != MEM_COMMIT || (information.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0) {
        return false;
    }
    return address + size <= reinterpret_cast<std::uintptr_t>(information.BaseAddress) + information.RegionSize;
}

/** Reads one pointer of the client memory. @return False when the address is not readable. */
bool read_pointer(std::uintptr_t address, std::uintptr_t& value) noexcept {
    if (!readable(address, sizeof(value))) {
        return false;
    }
    value = *reinterpret_cast<const std::uintptr_t*>(address);
    return true;
}

/** @return True for a pointer to an object with a method table whose first entry is code. */
bool is_object(std::uintptr_t object) noexcept {
    std::uintptr_t table = 0;
    std::uintptr_t first = 0;
    MEMORY_BASIC_INFORMATION information{};
    if (!read_pointer(object, table) || !read_pointer(table, first)
        || VirtualQuery(reinterpret_cast<const void*>(first), &information, sizeof(information)) == 0
        || information.State != MEM_COMMIT) {
        return false;
    }
    return (information.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}

/** Writes the file name (no folder) of the module that has an address, and the offset in it. */
void describe_code(std::uintptr_t address, char* text, std::size_t size) noexcept {
    HMODULE module = nullptr;
    wchar_t path[MAX_PATH]{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address),
                           &module)
            == 0
        || GetModuleFileNameW(module, path, MAX_PATH) == 0) {
        snprintf(text, size, "%p", reinterpret_cast<void*>(address));
        return;
    }
    // Only the file name: the folder can have a name of the user.
    const wchar_t* name = path;
    for (const wchar_t* cursor = path; *cursor != L'\0'; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') {
            name = cursor + 1;
        }
    }
    snprintf(text, size, "%ls+0x%llX", name,
             static_cast<unsigned long long>(address - reinterpret_cast<std::uintptr_t>(module)));
}

void allocate_texture(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    Renderer& renderer = g_renderer;
    for (UINT index = 0; index < kTextureDescriptors; ++index) {
        if (!renderer.textureUsed[index]) {
            renderer.textureUsed[index] = true;
            *cpu = renderer.textureHeap->GetCPUDescriptorHandleForHeapStart();
            cpu->ptr += static_cast<SIZE_T>(index) * renderer.textureStride;
            *gpu = renderer.textureHeap->GetGPUDescriptorHandleForHeapStart();
            gpu->ptr += static_cast<UINT64>(index) * renderer.textureStride;
            return;
        }
    }
    *cpu = {};
    *gpu = {};
}

void free_texture(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
    Renderer& renderer = g_renderer;
    const SIZE_T start = renderer.textureHeap->GetCPUDescriptorHandleForHeapStart().ptr;
    const SIZE_T index = (cpu.ptr - start) / renderer.textureStride;
    if (index < kTextureDescriptors) {
        renderer.textureUsed[index] = false;
    }
}

void make_style(ImGuiStyle& style) {
    ImGui::StyleColorsDark(&style);
    style.WindowRounding = 6.0F;
    style.ChildRounding = 4.0F;
    style.FrameRounding = 4.0F;
    style.GrabRounding = 4.0F;
    style.TabRounding = 4.0F;
    style.ScrollbarRounding = 6.0F;
    style.WindowPadding = ImVec2(12.0F, 10.0F);
    style.FramePadding = ImVec2(7.0F, 4.0F);
    style.ItemSpacing = ImVec2(8.0F, 6.0F);
    style.CellPadding = ImVec2(6.0F, 3.0F);
    style.ScrollbarSize = 14.0F;
    style.FontSizeBase = 15.0F;
    style.Colors[ImGuiCol_WindowBg].w = 0.96F;
}

/** Sets the size of the text and of the controls for the picture height. */
void apply_scale(Renderer& renderer, float height) {
    const unsigned forced = g_forcedScale.load();
    const float scale = forced != 0 ? std::clamp(static_cast<float>(forced) / 100.0F, 1.0F, kLargestScale)
                                    : std::clamp(std::floor(height / kReferenceHeight * 4.0F + 0.5F) / 4.0F, 1.0F, kLargestScale);
    if (scale == renderer.scale) {
        return;
    }
    renderer.scale = scale;
    ImGuiStyle style = *renderer.baseStyle;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
    ImGui::GetStyle() = style;
}

/** Makes the Direct3D objects and the Dear ImGui context. @return False after a stop() with the cause. */
bool initialize(IDXGISwapChain3* chain) noexcept {
    Renderer& renderer = g_renderer;
    const auto base = reinterpret_cast<std::uintptr_t>(executable::base());
    const Fault fault = settings().fault;

    std::uintptr_t devicePointer = 0;
    ID3D12Device* device = nullptr;
    if (fault == Fault::device || !read_pointer(base + targets::kDeviceGlobal, devicePointer) || !is_object(devicePointer)
        || FAILED(reinterpret_cast<IUnknown*>(devicePointer)->QueryInterface(IID_PPV_ARGS(&device)))) {
        stop("the client has no Direct3D 12 device at its first present call");
        return false;
    }
    std::uintptr_t manager = 0;
    std::uintptr_t record = 0;
    std::uintptr_t queuePointer = 0;
    if (!read_pointer(base + targets::kQueueManagerGlobal, manager) || !read_pointer(manager + targets::kQueueRecord, record)
        || !read_pointer(record + targets::kQueueRecordQueue, queuePointer) || !is_object(queuePointer)) {
        stop("the client has no command queue at its first present call");
        return false;
    }
    // With NVIDIA Streamline this queue is a proxy object of Streamline, and the device is the
    // native device. The proxy gives a command list of the native device to the native queue.
    auto* const queue = reinterpret_cast<ID3D12CommandQueue*>(queuePointer);
    const D3D12_COMMAND_QUEUE_DESC queueDescription = queue->GetDesc();
    if (queueDescription.Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
        stop("the command queue of the client has the type %d, not the direct type", static_cast<int>(queueDescription.Type));
        return false;
    }
    DXGI_SWAP_CHAIN_DESC description{};
    if (fault == Fault::swapChain || FAILED(chain->GetDesc(&description)) || description.OutputWindow == nullptr
        || description.BufferCount == 0 || description.BufferCount > kMaxBackBuffers) {
        stop("the swap chain of the game gave no description that the tool window can use");
        return false;
    }
    queue->AddRef();
    renderer.device = device;
    renderer.queue = queue;
    renderer.format = description.BufferDesc.Format;
    renderer.bufferCount = description.BufferCount;

    D3D12_DESCRIPTOR_HEAP_DESC targets{};
    targets.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    targets.NumDescriptors = renderer.bufferCount;
    D3D12_DESCRIPTOR_HEAP_DESC textures{};
    textures.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    textures.NumDescriptors = kTextureDescriptors;
    textures.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT result = device->CreateDescriptorHeap(&targets, IID_PPV_ARGS(&renderer.targetHeap));
    if (SUCCEEDED(result)) {
        result = device->CreateDescriptorHeap(&textures, IID_PPV_ARGS(&renderer.textureHeap));
    }
    for (FrameContext& frame : renderer.frames) {
        if (SUCCEEDED(result)) {
            result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator));
        }
    }
    if (SUCCEEDED(result)) {
        result = device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, renderer.frames[0].allocator, nullptr, IID_PPV_ARGS(&renderer.list));
    }
    if (SUCCEEDED(result)) {
        result = renderer.list->Close();
    }
    if (SUCCEEDED(result)) {
        result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&renderer.fence));
    }
    if (SUCCEEDED(result)) {
        renderer.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    if (FAILED(result) || renderer.fenceEvent == nullptr) {
        stop("a Direct3D 12 object of the tool window was not made (result 0x%08lX)", static_cast<unsigned long>(result));
        return false;
    }
    renderer.targetStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    renderer.textureStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.BackendPlatformName = "soulwarden_game_window";
    io.Fonts->AddFontDefaultVector();
    renderer.baseStyle = IM_NEW(ImGuiStyle)();
    make_style(*renderer.baseStyle);
    apply_scale(renderer, static_cast<float>(description.BufferDesc.Height));

    ImGui_ImplDX12_InitInfo information;
    information.Device = device;
    information.CommandQueue = queue;
    information.NumFramesInFlight = static_cast<int>(kFrameContexts);
    information.RTVFormat = renderer.format;
    information.DSVFormat = DXGI_FORMAT_UNKNOWN;
    information.SrvDescriptorHeap = renderer.textureHeap;
    information.SrvDescriptorAllocFn = &allocate_texture;
    information.SrvDescriptorFreeFn = &free_texture;
    // The frame function of the DX12 part has only an assertion for a failure of the shader
    // compile, and this build has no assertions. Thus the hook makes the objects here.
    if (!ImGui_ImplDX12_Init(&information) || !ImGui_ImplDX12_CreateDeviceObjects()) {
        stop("the Direct3D 12 part of Dear ImGui did not start (root signature, shader compile, or pipeline state)");
        return false;
    }
    QueryPerformanceFrequency(&renderer.frequency);
    QueryPerformanceCounter(&renderer.lastCounter);

    std::uintptr_t presentTable = 0;
    std::uintptr_t presentFunction = 0;
    std::uintptr_t proxyDevice = 0;
    char presentText[96] = "(not readable)";
    if (read_pointer(reinterpret_cast<std::uintptr_t>(chain), presentTable)
        && read_pointer(presentTable + 8 * sizeof(void*), presentFunction)) {
        describe_code(presentFunction, presentText, sizeof(presentText));
    }
    (void)read_pointer(base + targets::kProxyDeviceGlobal, proxyDevice);
    const bool streamline = readable(base + targets::kStreamlineFlag, 1)
                            && *reinterpret_cast<const std::uint8_t*>(base + targets::kStreamlineFlag) != 0;
    core::log::write("tool window: renderer ready on the present thread %lu: swap chain %p (%ux%u, format %u, %u buffers, "
                     "present function %s), command queue %p, device %p, Streamline %s (its device %p), game window %p",
                     static_cast<unsigned long>(GetCurrentThreadId()),
                     static_cast<void*>(chain),
                     description.BufferDesc.Width,
                     description.BufferDesc.Height,
                     static_cast<unsigned>(description.BufferDesc.Format),
                     description.BufferCount,
                     presentText,
                     static_cast<void*>(queue),
                     static_cast<void*>(device),
                     streamline ? "on" : "off",
                     reinterpret_cast<void*>(proxyDevice),
                     static_cast<void*>(description.OutputWindow));
    g_window = description.OutputWindow;
    g_ready = true;
    return true;
}

/** One Dear ImGui frame on the back buffer that the game presents next. */
void render(IDXGISwapChain3* chain) noexcept {
    Renderer& renderer = g_renderer;
    DXGI_SWAP_CHAIN_DESC description{};
    if (FAILED(chain->GetDesc(&description)) || description.BufferDesc.Width == 0 || description.BufferDesc.Height == 0) {
        return;
    }
    if (description.OutputWindow != g_window.load()) {
        // A swap chain of a different window: the tool window is on the game window only.
        return;
    }
    if (description.BufferDesc.Format != renderer.format || description.BufferCount > renderer.bufferCount) {
        stop("the swap chain of the game changed (format %u, %u buffers; at the start: format %u, %u buffers)",
             static_cast<unsigned>(description.BufferDesc.Format),
             description.BufferCount,
             static_cast<unsigned>(renderer.format),
             renderer.bufferCount);
        return;
    }
    if (renderer.device->GetDeviceRemovedReason() != S_OK) {
        stop("the Direct3D 12 device of the game is removed");
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const auto width = static_cast<float>(description.BufferDesc.Width);
    const auto height = static_cast<float>(description.BufferDesc.Height);
    io.DisplaySize = ImVec2(width, height);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const float elapsed = static_cast<float>(now.QuadPart - renderer.lastCounter.QuadPart)
                          / static_cast<float>(renderer.frequency.QuadPart);
    renderer.lastCounter = now;
    // The window can be closed for a long time: the first frame after that is a usual frame.
    io.DeltaTime = elapsed > 0.0F && elapsed < 0.25F ? elapsed : 1.0F / 60.0F;
    if (g_forgetInput.exchange(false)) {
        io.ClearInputKeys();
        io.ClearInputMouse();
    }
    // A pointer message has a position in the client area of the window.
    RECT client{};
    float scaleX = 1.0F;
    float scaleY = 1.0F;
    if (GetClientRect(description.OutputWindow, &client) != 0 && client.right > 0 && client.bottom > 0) {
        scaleX = width / static_cast<float>(client.right);
        scaleY = height / static_cast<float>(client.bottom);
    }
    input::drain(io, scaleX, scaleY);
    // In the world the game hides the pointer of the system: Dear ImGui then draws one.
    io.MouseDrawCursor = !input::system_pointer_visible();
    apply_scale(renderer, height);

    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    ui::draw();
    ImGui::Render();
    ImDrawData* const drawData = ImGui::GetDrawData();
    if (drawData == nullptr || drawData->CmdListsCount == 0) {
        return;
    }

    const UINT bufferIndex = chain->GetCurrentBackBufferIndex();
    ID3D12Resource* buffer = nullptr;
    if (bufferIndex >= renderer.bufferCount || FAILED(chain->GetBuffer(bufferIndex, IID_PPV_ARGS(&buffer)))) {
        return;
    }
    FrameContext& frame = renderer.frames[renderer.frameIndex % kFrameContexts];
    if (renderer.fence->GetCompletedValue() < frame.fenceValue) {
        // The queue did not complete the list that used this allocator. Wait a short time,
        // and draw no tool window in this frame when the queue needs more.
        if (FAILED(renderer.fence->SetEventOnCompletion(frame.fenceValue, renderer.fenceEvent))
            || WaitForSingleObject(renderer.fenceEvent, kQueueWaitMilliseconds) != WAIT_OBJECT_0) {
            buffer->Release();
            return;
        }
    }
    if (FAILED(frame.allocator->Reset()) || FAILED(renderer.list->Reset(frame.allocator, nullptr))) {
        buffer->Release();
        stop("the command list of the tool window did not start again");
        return;
    }
    ++renderer.frameIndex;

    D3D12_CPU_DESCRIPTOR_HANDLE target = renderer.targetHeap->GetCPUDescriptorHandleForHeapStart();
    target.ptr += static_cast<SIZE_T>(bufferIndex) * renderer.targetStride;
    renderer.device->CreateRenderTargetView(buffer, nullptr, target);

    // At the present call the back buffer has the state "present".
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = buffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    renderer.list->ResourceBarrier(1, &barrier);
    renderer.list->OMSetRenderTargets(1, &target, FALSE, nullptr);
    renderer.list->SetDescriptorHeaps(1, &renderer.textureHeap);
    ImGui_ImplDX12_RenderDrawData(drawData, renderer.list);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    renderer.list->ResourceBarrier(1, &barrier);
    if (FAILED(renderer.list->Close())) {
        buffer->Release();
        stop("the command list of the tool window did not close");
        return;
    }
    ID3D12CommandList* const lists[]{renderer.list};
    renderer.queue->ExecuteCommandLists(1, lists);
    renderer.queue->Signal(renderer.fence, ++renderer.fenceValue);
    frame.fenceValue = renderer.fenceValue;
    // The swap chain owns the buffer. No reference stays after the frame.
    buffer->Release();
}

} // namespace

void on_present(IDXGISwapChain3* chain, UINT flags) noexcept {
    if (stopped() || chain == nullptr || (flags & DXGI_PRESENT_TEST) != 0) {
        return;
    }
    if (!g_ready.load() && !initialize(chain)) {
        return;
    }
    if (visible()) {
        render(chain);
    }
}

bool ready() noexcept {
    return g_ready.load();
}

HWND window() noexcept {
    return g_window.load();
}

void forget_input() noexcept {
    g_forgetInput = true;
}

void force_scale(unsigned percent) noexcept {
    g_forcedScale = percent;
}

} // namespace soulwarden::client::toolwindow::graphics
