#include <gui/render_backend.hpp>

#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_dx11.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <cstdio>
#include <cstring>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace uvc {

struct preview_tex {
	ID3D11Texture2D *tex{nullptr};
	ID3D11ShaderResourceView *srv{nullptr};
};

class d3d11_render_backend : public render_backend {
public:
	bool init(GLFWwindow *window) override {
		window_ = window;

		HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
			nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &context_);
		if (FAILED(hr)) {
			return false;
		}

		IDXGIDevice *dxgi_device = nullptr;
		hr = device_->QueryInterface(__uuidof(IDXGIDevice),
			reinterpret_cast<void **>(&dxgi_device));
		if (FAILED(hr)) {
			return false;
		}

		IDXGIAdapter *adapter = nullptr;
		hr = dxgi_device->GetAdapter(&adapter);
		dxgi_device->Release();
		if (FAILED(hr)) {
			return false;
		}

		IDXGIFactory *factory = nullptr;
		hr = adapter->GetParent(__uuidof(IDXGIFactory),
			reinterpret_cast<void **>(&factory));
		adapter->Release();
		if (FAILED(hr)) {
			return false;
		}

		HWND hwnd = glfwGetWin32Window(window_);

		DXGI_SWAP_CHAIN_DESC sc_desc{};
		sc_desc.BufferCount = 2;
		sc_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sc_desc.OutputWindow = hwnd;
		sc_desc.SampleDesc.Count = 1;
		sc_desc.Windowed = TRUE;
		sc_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

		hr = factory->CreateSwapChain(device_, &sc_desc, &swap_chain_);
		factory->Release();
		if (FAILED(hr)) {
			return false;
		}

		if (!create_render_target()) {
			return false;
		}

		ImGui_ImplGlfw_InitForOther(window_, true);
		ImGui_ImplDX11_Init(device_, context_);

		return true;
	}

	void shutdown() override {
		for (auto *pt : preview_textures_) {
			if (pt->srv) pt->srv->Release();
			if (pt->tex) pt->tex->Release();
			delete pt;
		}
		preview_textures_.clear();

		if (window_) {
			ImGui_ImplDX11_Shutdown();
			ImGui_ImplGlfw_Shutdown();
		}

		if (rtv_) {
			rtv_->Release();
			rtv_ = nullptr;
		}
		if (swap_chain_) {
			swap_chain_->Release();
			swap_chain_ = nullptr;
		}
		if (context_) {
			context_->Release();
			context_ = nullptr;
		}
		if (device_) {
			device_->Release();
			device_ = nullptr;
		}
	}

	void begin_frame() override {
		int w, h;
		glfwGetFramebufferSize(window_, &w, &h);

		if (w != last_width_ || h != last_height_) {
			if (rtv_) {
				rtv_->Release();
				rtv_ = nullptr;
			}
			swap_chain_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
			create_render_target();
			last_width_ = w;
			last_height_ = h;
		}

		ImGui_ImplGlfw_NewFrame();
		ImGui_ImplDX11_NewFrame();
		ImGui::NewFrame();
	}

	void end_frame() override {
		ImGui::Render();

		const float clear_color[4] = {0.1f, 0.1f, 0.1f, 1.0f};
		context_->OMSetRenderTargets(1, &rtv_, nullptr);
		context_->ClearRenderTargetView(rtv_, clear_color);

		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

		swap_chain_->Present(1, 0);
	}

	void *create_preview_texture(uint32_t w, uint32_t h) override {
		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = w;
		desc.Height = h;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		ID3D11Texture2D *tex = nullptr;
		HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &tex);
		if (FAILED(hr)) {
			return nullptr;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{};
		srv_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srv_desc.Texture2D.MipLevels = 1;

		ID3D11ShaderResourceView *srv = nullptr;
		hr = device_->CreateShaderResourceView(tex, &srv_desc, &srv);
		if (FAILED(hr)) {
			tex->Release();
			return nullptr;
		}

		auto *pt = new preview_tex{tex, srv};
		preview_textures_.push_back(pt);
		return static_cast<void *>(srv);
	}

	void destroy_preview_texture(void *tex) override {
		if (!tex) return;

		ID3D11ShaderResourceView *srv = static_cast<ID3D11ShaderResourceView *>(tex);
		for (auto it = preview_textures_.begin(); it != preview_textures_.end(); ++it) {
			if ((*it)->srv == srv) {
				(*it)->srv->Release();
				(*it)->tex->Release();
				delete *it;
				preview_textures_.erase(it);
				break;
			}
		}
	}

	void update_preview_from_native(void *tex, void *native_buffer, uint32_t w, uint32_t h) override {
		ID3D11ShaderResourceView *srv = static_cast<ID3D11ShaderResourceView *>(tex);
		ID3D11Texture2D *texture = nullptr;

		for (auto *pt : preview_textures_) {
			if (pt->srv == srv) {
				texture = pt->tex;
				break;
			}
		}

		if (!texture) return;

		D3D11_MAPPED_SUBRESOURCE mapped{};
		HRESULT hr = context_->Map(texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
		if (FAILED(hr)) return;

		const uint8_t *src = static_cast<const uint8_t *>(native_buffer);
		uint8_t *dst = static_cast<uint8_t *>(mapped.pData);
		uint32_t src_stride = w * 4;
		uint32_t dst_stride = mapped.RowPitch;

		for (uint32_t row = 0; row < h; ++row) {
			std::memcpy(dst + row * dst_stride, src + row * src_stride, src_stride);
		}

		context_->Unmap(texture, 0);
	}

	bool check_camera_authorization() override {
		return true;
	}

	const char *get_system_font_path() override {
		return "C:\\Windows\\Fonts\\msgothic.ttc";
	}

private:
	bool create_render_target() {
		ID3D11Texture2D *back_buffer = nullptr;
		HRESULT hr = swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D),
			reinterpret_cast<void **>(&back_buffer));
		if (FAILED(hr)) {
			return false;
		}

		hr = device_->CreateRenderTargetView(back_buffer, nullptr, &rtv_);
		back_buffer->Release();
		return SUCCEEDED(hr);
	}

	GLFWwindow *window_{nullptr};
	ID3D11Device *device_{nullptr};
	ID3D11DeviceContext *context_{nullptr};
	IDXGISwapChain *swap_chain_{nullptr};
	ID3D11RenderTargetView *rtv_{nullptr};
	int last_width_{0};
	int last_height_{0};

	std::vector<preview_tex *> preview_textures_;
};

std::unique_ptr<render_backend> create_render_backend() {
	return std::make_unique<d3d11_render_backend>();
}

} // namespace uvc
