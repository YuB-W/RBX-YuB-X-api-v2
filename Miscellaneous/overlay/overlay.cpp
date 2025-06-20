#define _WINSOCKAPI_   // Stops <Windows.h> from including <winsock.h>
#include <winsock2.h>
#include <ws2tcpip.h>  // Optional, if you need extra functions like inet_pton

#include <Windows.h>
#include <fstream>
#include <string>
#include <Environment/Environment.hpp>

#define IMGUI_DEFINE_MATH_OPERATORS
#define _CRT_SECURE_NO_WARNINGS
#include "overlay.hpp"
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx11.h"
#include "imgui/imgui_internal.h"
#include <dwmapi.h>
#include <filesystem>
#include <vector>
#include <string>
#include <d3d11.h>
#include <functional>

#include <random>
#include <thread>
#include "zstd/zstd.h"
#include "Luau/Compiler.h"
#include "Luau/BytecodeBuilder.h"
#include "Luau/BytecodeUtils.h"
#include "Execution/Execution.hpp"

ID3D11Device* gui::overlay::d3d11_device = nullptr;

ID3D11DeviceContext* gui::overlay::d3d11_device_context = nullptr;

IDXGISwapChain* gui::overlay::dxgi_swap_chain = nullptr;

ID3D11RenderTargetView* gui::overlay::d3d11_render_target_view = nullptr;

namespace cuminaniggaass {
	class bytecode_encoder_t : public Luau::BytecodeEncoder {
		inline void encode(uint32_t* data, size_t count) override {
			for (auto i = 0u; i < count;) {
				auto& opcode = *reinterpret_cast<uint8_t*>(data + i);
				i += Luau::getOpLength(LuauOpcode(opcode));

				opcode *= 227;
			}
		}
	};

	Luau::CompileOptions compile_options;

	std::string compile(std::string source) {
		if (compile_options.debugLevel != 2) {
			compile_options.debugLevel = 2;
			compile_options.optimizationLevel = 2;
		}

		static auto encoder = bytecode_encoder_t();

		std::string bytecode = Luau::compile(
			source,
			{},
			{}, &encoder
		);

		return bytecode;
	}


	std::vector<char> compress_jest(std::string bytecode, size_t& byte_size) {
		const auto data_size = bytecode.size();
		const auto max_size = ZSTD_compressBound(data_size);
		auto buffer = std::vector<char>(max_size + 8);

		strcpy_s(&buffer[0], buffer.capacity(), "RSB1");
		memcpy_s(&buffer[4], buffer.capacity(), &data_size, sizeof(data_size));

		const auto compressed_size = ZSTD_compress(&buffer[8], max_size, bytecode.data(), data_size, ZSTD_maxCLevel());
		if (ZSTD_isError(compressed_size))
			throw std::runtime_error("Failed to compress the bytecode.");

		const auto size = compressed_size + 8;
		const auto key = XXH32(buffer.data(), size, 42u);
		const auto bytes = reinterpret_cast<const uint8_t*>(&key);

		for (auto i = 0u; i < size; ++i)
			buffer[i] ^= bytes[i % 4] + i * 41u;

		byte_size = size;

		return buffer;
	}
}

bool gui::overlay::init = false;
static bool is_dragging = false;
static ImVec2 drag_offset;

void HandleDragging(ImGuiWindow* window) {
	if (ImGui::IsMouseClicked(0)) {
		if (ImGui::IsItemHovered() && !ImGui::IsItemActive() && !is_dragging) {
			is_dragging = true;
			drag_offset = ImVec2(ImGui::GetMousePos().x - window->Pos.x, ImGui::GetMousePos().y - window->Pos.y);
		}
	}

	if (is_dragging) {
		if (ImGui::IsMouseDown(0)) {
			ImVec2 new_pos = ImVec2(ImGui::GetMousePos().x - drag_offset.x, ImGui::GetMousePos().y - drag_offset.y);
			window->Pos = new_pos;
		}
		else {
			is_dragging = false;
		}
	}
}

