#include "entityobject.hpp"

EntityObject::EntityObject(std::string meshLink, std::string textureLink, std::string normalMapLink, std::string parallaxMapLink, float mass, Vector3F position, Vector3F rotation, Vector3F scale, Vector3F velocity, std::unordered_map<std::string, Force> forces): Entity(mass, position, velocity, forces), Object(meshLink, textureLink, normalMapLink, parallaxMapLink, position, rotation, scale){}

void EntityObject::setPosition(Vector3F pos)
{
	//this->pos is a protected member in Object. Entity::position is private so is not visible here.
	this->getPosR() = pos;
}

const Vector3F& EntityObject::getPosition() const
{
	return this->getPos();
}

void EntityObject::updateMotion(unsigned int fps)
{
	this->velocity += (this->getAcceleration() / fps);
	this->getPosR() += (this->velocity/fps);
}