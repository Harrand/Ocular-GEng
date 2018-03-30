#include "engine.hpp"
#include "data.hpp"

void tz::initialise()
{
	tz::util::log::message("Initialising Topaz...");
	SDL_Init(SDL_INIT_EVERYTHING);
	tz::util::log::message("Initialised SDL2.");
	tz::audio::initialise();
	tz::util::log::message("Initialised Topaz. Ready to receive OpenGL context...");
}

void tz::terminate()
{
	tz::util::log::message("Terminating Topaz...");
	tz::graphics::terminate();
	tz::audio::terminate();
	SDL_Quit();
	tz::util::log::message("Terminated SDL2.");
	tz::util::log::message("Terminated Topaz.");
}

Engine::Engine(Window* window, std::string properties_path, unsigned int tps): camera(Camera()), scene(), meta(properties_path), default_shader(this->meta.get_properties().get_property_path(StandardProperty::DEFAULT_SHADER)), default_gui_shader(this->meta.get_properties().get_property_path(StandardProperty::DEFAULT_GUI_SHADER)), seconds_timer(), tick_timer(), profiler(), window(window), fps(0), tps(tps), update_command_executor(), tick_command_executor(), update_due(false), default_texture(Bitmap<PixelRGBA>(std::vector<PixelRGBA>({tz::graphics::default_texture_pixel, PixelRGBA(0, 0, 0, 255), PixelRGBA(0, 0, 0, 255), tz::graphics::default_texture_pixel}), 2, 2)), default_normal_map(), default_parallax_map(), default_displacement_map()
{
	// fill all the asset buffers via tz data manager
	tz::data::Manager(this->meta.get_resources().get_path()).retrieve_all_data(this->meshes, this->textures, this->normal_maps, this->parallax_maps, this->displacement_maps);
    this->scene = Scene(this->meta.get_properties().get_property_path(StandardProperty::DEFAULT_SCENE), this->meta.get_resources().get_path(), this->meshes, this->textures, this->normal_maps, this->parallax_maps, this->displacement_maps, false);
	// move the camera to the scene's spawn point & orientation.
	this->camera.position = this->scene.spawn_point;
	this->camera.rotation = this->scene.spawn_orientation;

	// setup default uniform values.
	this->default_shader.emplace_uniform<Matrix4x4>("m", Matrix4x4());
	this->default_shader.emplace_uniform<Matrix4x4>("v", Matrix4x4());
	this->default_shader.emplace_uniform<Matrix4x4>("p", Matrix4x4());
	this->default_shader.emplace_uniform<unsigned int>("shininess", tz::graphics::default_shininess);
	this->default_shader.emplace_uniform<float>("parallax_map_scale", tz::graphics::default_parallax_map_scale);
	this->default_shader.emplace_uniform<float>("parallax_map_bias", tz::graphics::default_parallax_map_scale / 2.0f * (tz::graphics::default_parallax_map_offset - 1));
	this->default_shader.emplace_uniform<float>("displacement_factor", tz::graphics::default_displacement_factor);
	// read the properties file for any extra shaders specified (gui shader not included in this)
	for(std::string shader_path : this->meta.get_properties().get_sequence("extra_shaders"))
		this->extra_shaders.emplace_back(shader_path);
}

void Engine::update(std::size_t shader_index)
{
	if(this->seconds_timer.millis_passed(1000))
	{
		// update fps every second instead of every frame; suppresses random spikes in performance and reduces runtime overhead slightly
		this->fps = this->profiler.get_fps();
		this->profiler.reset();
		this->seconds_timer.reload();
	}
	this->window->set_render_target();
	this->profiler.begin_frame();
	
	this->seconds_timer.update();
	this->tick_timer.update();
	this->window->clear();
	this->profiler.end_frame();
	
	this->scene.render(this->camera, &(this->get_shader(shader_index)), static_cast<unsigned int>(this->window->get_width()), static_cast<unsigned int>(this->window->get_height()));
	
	for(auto command : this->update_command_executor.get_commands())
		command->operator()({});
	
	if(this->tick_timer.millis_passed(1000.0f/this->tps))
	{
		// update physics engine when the average time of a fixed 'tick' has passed
		for(auto tick_command : this->tick_command_executor.get_commands())
			tick_command->operator()({});
		this->scene.update(this->tps);
		this->tick_timer.reload();
		this->update_due = true;
	}
	else
		this->update_due = false;
	this->window->update();
	
	GLenum error;
		if((error = glGetError()) != GL_NO_ERROR)
			tz::util::log::error("OpenGL Error: ", error, "\n");
}

const TimeProfiler& Engine::get_time_profiler() const
{
	return this->profiler;
}

const EngineMeta& Engine::get_meta() const
{
	return this->meta;
}

const Window& Engine::get_window() const
{
	 return *(this->window);
}

const std::vector<std::unique_ptr<Mesh>>& Engine::get_meshes() const
{
	return this->meshes;
}

const std::vector<std::unique_ptr<Texture>>& Engine::get_textures() const
{
	return this->textures;
}

const std::vector<std::unique_ptr<NormalMap>>& Engine::get_normal_maps() const
{
	return this->normal_maps;
}

const std::vector<std::unique_ptr<ParallaxMap>>& Engine::get_parallax_maps() const
{
	return this->parallax_maps;
}

const std::vector<std::unique_ptr<DisplacementMap>>& Engine::get_displacement_maps() const
{
	return this->displacement_maps;
}

Shader& Engine::get_shader(std::size_t index)
{
	if(index > this->extra_shaders.size())
		tz::util::log::error("Could not retrieve shader index ", index, ", retrieving default instead.");
	else if(index != 0)
		return this->extra_shaders[index - 1];
	return this->default_shader;
}

unsigned int Engine::get_fps() const
{
	return this->fps;
}

unsigned int Engine::get_tps() const
{
	return this->tps;
}

const CommandExecutor& Engine::get_update_command_executor() const
{
	return this->update_command_executor;
}

const CommandExecutor& Engine::get_tick_command_executor() const
{
	return this->tick_command_executor;
}

void Engine::add_update_command(Command* cmd)
{
	this->update_command_executor.register_command(cmd);
}

void Engine::remove_update_command(Command* cmd)
{
	this->update_command_executor.deregister_command(cmd);
}

void Engine::add_tick_command(Command* cmd)
{
	this->tick_command_executor.register_command(cmd);
}

void Engine::remove_tick_command(Command* cmd)
{
	this->tick_command_executor.deregister_command(cmd);
}

bool Engine::is_update_due() const
{
	return this->update_due;
}