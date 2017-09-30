#ifndef OBJECT_HPP
#define OBJECT_HPP
#include "mesh.hpp"
#include "shader.hpp"
#include "texture.hpp"
#include "camera.hpp"
#include <initializer_list>

class Object
{
public:
	Object(std::string mesh_link, std::vector<std::pair<std::string, Texture::TextureType>> textures, Vector3F pos, Vector3F rot, Vector3F scale, unsigned int shininess = 5, float parallax_map_scale = 0.04f, float parallax_map_offset = -0.5f, float displacement_factor = 0.25f);
	Object(const Object& copy) = default;
	Object(Object&& move) = default;
	~Object() = default;
	Object& operator=(const Object& rhs) = default;
	
	const Vector3F& getPosition() const;
	const Vector3F& getRotation() const;
	const Vector3F& getScale() const;
	unsigned int getShininess() const;
	float getParallaxMapScale() const;
	float getParallaxMapOffset() const;
	float getDisplacementFactor() const;
	Vector3F& getPositionR();
	Vector3F& getRotationR();
	Vector3F& getScaleR();
	unsigned int& getShininessR();
	float& getParallaxMapScaleR();
	float& getParallaxMapOffsetR();
	float& getDisplacementFactorR();
	const std::string& getMeshLink() const;
	const std::vector<std::pair<std::string, Texture::TextureType>> getTextures() const;
	virtual void render(Mesh* mesh, Texture* tex, NormalMap* nm, ParallaxMap* pm, DisplacementMap* dm, const Camera& cam, const Shader& shad, float width, float height) const;
protected:
	Vector3F pos, rot, scale;
	unsigned int shininess;
	float parallax_map_scale, parallax_map_offset, displacement_factor;
	std::string mesh_link;
	std::vector<std::pair<std::string, Texture::TextureType>> textures;
};

class Skybox
{
public:
	Skybox(std::string cube_mesh_link, CubeMap& cm);
	Skybox(const Skybox& copy) = default;
	Skybox(Skybox&& move) = default;
	~Skybox() = default;
	Skybox& operator=(const Skybox& rhs) = default;
	void render(const Camera& cam, const Shader& shad, const std::vector<std::unique_ptr<Mesh>>& all_meshes, float width, float height);
private:
	std::string cube_mesh_link;
	CubeMap& cm;
};

#endif