// Read-only feasibility probe for the wallpaper-only lighting input.
// No UI control, wallpaper changes, files containing pixels, or LED writes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <psapi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <vector>

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using Clock = std::chrono::steady_clock;
static HANDLE cancelled;
static BOOL WINAPI interrupt(DWORD) { SetEvent(cancelled); return TRUE; }

static std::wstring className(HWND window) {
    wchar_t buffer[256]{};
    GetClassNameW(window, buffer, 256);
    return buffer;
}
static std::wstring processName(HWND window) {
    DWORD pid{}; GetWindowThreadProcessId(window, &pid);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t path[32768]{}; DWORD count = 32768;
    bool valid = QueryFullProcessImageNameW(process, 0, path, &count);
    CloseHandle(process);
    if (!valid) return {};
    std::wstring name(path, count);
    return name.substr(name.find_last_of(L"\\/")+1);
}
static bool renderer(HWND window) {
    const auto name = processName(window);
    return name == L"wallpaper64.exe" || name == L"wallpaper32.exe" ||
           name == L"webwallpaper32.exe" || name == L"webwallpaper64.exe" ||
           name == L"edgewallpaper64.exe";
}
static GraphicsCaptureItem captureItem(HWND window) {
    auto factory = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{nullptr};
    check_hresult(factory->CreateForWindow(window, guid_of<GraphicsCaptureItem>(), put_abi(item)));
    return item;
}
static void inventory() {
    std::set<HWND> candidates;
    EnumWindows([](HWND root, LPARAM data) -> BOOL {
        auto& found = *reinterpret_cast<std::set<HWND>*>(data);
        const auto cls = className(root);
        if (cls == L"Progman" || cls == L"WorkerW" || renderer(root)) found.insert(root);
        EnumChildWindows(root, [](HWND child, LPARAM pointer) -> BOOL {
            if (renderer(child) || className(child) == L"WorkerW")
                reinterpret_cast<std::set<HWND>*>(pointer)->insert(child);
            return TRUE;
        }, data);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&candidates));
    for (auto window : candidates) {
        RECT bounds{}; GetWindowRect(window, &bounds);
        if (bounds.right-bounds.left < 100 || bounds.bottom-bounds.top < 100) continue;
        bool icons = false;
        EnumChildWindows(window, [](HWND child, LPARAM data) -> BOOL {
            if (className(child) == L"SysListView32" && IsWindowVisible(child))
                *reinterpret_cast<bool*>(data) = true;
            return TRUE;
        }, reinterpret_cast<LPARAM>(&icons));
        HRESULT status = S_OK;
        try { auto item = captureItem(window); } catch (hresult_error const& error) { status=error.code(); }
        std::wcout << L"{\"hwnd\":" << reinterpret_cast<uintptr_t>(window)
            << L",\"class\":\"" << className(window) << L"\",\"root_class\":\""
            << className(GetAncestor(window,GA_ROOT)) << L"\",\"visible\":"
            << (IsWindowVisible(window) ? L"true" : L"false")
            << L",\"visible_icon_tree\":" << (icons ? L"true" : L"false")
            << L",\"width\":" << bounds.right-bounds.left << L",\"height\":" << bounds.bottom-bounds.top
            << L",\"hresult\":\"0x" << std::hex << static_cast<unsigned>(status) << std::dec << L"\"}" << std::endl;
    }
}

struct Readback {
    com_ptr<ID3D11Texture2D> mip, staging;
    com_ptr<ID3D11ShaderResourceView> view;
    UINT sourceWidth{}, sourceHeight{}, level{}, width{}, height{};
    D3D11_BOX crop{};
    void prepare(ID3D11Device* device, D3D11_TEXTURE2D_DESC source, RECT host, RECT monitor) {
        if (sourceWidth == source.Width && sourceHeight == source.Height) return;
        sourceWidth=source.Width; sourceHeight=source.Height;
        auto x = [&](LONG coordinate) { return static_cast<UINT>(std::clamp(
            (double)(coordinate-host.left)/(host.right-host.left)*source.Width, 0.0, (double)source.Width)); };
        auto y = [&](LONG coordinate) { return static_cast<UINT>(std::clamp(
            (double)(coordinate-host.top)/(host.bottom-host.top)*source.Height, 0.0, (double)source.Height)); };
        crop={x(monitor.left),y(monitor.top),0,x(monitor.right),y(monitor.bottom),1};
        if (crop.right<=crop.left || crop.bottom<=crop.top) throw std::runtime_error("LG monitor does not intersect capture item");
        UINT cw=crop.right-crop.left, ch=crop.bottom-crop.top;
        level=0;
        UINT support{}; check_hresult(device->CheckFormatSupport(source.Format,&support));
        if (!(support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN)) throw std::runtime_error("GPU downscale unsupported for capture format");
        while ((cw>>(level+1))>=256 && (ch>>(level+1))>=108) ++level;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=cw; desc.Height=ch; desc.MipLevels=0; desc.ArraySize=1; desc.Format=source.Format;
        desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags=D3D11_RESOURCE_MISC_GENERATE_MIPS;
        mip=nullptr; view=nullptr; staging=nullptr;
        check_hresult(device->CreateTexture2D(&desc,nullptr,mip.put()));
        check_hresult(device->CreateShaderResourceView(mip.get(),nullptr,view.put()));
        width=std::max(1u,cw>>level); height=std::max(1u,ch>>level);
        desc.Width=width; desc.Height=height; desc.MipLevels=1;
        desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        desc.BindFlags=0; desc.MiscFlags=0;
        check_hresult(device->CreateTexture2D(&desc,nullptr,staging.put()));
    }
};

