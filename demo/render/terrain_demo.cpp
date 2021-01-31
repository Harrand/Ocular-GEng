#include "core/core.hpp"
#include "core/debug/assert.hpp"
#include "core/matrix_transform.hpp"
#include "gl/shader.hpp"
#include "gl/shader_compiler.hpp"
#include "gl/manager.hpp"
#include "gl/mesh_loader.hpp"
#include "algo/noise.hpp"
#include "gl/buffer.hpp"
#include "gl/frame.hpp"
#include "gl/modules/ssbo.hpp"
#include "gl/texture.hpp"
#include "render/device.hpp"
#include "GLFW/glfw3.h"
#include "dui/imgui_context.hpp"
#include "gl/resource_writer.hpp"
#include "gl/modules/bindless_sampler.hpp"
#include "render/asset.hpp"
#include "render/scene.hpp"
#include <unordered_map>

const char *vtx_shader_src = R"glsl(
	#version 460
	layout (location = 0) in vec3 aPos;
	layout (location = 1) in vec2 aTexcoord;
	layout (location = 2) in vec3 aNormal;
    struct MVP
    {
        mat4 m;
        mat4 v;
        mat4 p;
    };
	#ssbo matrices
	{
		MVP mvp_element[512];
	};
    out vec3 pos;
	out vec2 texcoord;
	out vec3 normal;
    out mat4 model;
    out mat4 view;
    out mat4 proj;
	void main()
	{
        MVP cur_mvp = mvp_element[gl_DrawID];
        // position, in camera space.
        pos = ((cur_mvp.v * cur_mvp.m) * vec4(aPos, 1.0)).xyz;
		texcoord = aTexcoord;
		normal = aNormal;
        model = cur_mvp.m;
        view = cur_mvp.v;
        proj = cur_mvp.p;
	}
	)glsl";