int CountLines(const std::string& text) {
	int lines = 1;
	for (char c : text) {
		if (c == '\n') {
			lines++;
		}
	}
	return lines;
}

bool syntaxhighlighting = false;

void gui::overlay::render()
{

	ImGui_ImplWin32_EnableDpiAwareness();

	WNDCLASSEX wc;
	wc.cbClsExtra = NULL;
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.cbWndExtra = NULL;
	wc.hbrBackground = (HBRUSH)CreateSolidBrush(RGB(0, 0, 0));
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
	wc.hInstance = GetModuleHandle(nullptr);
	wc.lpfnWndProc = window_proc;
	wc.lpszClassName = TEXT("Charm");
	wc.lpszMenuName = nullptr;
	wc.style = CS_VREDRAW | CS_HREDRAW;

	RegisterClassEx(&wc);
	const HWND hw = CreateWindowEx(WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE, wc.lpszClassName, TEXT("gex"),
		WS_POPUP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), nullptr, nullptr, wc.hInstance, nullptr);

	SetLayeredWindowAttributes(hw, 0, 255, LWA_ALPHA);
	const MARGINS margin = { -1 };
	DwmExtendFrameIntoClientArea(hw, &margin);

	if (!create_device_d3d(hw))
	{
		cleanup_device_d3d();
		UnregisterClass(wc.lpszClassName, wc.hInstance);
		return;
	}

	ShowWindow(hw, SW_SHOW);
	UpdateWindow(hw);

	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui::GetIO().IniFilename = nullptr;

	ImGui_ImplWin32_Init(hw);
	ImGui_ImplDX11_Init(d3d11_device, d3d11_device_context);

	ImGuiIO& io = ImGui::GetIO(); (void)io;

	io.Fonts->Build();


	const ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	init = true;

	bool draw = false;
	bool done = false;

	bool check = false;

	std::vector<std::string> consoleLog; // dont delete this!!

	while (!done)
	{
		MSG msg;
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
			{
				done = true;
			}
		}

		if (done)
			break;

		move_window(hw);

		if (check == true) {
			if ((GetAsyncKeyState(VK_F10) & 1))
				draw = !draw;
			if ((GetAsyncKeyState(VK_END) & 1))
				draw = !draw;
			check = !check;
		}
		else {
			check = !check;
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		{
			static char texte[9004515] = "";




			if (GetForegroundWindow() == FindWindowA(0, "Roblox") || GetForegroundWindow() == hw)
			{
				ImGui::Begin(("overlay"), nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration);
				{
					ImGui::End();
				}

				if (draw)
				{

					ImGuiStyle& style = ImGui::GetStyle();

					style.Alpha = 1.0f;
					style.DisabledAlpha = 1.0f;
					style.WindowPadding = ImVec2(12.0f, 12.0f);
					style.WindowRounding = 6.0f;
					style.WindowBorderSize = 0.0f;
					style.WindowMinSize = ImVec2(20.0f, 20.0f);
					style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
					style.WindowMenuButtonPosition = ImGuiDir_Right;
					style.ChildRounding = 3.0f;
					style.ChildBorderSize = 1.0f;
					style.PopupRounding = 0.0f;
					style.PopupBorderSize = 1.0f;
					style.FramePadding = ImVec2(20.0f, 3.400000095367432f);
					style.FrameRounding = 4.0f;
					style.FrameBorderSize = 0.0f;
					style.ItemSpacing = ImVec2(4.300000190734863f, 5.5f);
					style.ItemInnerSpacing = ImVec2(7.099999904632568f, 1.799999952316284f);
					style.CellPadding = ImVec2(12.10000038146973f, 9.199999809265137f);
					style.IndentSpacing = 0.0f;
					style.ColumnsMinSpacing = 4.900000095367432f;
					style.ScrollbarSize = 11.60000038146973f;
					style.ScrollbarRounding = 10.0f;
					style.GrabMinSize = 3.700000047683716f;
					style.GrabRounding = 20.0f;
					style.TabRounding = 0.0f;
					style.TabBorderSize = 0.0f;
					style.TabMinWidthForCloseButton = 0.0f;
					style.ColorButtonPosition = ImGuiDir_Right;
					style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
					style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

					style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
					style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.2745098173618317f, 0.3176470696926117f, 0.4509803950786591f, 1.0f);
					style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
					style.Colors[ImGuiCol_ChildBg] = ImVec4(0.09250493347644806f, 0.100297249853611f, 0.1158798336982727f, 1.0f);
					style.Colors[ImGuiCol_PopupBg] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
					style.Colors[ImGuiCol_Border] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
					style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
					style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1120669096708298f, 0.1262156516313553f, 0.1545064449310303f, 1.0f);
					style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
					style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
					style.Colors[ImGuiCol_TitleBg] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
					style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
					style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
					style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.09803921729326248f, 0.105882354080677f, 0.1215686276555061f, 1.0f);
					style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
					style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
					style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.1568627506494522f, 0.168627455830574f, 0.1921568661928177f, 1.0f);
					style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
					style.Colors[ImGuiCol_CheckMark] = ImVec4(0.9725490212440491f, 1.0f, 0.4980392158031464f, 1.0f);
					style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.971993625164032f, 1.0f, 0.4980392456054688f, 1.0f);
					style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 0.7953379154205322f, 0.4980392456054688f, 1.0f);
					style.Colors[ImGuiCol_Button] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
					style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.1821731775999069f, 0.1897992044687271f, 0.1974248886108398f, 1.0f);
					style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.1545050293207169f, 0.1545048952102661f, 0.1545064449310303f, 1.0f);
					style.Colors[ImGuiCol_Header] = ImVec4(0.1414651423692703f, 0.1629818230867386f, 0.2060086131095886f, 1.0f);
					style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.1072951927781105f, 0.107295036315918f, 0.1072961091995239f, 1.0f);
					style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
					style.Colors[ImGuiCol_Separator] = ImVec4(0.1293079704046249f, 0.1479243338108063f, 0.1931330561637878f, 1.0f);
					style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.1568627506494522f, 0.1843137294054031f, 0.250980406999588f, 1.0f);
					style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.1568627506494522f, 0.1843137294054031f, 0.250980406999588f, 1.0f);
					style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.1459212601184845f, 0.1459220051765442f, 0.1459227204322815f, 1.0f);
					style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.9725490212440491f, 1.0f, 0.4980392158031464f, 1.0f);
					style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.999999463558197f, 1.0f, 0.9999899864196777f, 1.0f);
					style.Colors[ImGuiCol_Tab] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
					style.Colors[ImGuiCol_TabHovered] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
					style.Colors[ImGuiCol_TabActive] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
					style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.0784313753247261f, 0.08627451211214066f, 0.1019607856869698f, 1.0f);
					style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1249424293637276f, 0.2735691666603088f, 0.5708154439926147f, 1.0f);
					style.Colors[ImGuiCol_PlotLines] = ImVec4(0.5215686559677124f, 0.6000000238418579f, 0.7019608020782471f, 1.0f);
					style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.03921568766236305f, 0.9803921580314636f, 0.9803921580314636f, 1.0f);
					style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.8841201663017273f, 0.7941429018974304f, 0.5615870356559753f, 1.0f);
					style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.9570815563201904f, 0.9570719599723816f, 0.9570761322975159f, 1.0f);
					style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
					style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.0470588244497776f, 0.05490196123719215f, 0.07058823853731155f, 1.0f);
					style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
					style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.1176470592617989f, 0.1333333402872086f, 0.1490196138620377f, 1.0f);
					style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.09803921729326248f, 0.105882354080677f, 0.1215686276555061f, 1.0f);
					style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.9356134533882141f, 0.9356129765510559f, 0.9356223344802856f, 1.0f);
					style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.4980392158031464f, 0.5137255191802979f, 1.0f, 1.0f);
					style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.266094446182251f, 0.2890366911888123f, 1.0f, 1.0f);
					style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.4980392158031464f, 0.5137255191802979f, 1.0f, 1.0f);
					style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.196078434586525f, 0.1764705926179886f, 0.5450980663299561f, 0.501960813999176f);
					style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.196078434586525f, 0.1764705926179886f, 0.5450980663299561f, 0.501960813999176f);



					ImGui::SetNextWindowSize(ImVec2(670, 450));

					ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse;

					ImGui::Begin("Vexure | Internal", nullptr, window_flags);

					HandleDragging(ImGui::GetCurrentWindow());

					// Add a tab bar for switching between the Editor and Console
					if (ImGui::BeginTabBar("Tabs"))
					{
						if (ImGui::BeginTabItem("Editor"))
						{

							if (ImGui::Button("Execute")) {

								Execution->Execute(texte); // Assuming the function works with the input string

								std::string text;
							}


							ImGui::InputTextMultiline("##text", texte, sizeof(texte),
								ImVec2(-1, 200),
								ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CtrlEnterForNewLine
							);


							static std::unordered_map<std::string, std::string> scripts = {
			{"Infinity yield", "loadstring(game:HttpGet('https://raw.githubusercontent.com/EdgeIY/infiniteyield/master/source'))()"},
			{"UNC", "loadstring(game:HttpGet('https://raw.githubusercontent.com/unified-naming-convention/NamingStandard/refs/heads/main/UNCCheckEnv.lua'))()"},
			{"sUNC", "loadstring(game:HttpGet(\"https://gitlab.com/sens3/nebunu/-/raw/main/HummingBird8's_sUNC_yes_i_moved_to_gitlab_because_my_github_acc_got_brickedd/sUNCm0m3n7.lua\"))()"},
			{"Simple spy", "loadstring(game:HttpGet('https://raw.githubusercontent.com/exxtremestuffs/SimpleSpySource/master/SimpleSpy.lua'))()"},
			{"Hydroxide", "local owner = \"Upbolt\"\nlocal branch = \"revision\"\nlocal function webImport(file)\n    return loadstring(game:HttpGet((\"https://raw.githubusercontent.com/%s/Hydroxide/%s/%s.lua\"):format(owner, branch, file)), file .. '.lua')()\nend\nwebImport(\"init\")\nwebImport(\"ui/main\")"}
							};

							ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 100);

							if (ImGui::Button("Scripts"))


								ImGui::OpenPopup("ScriptsMenu");
							if (ImGui::BeginPopup("ScriptsMenu")) {
								for (const auto& [name, script] : scripts) {
									if (ImGui::MenuItem(name.c_str())) {
										// Assuming 'texte' is a char array and 'script' is a std::string or const char*
										strcpy(texte, script.c_str());  // Copy 'script' into 'texte' buffer
									}
								}
								ImGui::EndPopup();
							}


							ImGui::EndTabItem();
						}

						if (ImGui::BeginTabItem("Console"))
						{
							ImGui::Text("Console Output");
							ImGui::Text("Logs will appear here...");

							ImGui::EndTabItem();
						}

						ImGui::EndTabBar();
					}



					ImGui::PopStyleColor();

					ImGui::End();
				}
			}

			SetWindowDisplayAffinity(hw, WDA_NONE);

			if (draw)
			{
				SetWindowLong(hw, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW);
			}
			else
			{
				SetWindowLong(hw, GWL_EXSTYLE, WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW);
			}

			ImGui::EndFrame();
			ImGui::Render();

			const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
			d3d11_device_context->OMSetRenderTargets(1, &d3d11_render_target_view, nullptr);
			d3d11_device_context->ClearRenderTargetView(d3d11_render_target_view, clear_color_with_alpha);
			ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

			dxgi_swap_chain->Present(1, 0);
		}
	}

	init = false;

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	cleanup_device_d3d();
	DestroyWindow(hw);
	UnregisterClass(wc.lpszClassName, wc.hInstance);
}

