#include "pch.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;

extern "C"
{
	HRESULT __stdcall OS_RoGetActivationFactory(HSTRING classId, GUID const& iid, void** factory) noexcept;
}

#ifdef _M_IX86
#pragma comment(linker, "/alternatename:_OS_RoGetActivationFactory@12=_RoGetActivationFactory@12")
#else
#pragma comment(linker, "/alternatename:OS_RoGetActivationFactory=RoGetActivationFactory")
#endif

bool starts_with(std::wstring_view value, std::wstring_view match) noexcept
{
	return 0 == value.compare(0, match.size(), match);
}

HRESULT __stdcall WINRT_RoGetActivationFactory(HSTRING classId, GUID const& iid, void** factory) noexcept
{
	*factory = nullptr;
	std::wstring_view name{ WindowsGetStringRawBuffer(classId, nullptr), WindowsGetStringLen(classId) };
	HMODULE library{ nullptr };

	if (starts_with(name, L"Microsoft.Graphics."))
	{
		library = LoadLibraryW(L"Microsoft.Graphics.Canvas.dll");
	}
	else
	{
		return OS_RoGetActivationFactory(classId, iid, factory);
	}

	if (!library)
	{
		return HRESULT_FROM_WIN32(GetLastError());
	}

	using DllGetActivationFactory = HRESULT __stdcall(HSTRING classId, void** factory);
	auto call = reinterpret_cast<DllGetActivationFactory*>(GetProcAddress(library, "DllGetActivationFactory"));

	if (!call)
	{
		HRESULT const hr = HRESULT_FROM_WIN32(GetLastError());
		WINRT_VERIFY(FreeLibrary(library));
		return hr;
	}

	winrt::com_ptr<winrt::Windows::Foundation::IActivationFactory> activation_factory;
	HRESULT const hr = call(classId, activation_factory.put_void());

	if (FAILED(hr))
	{
		WINRT_VERIFY(FreeLibrary(library));
		return hr;
	}

	if (iid != winrt::guid_of<winrt::Windows::Foundation::IActivationFactory>())
	{
		return activation_factory->QueryInterface(iid, factory);
	}

	*factory = activation_factory.detach();
	return S_OK;
}

using namespace winrt;
using namespace Windows::UI;
using namespace Windows::UI::Composition;
using namespace Windows::UI::Composition::Desktop;
using namespace Windows::UI::Xaml::Hosting;
using namespace Microsoft::Graphics::Canvas;
using namespace Microsoft::Graphics::Canvas::Geometry;
using namespace Windows::Foundation::Numerics;

auto CreateDispatcherQueueController()
{
    namespace abi = ABI::Windows::System;

    DispatcherQueueOptions options
    {
        sizeof(DispatcherQueueOptions),
        DQTYPE_THREAD_CURRENT,
        DQTAT_COM_STA
    };

    Windows::System::DispatcherQueueController controller{ nullptr };
    check_hresult(CreateDispatcherQueueController(options, reinterpret_cast<abi::IDispatcherQueueController**>(put_abi(controller))));
    return controller;
}

DesktopWindowTarget CreateDesktopWindowTarget(Compositor const& compositor, HWND window)
{
    namespace abi = ABI::Windows::UI::Composition::Desktop;

    auto interop = compositor.as<abi::ICompositorDesktopInterop>();
    DesktopWindowTarget target{ nullptr };
    check_hresult(interop->CreateDesktopWindowTarget(window, true, reinterpret_cast<abi::IDesktopWindowTarget**>(put_abi(target))));
    return target;
}

template <typename T>
struct DesktopWindow
{
    static T* GetThisFromHandle(HWND const window) noexcept
    {
        return reinterpret_cast<T *>(GetWindowLongPtr(window, GWLP_USERDATA));
    }

    static LRESULT __stdcall WndProc(HWND const window, UINT const message, WPARAM const wparam, LPARAM const lparam) noexcept
    {
        WINRT_ASSERT(window);

        if (WM_NCCREATE == message)
        {
            auto cs = reinterpret_cast<CREATESTRUCT *>(lparam);
            T* that = static_cast<T*>(cs->lpCreateParams);
            WINRT_ASSERT(that);
            WINRT_ASSERT(!that->m_window);
            that->m_window = window;
            SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(that));
        }
        else if (T* that = GetThisFromHandle(window))
        {
            return that->MessageHandler(message, wparam, lparam);
        }

        return DefWindowProc(window, message, wparam, lparam);
    }

    LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam) noexcept
    {
        if (WM_DESTROY == message)
        {
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProc(m_window, message, wparam, lparam);
    }

	HWND GetHandle() {
		return m_window;
	}

protected:

    using base_type = DesktopWindow<T>;
    HWND m_window = nullptr;
};

struct Window : DesktopWindow<Window>
{
    Window() noexcept
    {
        WNDCLASS wc{};
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hInstance = reinterpret_cast<HINSTANCE>(&__ImageBase);
        wc.lpszClassName = L"Windows::UI::Composition in Win32 Sample";
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WndProc;
        RegisterClass(&wc);
        WINRT_ASSERT(!m_window);

        WINRT_VERIFY(CreateWindow(wc.lpszClassName,
            L"Windows::UI::Composition in Win32 Sample", 
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 
            nullptr, nullptr, wc.hInstance, this));

        WINRT_ASSERT(m_window);
    }

    LRESULT MessageHandler(UINT const message, WPARAM const wparam, LPARAM const lparam) noexcept
    {
        // TODO: handle messages here...

        return base_type::MessageHandler(message, wparam, lparam);
    }

private:

    DesktopWindowTarget m_target{ nullptr };
};

int __stdcall wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    init_apartment(apartment_type::single_threaded);
    auto controller = CreateDispatcherQueueController();

    Window window;

	Windows::UI::Xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread(); 

	DesktopWindowXamlSource source;
	auto handle = window.GetHandle();
	auto interop = source.as<IDesktopWindowXamlSourceNative>();
	check_hresult(interop->AttachToWindow(handle));

	Windows::UI::Xaml::Media::SolidColorBrush backgroundBrush{ Windows::UI::Colors::Blue() };

	Windows::UI::Xaml::Controls::Button b;
	b.Width(100);
	b.Height(10);
	b.Background(backgroundBrush);
	//b.Content(L"HelloWorld");

	source.Content(b);

    MSG message;

    while (GetMessage(&message, nullptr, 0, 0))
    {
        DispatchMessage(&message);
    }
}