const char* tess_ctrl_src = R"glsl(
    #version 460
    layout (vertices = 4) out;

    // outputs from vertex shader.
    in vec3 pos[];
    in vec2 texcoord[];
	in vec3 normal[];
    in mat4 model[];
    in mat4 view[];
    in mat4 proj[];

    // inputs to evaluation shader
    out vec3 pos_eval[];
    out vec2 texcoord_eval[];
	out vec3 normal_eval[];
    out mat4 model_eval[];
    out mat4 view_eval[];
    out mat4 proj_eval[];

	#ssbo scenery_block
	{
		vec4 snow_colour;
		vec4 terrain_colour;
		vec4 water_colour;
		vec4 tessellation_options;
		vec4 magic;
	};

    void main()
    {
        // Set the control points of the output patch.
        pos_eval[gl_InvocationID] = pos[gl_InvocationID];
        texcoord_eval[gl_InvocationID] = texcoord[gl_InvocationID];
		normal_eval[gl_InvocationID] = normal[gl_InvocationID];
        model_eval[gl_InvocationID] = model[gl_InvocationID];
        view_eval[gl_InvocationID] = view[gl_InvocationID];
        proj_eval[gl_InvocationID] = proj[gl_InvocationID];

        vec3 cam_pos_cs = vec3(0.0); // camera position, in camera space (duh!)
        float eye_to_vertex_dist0 = distance(cam_pos_cs, pos_eval[0]);
        float eye_to_vertex_dist1 = distance(cam_pos_cs, pos_eval[1]);
        float eye_to_vertex_dist2 = distance(cam_pos_cs, pos_eval[2]);

        // Calculate tessellation levels.
        // TODO: Change with respect to distance? The setup is all ready.
        gl_TessLevelOuter[0] = tessellation_options[0];
        gl_TessLevelOuter[1] = tessellation_options[1];
        gl_TessLevelOuter[2] = tessellation_options[2];
		gl_TessLevelOuter[3] = tessellation_options[3];
        gl_TessLevelInner[0] = gl_TessLevelOuter[3];
    }
)glsl";
const char* tess_eval_src = R"glsl(
    #version 460
	#extension GL_ARB_bindless_texture : require

    layout(triangles, equal_spacing, cw) in;

    // inputs to evaluation shader
    in vec3 pos_eval[];
    in vec2 texcoord_eval[];
	in vec3 normal_eval[];
    in mat4 model_eval[];
    in mat4 view_eval[];
    in mat4 proj_eval[];

    out vec2 texcoord;
	out vec3 position_worldspace;

	#ssbo texture_block
	{
		tz_bindless_sampler textures[2];
	};

	#ssbo scenery_block
	{
		vec4 snow_colour;
		vec4 terrain_colour;
		vec4 water_colour;
		vec4 tessellation_options;
		vec4 magic;
	};

    vec2 interpolate2D(vec2 v0, vec2 v1, vec2 v2)
    {
        return vec2(gl_TessCoord.x) * v0 + vec2(gl_TessCoord.y) * v1 + vec2(gl_TessCoord.z) * v2;
    }

    vec3 interpolate3D(vec3 v0, vec3 v1, vec3 v2)
    {
        return vec3(gl_TessCoord.x) * v0 + vec3(gl_TessCoord.y) * v1 + vec3(gl_TessCoord.z) * v2;
    }

    void main()
    {
        // Interpolate the attributes of the output vertex using the barycentric coordinates
        texcoord = interpolate2D(texcoord_eval[0], texcoord_eval[1], texcoord_eval[2]);
		vec3 normal = interpolate3D(normal_eval[0], normal_eval[1], normal_eval[2]);
		// this is already in camera space. multiply by inverse of view to get back to worldspace to pass to frag shader.
        vec3 pos = interpolate3D(pos_eval[0], pos_eval[1], pos_eval[2]);

		float displacement_amt = texture(textures[1], texcoord).r;
		// but first, we want to use our displacement map to extend the cameraspace position along the normal (in cameraspace too!)
		vec3 normal_cameraspace = (view_eval[0] * model_eval[0] * vec4(normal, 0.0)).xyz;
		// now extend the position along the normal.
		float displacement_factor = magic[0];
		vec3 displaced_position_cameraspace = pos + (normal_cameraspace * displacement_amt * displacement_factor);
		// remember this is in camera space. we only need multiply by p to get in clip space.
		vec4 result_clipspace = proj_eval[0] * vec4(displaced_position_cameraspace, 1.0);
		// now we want this position back in world space.
		position_worldspace = (inverse(view_eval[0]) * inverse(proj_eval[0]) * result_clipspace).xyz;
		// anything under 1000 coord is sea so we clamp it to sea level
		position_worldspace.y = max(position_worldspace.y, magic[2] * 0.9f);

		result_clipspace = proj_eval[0] * view_eval[0] * vec4(position_worldspace, 1.0);
		gl_Position = result_clipspace;

    }
)glsl";
const char *frg_shader_src = R"glsl(
	#version 460
	#extension GL_ARB_bindless_texture : require
	out vec4 FragColor;
	in vec2 texcoord;
	in vec3 position_worldspace;
	#ssbo texture_block
	{
		tz_bindless_sampler textures[2];
	};
	#ssbo scenery_block
	{
		vec4 snow_colour;
		vec4 terrain_colour;
		vec4 water_colour;
		vec4 tessellation_options;
		vec4 magic;
	};
	void main()
	{
		vec4 tex_colour = texture(textures[0], texcoord);
		vec4 terrained_colour = mix(tex_colour, terrain_colour, 0.5);
		FragColor = mix(terrained_colour, snow_colour, clamp(pow(position_worldspace.y / magic[1], 3), 0.0, 1.0));
		FragColor = mix(water_colour, FragColor, clamp(pow(position_worldspace.y / magic[2], magic[3]), 0.0, 1.0));
	}
	)glsl";

enum class NoiseType
{
	Rough,
	Smooth,
	Cosine
};

template<typename NoiseMapEngine>
float do_noise(tz::algo::NoiseMap<NoiseMapEngine>& noise_map, NoiseType type, float x, float z)
{
	switch(type)
	{
		case NoiseType::Rough:
		default:
			return noise_map.random(x, z);
		break;
		case NoiseType::Smooth:
			return noise_map.smooth(x, z);
		break;
		case NoiseType::Cosine:
			return noise_map.cosine(x, z);
		break;
	}
}