bool gui::overlay::fullsc(HWND windowHandle)
{
	MONITORINFO monitorInfo = { sizeof(MONITORINFO) };
	if (GetMonitorInfo(MonitorFromWindow(windowHandle, MONITOR_DEFAULTTOPRIMARY), &monitorInfo))
	{
		RECT windowRect;
		if (GetWindowRect(windowHandle, &windowRect))
		{
			return windowRect.left == monitorInfo.rcMonitor.left
				&& windowRect.right == monitorInfo.rcMonitor.right
				&& windowRect.top == monitorInfo.rcMonitor.top
				&& windowRect.bottom == monitorInfo.rcMonitor.bottom;
		}
	}
}

void gui::overlay::move_window(HWND hw)
{
	HWND target = FindWindowA(0, ("Roblox"));
	HWND foregroundWindow = GetForegroundWindow();

	if (target != foregroundWindow && hw != foregroundWindow)
	{
		MoveWindow(hw, 0, 0, 0, 0, true);
	}
	else
	{
		RECT rect;
		GetWindowRect(target, &rect);

		int rsize_x = rect.right - rect.left;
		int rsize_y = rect.bottom - rect.top;

		if (fullsc(target))
		{
			rsize_x += 16;
			rsize_y -= 24;

			MoveWindow(hw, rect.left, rect.top, rsize_x, rsize_y, TRUE);
		}
		else
		{
			rsize_y -= 63;
			rect.left += 8;
			rect.top += 31;
		}

		MoveWindow(hw, rect.left, rect.top, rsize_x, rsize_y, TRUE);
	}
}

