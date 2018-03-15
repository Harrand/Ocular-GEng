#include <type_traits>

template<class Pixel>
Texture::Texture(Bitmap<Pixel> pixel_data): Texture(pixel_data.width, pixel_data.height, false)
{
	// handle not generated, do it.
	// Generates a new texture, and just fills it with zeroes if specified.
	glGenTextures(1, &(this->texture_handle));
	glBindTexture(GL_TEXTURE_2D, this->texture_handle);
	std::vector<unsigned char> image_data;
	for(const auto& pixel : pixel_data.pixels)
	{
		image_data.push_back(pixel.data.x);
		image_data.push_back(pixel.data.y);
		image_data.push_back(pixel.data.z);
		image_data.push_back(pixel.data.w);
	}
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, this->width, this->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data.data());
	// Unbind the texture.
	glBindTexture(GL_TEXTURE_2D, 0);
}

template<class T>
T* Texture::get_from_link(const std::string& texture_link, const std::vector<std::unique_ptr<T>>& all_textures)
{
	for(auto& texture : all_textures)
	{
		if(texture->get_file_name() == texture_link)
			return texture.get();
	}
	tz::util::log::error("Texture link \"", texture_link, "\" could not be located. Anything using this texture will not render.");
	return nullptr;
}

template<class Buffer, typename... Args>
Buffer& FrameBuffer::emplace(GLenum attachment, Args&&... args)
{
	if constexpr(std::is_same<Buffer, Texture>::value)
	{
		return this->emplace_texture(attachment, std::forward<Args>(args)...);
	}
	else if constexpr(std::is_same<Buffer, RenderBuffer>::value)
	{
		return this->emplace_renderbuffer(attachment, std::forward<Args>(args)...);
	}
	else
	{
		static_assert(std::is_void<Buffer>::value, "[Topaz Texture]: Texture::emplace has unsupported type.");
	}
}

template<typename... Args>
Texture& FrameBuffer::emplace_texture(GLenum attachment, Args&&... args)
{
	Texture& texture = std::get<Texture>((*(this->attachments.insert(std::make_pair(attachment, Texture(std::forward<Args>(args)...))).first)).second);
	glBindFramebuffer(GL_FRAMEBUFFER, this->framebuffer_handle);
	glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, texture.texture_handle, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return texture;
}

template<typename... Args>
RenderBuffer& FrameBuffer::emplace_renderbuffer(GLenum attachment, Args&&... args)
{
	RenderBuffer& render_buffer = std::get<RenderBuffer>((*(this->attachments.insert(std::make_pair(attachment, RenderBuffer(std::forward<Args>(args)...))).first)).second);
	glBindFramebuffer(GL_FRAMEBUFFER, this->framebuffer_handle);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment, GL_RENDERBUFFER, render_buffer.renderbuffer_handle);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	return render_buffer;
}