template<typename RandomEngineType = tz::algo::prng::Knuth>
tz::gl::Image<tz::gl::PixelRGB8> random_noise(unsigned int seed, unsigned int width, unsigned int height, NoiseType type = NoiseType::Rough)
{
	tz::algo::NoiseMap<RandomEngineType> noise{seed};
	tz::gl::Image<tz::gl::PixelRGB8> noise_map{width, height};

	for(unsigned int i = 0; i < width; i++)
	{
		for(unsigned int j = 0; j < height; j++)
		{
			float value = do_noise(noise, type, static_cast<float>(i / 4.0f), static_cast<float>(j / 4.0f)); // somewhere between 0.0-1.0
			// not a very helpful pixel colour.
			constexpr float max_byte_val_f = 255.0f;
			constexpr float half_byte_max_f = max_byte_val_f / 2.0f;
			std::byte val_byte = static_cast<std::byte>((value * half_byte_max_f) + half_byte_max_f); // between 0-byte_max
			noise_map(i, j) = {val_byte, val_byte, val_byte};
		}
	}
	return noise_map;
}

class SeparateTransformResourceWriter : public tz::gl::TransformResourceWriter
{
public:
    SeparateTransformResourceWriter(tz::mem::Block data): tz::gl::TransformResourceWriter(data){}
    bool write(tz::Vec3 pos, tz::Vec3 rot, tz::Vec3 scale, tz::Vec3 cam_pos, tz::Vec3 cam_rot, float fov, float aspect, float near, float far)
    {
        tz::Mat4 model = tz::model(pos, rot, scale);
        tz::Mat4 view = tz::view(cam_pos, cam_rot);
        tz::Mat4 proj = tz::perspective(fov, aspect, near, far);
        // Writes the mvp components separately instead of at once.
        return this->mat_writer.write(model) && this->mat_writer.write(view) && this->mat_writer.write(proj);
    }
};

namespace tz::render
{
    template<>
    class ElementWriter<tz::render::SceneElement, SeparateTransformResourceWriter>
    {
    public:
        static void write([[maybe_unused]] SeparateTransformResourceWriter& writer, [[maybe_unused]] const tz::render::SceneElement& element)
        {
            writer.write(element.transform.position, element.transform.rotation, element.transform.scale, element.camera.position, element.camera.rotation, element.camera.fov, element.camera.aspect_ratio, element.camera.near, element.camera.far);
        }
    };
}

constexpr float heightmap_size = 100.0f;

class SceneryOptionsWindow : public tz::ext::imgui::ImGuiWindow
{
public:
	constexpr static tz::Vec3 default_sky_colour{0.2f, 0.2f, 0.2f};

	SceneryOptionsWindow(tz::Vec4& snow_colour, tz::Vec4& terrain_colour, tz::Vec4& water_colour, tz::Vec4& tess_options, tz::Vec4& magic, tz::gl::Texture& heightmap_tex): tz::ext::imgui::ImGuiWindow("Scenery Options"), snow_colour(snow_colour), terrain_colour(terrain_colour), water_colour(water_colour), tess_options(tess_options), magic(magic), heightmap_tex(heightmap_tex), noise_map_seed(0)
	{
		set_sky_col(default_sky_colour);
	}

	void set_sky_col(tz::Vec3 sky_colour)
	{
		tz::core::get().window().get_frame()->set_clear_color(sky_colour[0], sky_colour[1], sky_colour[2]);
	}

