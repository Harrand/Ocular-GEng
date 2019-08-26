#include "graphics/static_object.hpp"

template<class ObjectType>
RenderableBuffer<ObjectType>::RenderableBuffer(GLuint layout_qualifier_id): objects(), mesh_buffer(), data_ssbo(layout_qualifier_id), matrix_pool(this->data_ssbo.persistently_map<Matrix4x4>(RenderableBuffer::capacity, false)){}

template<class ObjectType>
RenderableBuffer<ObjectType>::RenderableBuffer(const RenderableBuffer<ObjectType>& copy): objects(), mesh_buffer(copy.mesh_buffer), data_ssbo(copy.data_ssbo), matrix_pool(copy.matrix_pool)
{
    // Just copy all objects individually.
    for(const auto&[id, object_ptr] : copy.objects)
        this->objects[id] = std::make_unique<ObjectType>(*object_ptr);
}

template<class ObjectType>
RenderableBuffer<ObjectType>::RenderableBuffer(RenderableBuffer<ObjectType>&& move): objects(std::move(move.objects)), mesh_buffer(std::move(move.mesh_buffer)), data_ssbo(std::move(move.data_ssbo)), matrix_pool(move.matrix_pool.first, move.matrix_pool.pool_size){}

template<class ObjectType>
RenderableBuffer<ObjectType>& RenderableBuffer<ObjectType>::operator=(RenderableBuffer<ObjectType>&& rhs)
{
    this->objects = std::move(rhs.objects);
    this->mesh_buffer = std::move(rhs.mesh_buffer);
    this->data_ssbo = std::move(rhs.data_ssbo);
    this->matrix_pool = rhs.matrix_pool;
    return *this;
}

template<class ObjectType>
std::size_t RenderableBuffer<ObjectType>::insert(ObjectType object)
{
    std::size_t id = this->objects.size();
    this->objects[id] = std::make_unique<ObjectType>(std::move(object));
    return id;
}

template<class ObjectType>
template<class ObjectSubType, typename... Args>
ObjectSubType& RenderableBuffer<ObjectType>::emplace(Args&&... args)
{
    std::size_t id = this->objects.size();
    this->objects[id] = std::make_unique<ObjectSubType>(std::forward<Args>(args)...);
    return *dynamic_cast<ObjectSubType*>(this->objects[id].get());
}

template<class ObjectType>
void RenderableBuffer<ObjectType>::render(RenderPass pass) const
{
    // Update the mvp matrix pool. Write "uniform" data.
    for(const auto&[id, object_ptr] : this->objects)
    {
        matrix_pool[id] = object_ptr->transform.model();
        matrix_pool[id+1] = pass.get_camera().view();
        matrix_pool[id+2] = pass.get_camera().projection(pass.get_window().get_width(), pass.get_window().get_height());
    }
    // After the pool is updated. bind the SSBO and initiate one render call.
    pass.get_render_context().get_object_shader().bind();
    this->data_ssbo.bind();
    this->mesh_buffer.render();
}

template<class ObjectType>
Renderable* RenderableBuffer<ObjectType>::take_object(std::unique_ptr<Renderable> object)
{
    std::size_t id = this->objects.size();
    StaticObject* owning_raw = dynamic_cast<StaticObject*>(object.release());
    this->objects[id] = std::unique_ptr<StaticObject>{owning_raw};
    return dynamic_cast<Renderable*>(owning_raw);
}