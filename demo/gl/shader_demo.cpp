#include "core/core.hpp"
#include "core/debug/print.hpp"
#include "geo/matrix_transform.hpp"
#include "gl/tz_stb_image/image_reader.hpp"
#include "gl/tz_imgui/imgui_context.hpp"
#include "gl/shader.hpp"
#include "gl/shader_compiler.hpp"
#include "gl/manager.hpp"
#include "gl/buffer.hpp"
#include "gl/frame.hpp"
#include "gl/modules/ubo.hpp"
#include "gl/texture.hpp"
#include "render/device.hpp"
#include "GLFW/glfw3.h"

int main()
{
	// Minimalist Graphics Demo.
	tz::core::initialise("Topaz Shader Demo");
	{
		tz::core::IWindow& wnd = tz::core::get().window();
		wnd.register_this();
		wnd.emplace_custom_key_listener([](tz::input::KeyPressEvent e)
		{
			switch(e.key)
			{
			case GLFW_KEY_W:
				tz::debug_printf("moving forward.\n");
			break;
			case GLFW_KEY_S:
				tz::debug_printf("moving backward.\n");
			break;
			case GLFW_KEY_A:
				tz::debug_printf("moving left\n");
			break;
			case GLFW_KEY_D:
				tz::debug_printf("moving right\n");
			break;
			case GLFW_KEY_SPACE:
				tz::debug_printf("moving up\n");
			break;
			case GLFW_KEY_LEFT_SHIFT:
				tz::debug_printf("moving down\n");
			break;
			}
		});

		glClearColor(0.0f, 0.3f, 0.15f, 1.0f);
		while(!wnd.is_close_requested())
		{
			tz::core::update();
			wnd.update();
		}
	}
	tz::core::terminate();
}