static void saveBitmap(const wchar_t* path, const D3D11_MAPPED_SUBRESOURCE& image, UINT width, UINT height) {
    BITMAPFILEHEADER file{}; BITMAPINFOHEADER header{};
    file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(header);
    file.bfSize=file.bfOffBits+width*height*4;
    header.biSize=sizeof(header); header.biWidth=width; header.biHeight=-static_cast<LONG>(height);
    header.biPlanes=1; header.biBitCount=32; header.biCompression=BI_RGB;
    HANDLE output=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    check_bool(output!=INVALID_HANDLE_VALUE);
    DWORD written{};
    bool valid=WriteFile(output,&file,sizeof(file),&written,nullptr) && written==sizeof(file);
    valid=valid && WriteFile(output,&header,sizeof(header),&written,nullptr) && written==sizeof(header);
    for(UINT row=0;row<height && valid;++row)
        valid=WriteFile(output,static_cast<const char*>(image.pData)+row*image.RowPitch,width*4,&written,nullptr) && written==width*4;
    CloseHandle(output);
    check_bool(valid);
}
static double processCpu() {
    FILETIME created{},ended{},kernel{},user{};
    check_bool(GetProcessTimes(GetCurrentProcess(),&created,&ended,&kernel,&user));
    return ((static_cast<uint64_t>(kernel.dwHighDateTime)<<32)+kernel.dwLowDateTime+
            (static_cast<uint64_t>(user.dwHighDateTime)<<32)+user.dwLowDateTime)*1e-7;
}
static int probeCapture(HWND window, unsigned requested, const wchar_t* snapshot) {
    HWND foreground = GetForegroundWindow();
    RECT host{}; check_bool(GetWindowRect(window,&host));
    struct MonitorSearch { RECT rect{}; bool found=false; } monitor;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR handle,HDC,LPRECT,LPARAM pointer)->BOOL {
        auto& search=*reinterpret_cast<MonitorSearch*>(pointer);
        MONITORINFOEXW info{}; info.cbSize=sizeof(info);
        if (GetMonitorInfoW(handle,&info) && std::wstring(info.szDevice)==L"\\\\.\\DISPLAY2") {
            search.rect=info.rcMonitor; search.found=true;
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&monitor));
    if (!monitor.found) throw std::runtime_error("DISPLAY2 is absent");
    auto item=captureItem(window);
    com_ptr<ID3D11Device> device; com_ptr<ID3D11DeviceContext> context;
    check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,0,D3D11_SDK_VERSION,device.put(),nullptr,context.put()));
    auto dxgi=device.as<IDXGIDevice>();
    com_ptr<IInspectable> wrapper;
    check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(),wrapper.put()));
    auto winDevice=wrapper.as<IDirect3DDevice>();
    auto pool=Direct3D11CaptureFramePool::CreateFreeThreaded(winDevice,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,item.Size());
    auto session=pool.CreateCaptureSession(item);
    session.IsCursorCaptureEnabled(false);
    try { session.IsBorderRequired(false); } catch (hresult_error const&) { std::cerr << "Border suppression unavailable\n"; }
    HANDLE done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    Readback readback;
    std::mutex mutex;
    std::vector<double> times, readTimes, ages;
    std::set<uint64_t> hashes;
    double litMax=0;
    std::string failure;
    auto began=Clock::now();
    double cpuBegan=processCpu();
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    auto closed=item.Closed([&](auto const&,auto const&) { SetEvent(cancelled); });
    auto arrival=pool.FrameArrived([&](auto const& sender,auto const&) {
        std::lock_guard<std::mutex> lock(mutex);
        if (times.size()>=requested || !failure.empty()) return;
        try {
            auto frame=sender.TryGetNextFrame();
            if (!frame) return;
            auto workBegan=Clock::now();
            auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            com_ptr<ID3D11Texture2D> texture;
            check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),texture.put_void()));
            D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
            readback.prepare(device.get(),desc,host,monitor.rect);
            context->CopySubresourceRegion(readback.mip.get(),0,0,0,0,texture.get(),0,&readback.crop);
            context->GenerateMips(readback.view.get());
            context->CopySubresourceRegion(readback.staging.get(),0,0,0,0,readback.mip.get(),readback.level,nullptr);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check_hresult(context->Map(readback.staging.get(),0,D3D11_MAP_READ,0,&mapped));
            uint64_t hash=1469598103934665603ULL; unsigned lit=0;
            for(UINT y=0;y<readback.height;y+=2) for(UINT x=0;x<readback.width;x+=2) {
                auto p=static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch+x*4;
                lit+=(p[0]|p[1]|p[2])!=0;
                for(int c=0;c<3;++c) hash=(hash^p[c])*1099511628211ULL;
            }
            context->Unmap(readback.staging.get(),0);
            if(snapshot && times.size()+1==requested) {
                check_hresult(context->Map(readback.staging.get(),0,D3D11_MAP_READ,0,&mapped));
                saveBitmap(snapshot,mapped,readback.width,readback.height);
                context->Unmap(readback.staging.get(),0);
            }
            LARGE_INTEGER counter{}; QueryPerformanceCounter(&counter);
            ages.push_back(counter.QuadPart*1000.0/frequency.QuadPart-frame.SystemRelativeTime().count()/10000.0);
            frame.Close();
            hashes.insert(hash);
            litMax=std::max(litMax,(double)lit/(((readback.width+1)/2)*((readback.height+1)/2)));
            auto now=Clock::now();
            times.push_back(std::chrono::duration<double>(now-began).count());
            readTimes.push_back(std::chrono::duration<double,std::milli>(now-workBegan).count());
            if(times.size()>=requested) SetEvent(done);
        } catch(std::exception const& e) { failure=e.what(); SetEvent(done); }
        catch(hresult_error const& e) { failure=to_string(e.message()); SetEvent(done); }
    });
    session.StartCapture();
    std::cout << "{\"capturing\":true,\"hwnd\":" << reinterpret_cast<uintptr_t>(window) << "}" << std::endl;
    HANDLE signals[]={done,cancelled};
    WaitForMultipleObjects(2,signals,FALSE,INFINITE);
    pool.FrameArrived(arrival); item.Closed(closed);
    session.Close(); pool.Close();
    std::lock_guard<std::mutex> lock(mutex);
    CloseHandle(done);
    if(!failure.empty()) throw std::runtime_error(failure);
    if(times.size()<requested) { std::cout << "{\"cancelled\":true}" << std::endl; return 2; }
    std::vector<double> gaps;
    for(size_t i=1;i<times.size();++i) gaps.push_back((times[i]-times[i-1])*1000);
    std::sort(gaps.begin(),gaps.end()); std::sort(readTimes.begin(),readTimes.end());
    std::sort(ages.begin(),ages.end());
    SYSTEM_INFO system{}; GetSystemInfo(&system);
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
    std::cout << "{\"frames\":" << times.size() << ",\"distinct_grids\":" << hashes.size()
        << ",\"fps\":" << (times.size()-1)/(times.back()-times.front())
        << ",\"interval_median_ms\":" << gaps[gaps.size()/2]
        << ",\"interval_p95_ms\":" << gaps[std::min(gaps.size()-1,gaps.size()*95/100)]
        << ",\"interval_max_ms\":" << gaps.back()
        << ",\"gpu_downscale_readback_median_ms\":" << readTimes[readTimes.size()/2]
        << ",\"gpu_downscale_readback_max_ms\":" << readTimes.back()
        << ",\"frame_age_median_ms\":" << ages[ages.size()/2]
        << ",\"frame_age_p95_ms\":" << ages[std::min(ages.size()-1,ages.size()*95/100)]
        << ",\"cpu_whole_machine_percent\":" << (processCpu()-cpuBegan)/std::chrono::duration<double>(Clock::now()-began).count()/system.dwNumberOfProcessors*100
        << ",\"working_set_mib\":" << memory.WorkingSetSize/1048576.0
        << ",\"cpu_texture_width\":" << readback.width << ",\"cpu_texture_height\":" << readback.height
        << ",\"nonblack_max\":" << litMax
        << ",\"foreground_unchanged\":" << (foreground==GetForegroundWindow()?"true":"false") << "}" << std::endl;
    return 0;
}

int wmain(int argc,wchar_t** argv) {
    try {
        init_apartment(apartment_type::multi_threaded);
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        cancelled=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        SetConsoleCtrlHandler(interrupt,TRUE);
        int result=0;
        if(argc<2 || std::wstring(argv[1])==L"--inventory") inventory();
        else {
            HWND window=std::wstring(argv[1])==L"--progman" ? FindWindowW(L"Progman",nullptr)
                : reinterpret_cast<HWND>(_wcstoui64(argv[1],nullptr,0));
            if(!window) throw std::runtime_error("Capture target absent");
            result=probeCapture(window,argc>2?std::max(2,_wtoi(argv[2])):120,argc>3?argv[3]:nullptr);
        }
        CloseHandle(cancelled);
        return result;
    } catch(hresult_error const& error) { std::cerr << "HRESULT 0x" << std::hex << static_cast<unsigned>(error.code()) << ": " << to_string(error.message()) << std::endl; }
    catch(std::exception const& error) { std::cerr << error.what() << std::endl; }
    return 1;
}