	virtual void render() override
	{
		ImGui::Begin("Scenery Options", &this->visible);
		static tz::Vec3 sky_colour = default_sky_colour;
		if(ImGui::ColorEdit3("Sky Colour", sky_colour.data()))
		{
			set_sky_col(sky_colour);
		}
		ImGui::ColorEdit3("Snow Colour", this->snow_colour.data());
		ImGui::ColorEdit3("Terrain Colour", this->terrain_colour.data());
		ImGui::ColorEdit3("Water Colour", this->water_colour.data());
		ImGui::DragFloat4("Tessellation Options", this->tess_options.data(), 1.0f, 0.0f, 64.0f);
		ImGui::DragFloat("Displacement Factor (Magic)", this->magic.data(), 0.025f, 0.0f, 0.8f);
		ImGui::DragFloat("Snow Threshold (Magic)", this->magic.data() + 1, 100.0f, 1000.0f, 10000.0f);
		ImGui::DragFloat("Water Threshold (Magic)", this->magic.data() + 2, 50.0f, 500.0f, 10000.0f);
		ImGui::DragFloat("Water Exponent (Magic)", this->magic.data() + 3, 0.5f, 0.0f, 64.0f);

		ImGui::Spacing();

		ImGui::InputFloat("Noisemap Seed", &this->noise_map_seed, 1.0f, 50.0f);
		if(ImGui::Button("Regenerate Heightmap"))
		{
			// This is allowed to happen on-the-fly because underlying texture format/dimensions are the same as it was before.
			this->heightmap_tex.set_data(random_noise<tz::algo::prng::Knuth>(this->noise_map_seed, heightmap_size, heightmap_size, NoiseType::Cosine));
		}
		ImGui::Text("Heightmap:");
		this->heightmap_tex.dui_draw({heightmap_size, heightmap_size});
		ImGui::End();
	}
private:
	tz::Vec4& snow_colour;
	tz::Vec4& terrain_colour;
	tz::Vec4& water_colour;
	tz::Vec4& tess_options;
	tz::Vec4& magic;
	tz::gl::Texture& heightmap_tex;
	float noise_map_seed;
};

