#include <gui/render_backend.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstring>

namespace uvc {

class opengl_render_backend : public render_backend {
public:
	bool init(GLFWwindow *window) override {
		window_ = window;

		glfwMakeContextCurrent(window_);
		glfwSwapInterval(1);

		ImGui_ImplGlfw_InitForOpenGL(window_, true);
		ImGui_ImplOpenGL3_Init("#version 330 core");

		return true;
	}

	void shutdown() override {
		if (window_) {
			ImGui_ImplOpenGL3_Shutdown();
			ImGui_ImplGlfw_Shutdown();
		}
	}

	void begin_frame() override {
		ImGui_ImplGlfw_NewFrame();
		ImGui_ImplOpenGL3_NewFrame();
		ImGui::NewFrame();
	}

	void end_frame() override {
		ImGui::Render();

		int fb_w, fb_h;
		glfwGetFramebufferSize(window_, &fb_w, &fb_h);
		glViewport(0, 0, fb_w, fb_h);
		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
		glfwSwapBuffers(window_);
	}

	void *create_preview_texture(uint32_t w, uint32_t h) override {
		GLuint tex_id = 0;
		glGenTextures(1, &tex_id);
		glBindTexture(GL_TEXTURE_2D, tex_id);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA,
			GL_UNSIGNED_BYTE, nullptr);
		glBindTexture(GL_TEXTURE_2D, 0);
		return reinterpret_cast<void *>(static_cast<uintptr_t>(tex_id));
	}

	void destroy_preview_texture(void *tex) override {
		if (tex) {
			GLuint tex_id = static_cast<GLuint>(reinterpret_cast<uintptr_t>(tex));
			glDeleteTextures(1, &tex_id);
		}
	}

	void update_preview_from_native(void *tex, void *native_buffer,
		uint32_t w, uint32_t h) override {
		GLuint tex_id = static_cast<GLuint>(reinterpret_cast<uintptr_t>(tex));
		glBindTexture(GL_TEXTURE_2D, tex_id);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA,
			GL_UNSIGNED_BYTE, native_buffer);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	bool check_camera_authorization() override {
		return true;
	}

	const char *get_system_font_path() override {
		return "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
	}

private:
	GLFWwindow *window_{nullptr};
};

std::unique_ptr<render_backend> create_render_backend() {
	return std::make_unique<opengl_render_backend>();
}

} // namespace uvc