bool gui::overlay::create_device_d3d(HWND hw)
{
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hw;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	const UINT create_device_flags = 0;
	D3D_FEATURE_LEVEL d3d_feature_level;
	const D3D_FEATURE_LEVEL feature_level_arr[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
	HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, create_device_flags, feature_level_arr, 2, D3D11_SDK_VERSION, &sd, &dxgi_swap_chain, &d3d11_device, &d3d_feature_level, &d3d11_device_context);
	if (res == DXGI_ERROR_UNSUPPORTED)
		res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, create_device_flags, feature_level_arr, 2, D3D11_SDK_VERSION, &sd, &dxgi_swap_chain, &d3d11_device, &d3d_feature_level, &d3d11_device_context);
	if (res != S_OK)
		return false;

	create_render_target();
	return true;
}

void gui::overlay::cleanup_device_d3d()
{
	cleanup_render_target();

	if (dxgi_swap_chain)
	{
		dxgi_swap_chain->Release();
		dxgi_swap_chain = nullptr;
	}

	if (d3d11_device_context)
	{
		d3d11_device_context->Release();
		d3d11_device_context = nullptr;
	}

	if (d3d11_device)
	{
		d3d11_device->Release();
		d3d11_device = nullptr;
	}
}

void gui::overlay::create_render_target()
{
	ID3D11Texture2D* d3d11_back_buffer;
	dxgi_swap_chain->GetBuffer(0, IID_PPV_ARGS(&d3d11_back_buffer));
	if (d3d11_back_buffer != nullptr)
	{
		d3d11_device->CreateRenderTargetView(d3d11_back_buffer, nullptr, &d3d11_render_target_view);
		d3d11_back_buffer->Release();
	}
}

void gui::overlay::cleanup_render_target()
{
	if (d3d11_render_target_view)
	{
		d3d11_render_target_view->Release();
		d3d11_render_target_view = nullptr;
	}
}

LRESULT __stdcall gui::overlay::window_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
	case WM_SIZE:
		if (d3d11_device != nullptr && wParam != SIZE_MINIMIZED)
		{
			cleanup_render_target();
			dxgi_swap_chain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
			create_render_target();
		}
		return 0;

	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU)
			return 0;
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		break;
	}
	return DefWindowProc(hWnd, msg, wParam, lParam);
}