int main()
{
	constexpr std::size_t max_elements = 1;
	constexpr std::size_t max_textures = 2;
	// Minimalist Graphics Demo.
	tz::core::initialise("Topaz Terrain Demo");
	{
		tz::gl::Manager m;
		tz::gl::Object& o = *m;

		// Track it in imgui.
		tz::ext::imgui::track_object(&o);
		tz::core::get().enable_culling(false);

		tz::gl::p::SSBOModule* ssbo_module = nullptr;
		std::string vtx_result;
		std::string frg_result;
		std::string tess_ctrl_result;
		std::string tess_result;
		tz::gl::ShaderPreprocessor pre{vtx_shader_src};
		{
			std::size_t ssbo_module_id = pre.emplace_module<tz::gl::p::SSBOModule>(&o);
			pre.emplace_module<tz::gl::p::BindlessSamplerModule>();
			pre.preprocess();
			vtx_result = pre.result();
			pre.set_source(frg_shader_src);
			pre.preprocess();
			frg_result = pre.result();
			pre.set_source(tess_ctrl_src);
			pre.preprocess();
			tess_ctrl_result = pre.result();
			pre.set_source(tess_eval_src);
			pre.preprocess();
			tess_result = pre.result();
			ssbo_module = static_cast<tz::gl::p::SSBOModule*>(pre[ssbo_module_id]);
		}
		topaz_assert(ssbo_module->size() == 3, "UBO Module had unexpected number of buffers. Expected 3, got ", ssbo_module->size());
		std::size_t ssbo_id = ssbo_module->get_buffer_id(0);
		std::size_t tex_ssbo_id = ssbo_module->get_buffer_id(1);
		std::size_t scenery_ssbo_id = ssbo_module->get_buffer_id(2);

		tz::gl::SSBO* ssbo = o.get<tz::gl::BufferType::ShaderStorage>(ssbo_id);
		tz::gl::SSBO* tex_ssbo = o.get<tz::gl::BufferType::ShaderStorage>(tex_ssbo_id);
		tz::gl::SSBO* scenery_ssbo = o.get<tz::gl::BufferType::ShaderStorage>(scenery_ssbo_id);

		tz::gl::IndexedMesh square;
		square.vertices.push_back(tz::gl::Vertex{{{-0.5f, -0.5f, 0.0f}}, {{0.0f, 0.0f}}, {{0.0f, 0.0f, -1.0f}}, {{}}, {{}}});
		square.vertices.push_back(tz::gl::Vertex{{{0.5f, -0.5f, 0.0f}}, {{1.0f, 0.0f}}, {{0.0f, 0.0f, -1.0f}}, {{}}, {{}}});
		square.vertices.push_back(tz::gl::Vertex{{{0.5f, 0.5f, 0.0f}}, {{1.0f, 0.5f}}, {{0.0f, 0.0f, -1.0f}}, {{}}, {{}}});
		
		square.vertices.push_back(tz::gl::Vertex{{{-0.5f, -0.5f, 0.0f}}, {{0.0f, 0.0f}}, {{0.0f, 0.0f, -1.0f}}, {{}}, {{}}});
		square.vertices.push_back(tz::gl::Vertex{{{0.5f, 0.5f, 0.0f}}, {{1.0f, 0.5f}}, {{0.0f, 0.0f, -1.0f}}, {{}}, {{}}});
		square.vertices.push_back(tz::gl::Vertex{{{-0.5f, 0.5f, 0.0f}}, {{0.0f, 0.5f}}, {{0.0f, 0.0f, -1.0f}}, {{}}, {{}}});
		square.indices = {0, 1, 2, 3, 4, 5};

		tz::Vec3 cam_pos{{0.0f, 6000.0f, 0.0f}};

		tz::gl::Texture terrain_texture;
		terrain_texture.set_parameters(tz::gl::default_texture_params);
		terrain_texture.set_data(random_noise<tz::algo::prng::LinearCongruential>(9857349, 128, 128));
		terrain_texture.make_terminal();

		tz::gl::Texture heightmap;
		heightmap.set_parameters(tz::gl::default_texture_params);
		heightmap.set_data(random_noise<tz::algo::prng::Knuth>(1, heightmap_size, heightmap_size, NoiseType::Cosine));
		heightmap.make_terminal();

		tz::gl::Manager::Handle square_handle = m.add_mesh(square);

		// UBO stores mesh transform data (model + view + projection each)
		ssbo->terminal_resize(sizeof(tz::Mat4) * max_elements * 3);
		// Scene uses UBO resource data.
		tz::render::Scene<tz::render::SceneElement, SeparateTransformResourceWriter> scene{ssbo->map()};
		// Scene setup.
		// Meshes
		tz::render::AssetBuffer::Index square_mesh_idx = scene.add_mesh({m, square_handle});
		// Textures
		tex_ssbo->resize(sizeof(tz::gl::BindlessTextureHandle) * max_textures);
		{
			tz::mem::UniformPool<tz::gl::BindlessTextureHandle> tex_pool = tex_ssbo->map_uniform<tz::gl::BindlessTextureHandle>();
			tz::gl::BindlessTextureHandle terrain_texture_handle = terrain_texture.get_terminal_handle();
			tz::gl::BindlessTextureHandle heightmap_handle = heightmap.get_terminal_handle();
			tex_pool.set(0, terrain_texture_handle);
			tex_pool.set(1, heightmap_handle);
			tex_ssbo->unmap();
		}
		// Scenery options
		scenery_ssbo->terminal_resize(sizeof(tz::Vec4) * 5);
		tz::mem::UniformPool<tz::Vec4> scenery_pool = scenery_ssbo->map_uniform<tz::Vec4>();
		{
			constexpr tz::Vec4 default_snow_colour{0.27f, 0.0f, 0.0f, 1.0f};
			constexpr tz::Vec4 default_terrain_colour{0.0f, 0.0f, 0.0f, 1.0f};
			constexpr tz::Vec4 default_water_colour{1.0f, 0.47f, 0.0f, 1.0f};
			constexpr tz::Vec4 default_tess_options{64.0f, 64.0f, 64.0f, 64.0f};
			constexpr tz::Vec4 default_magic{0.625f, 8900.0f, 4850.0f, 64.0f};

			scenery_pool.set(0, default_snow_colour);
			scenery_pool.set(1, default_terrain_colour);
			scenery_pool.set(2, default_water_colour);
			scenery_pool.set(3, default_tess_options);
			scenery_pool.set(4, default_magic);
		}

		tz::ext::imgui::emplace_window<SceneryOptionsWindow>(scenery_pool[0], scenery_pool[1], scenery_pool[2], scenery_pool[3], scenery_pool[4], heightmap);

		tz::gl::ShaderCompiler cpl;
		tz::gl::ShaderProgram prg;
		tz::gl::Shader* vs = prg.emplace(tz::gl::ShaderType::Vertex, vtx_result);
        tz::gl::Shader* tcs = prg.emplace(tz::gl::ShaderType::TessellationControl, tess_ctrl_result);
        tz::gl::Shader* tes = prg.emplace(tz::gl::ShaderType::TessellationEvaluation, tess_result);
		tz::gl::Shader* fs = prg.emplace(tz::gl::ShaderType::Fragment, frg_result);

		auto cpl_diag = cpl.compile(*vs);
		topaz_assert(cpl_diag.successful(), "Shader Vtx Compilation Fail: ", cpl_diag.get_info_log());
        cpl_diag = cpl.compile(*tcs);
		topaz_assert(cpl_diag.successful(), "Shader Tcs Compilation Fail: ", cpl_diag.get_info_log());
        cpl_diag = cpl.compile(*tes);
		topaz_assert(cpl_diag.successful(), "Shader Tes Compilation Fail: ", cpl_diag.get_info_log());
		cpl_diag = cpl.compile(*fs);
		topaz_assert(cpl_diag.successful(), "Shader Frg Compilation Fail: ", cpl_diag.get_info_log());

		auto lnk_diag = cpl.link(prg);
		topaz_assert(lnk_diag.successful(), "Shader Linkage Fail: ", lnk_diag.get_info_log());

		decltype(scene)::Handle element_handle;
		for(std::size_t i = 0; i < max_elements; i++)
		{
			tz::render::SceneElement ele{square_mesh_idx};
			ele.transform.position = tz::Vec3{1.0f, 1.0f, 1.0f};
			ele.transform.rotation = tz::Vec3{tz::pi / 2.0f, tz::pi / 2.0f, 0.0f};
			ele.transform.scale = tz::Vec3{10000.0f, 10000.0f, 10000.0f};
			ele.camera.fov = 1.57f;
			ele.camera.aspect_ratio = 1920.0f/1080.0f;
			ele.camera.near = 0.1f;
			ele.camera.far = 20000.0f;
			element_handle = scene.add(ele);
		}

		bool mouse_down = false;
		tz::core::IWindow& wnd = tz::core::get().window();
		wnd.emplace_custom_mouse_listener(
			[element_handle, &scene, &mouse_down](tz::input::MouseUpdateEvent mue)
			{
				static tz::Vec2 old_mouse{0.0f, 0.0f};
				tz::Vec2 new_mouse{static_cast<float>(mue.xpos), static_cast<float>(mue.ypos)};
				if(mouse_down)
				{
					tz::Vec2 mdelta = old_mouse - new_mouse;
					scene.get(element_handle).camera.rotation += tz::Vec3{0.03f * mdelta[1], 0.03f * mdelta[0], 0.0f};
				}
				old_mouse = new_mouse;
			}
			,
			[&mouse_down](tz::input::MouseClickEvent mce)
			{
				if(mce.button == GLFW_MOUSE_BUTTON_RIGHT)
				{
					if(mce.action == GLFW_PRESS)
					{
						mouse_down = true;
					}
					else if(mce.action == GLFW_RELEASE)
					{
						mouse_down = false;
					}
				}
			}
		);

		wnd.emplace_custom_key_listener([element_handle, &scene, &cam_pos](tz::input::KeyPressEvent e)
		{
			const tz::gl::CameraData& cam = scene.get(element_handle).camera;
			constexpr float multiplier = 20.0f;
			switch(e.key)
			{
			case GLFW_KEY_W:
				cam_pos += cam.get_forward() * multiplier;
			break;
			case GLFW_KEY_S:
				cam_pos += cam.get_backward() * multiplier;
			break;
			case GLFW_KEY_A:
				cam_pos += cam.get_left() * multiplier;
			break;
			case GLFW_KEY_D:
				cam_pos += cam.get_right() * multiplier;
			break;
			case GLFW_KEY_SPACE:
				cam_pos += cam.get_up() * multiplier;
			break;
			case GLFW_KEY_LEFT_SHIFT:
				cam_pos += cam.get_up() * multiplier;
			break;
			}
		});

		tz::render::Device dev{wnd.get_frame(), &prg, &o};
		dev.add_resource_buffer(ssbo);
		dev.add_resource_buffer(tex_ssbo);
		dev.add_resource_buffer(scenery_ssbo);
		dev.set_handle(m.get_indices());
        dev.set_is_patches(true);

		while(!wnd.is_close_requested())
		{

			dev.clear();
			o.bind();
			for(std::size_t i = 0; i < max_elements; i++)
			{
				scene.get(i).camera.position = cam_pos;
			}
			scene.configure(dev);
			dev.render();
			tz::core::update();
			wnd.update();
		}
	}
	tz::core::terminate();
}