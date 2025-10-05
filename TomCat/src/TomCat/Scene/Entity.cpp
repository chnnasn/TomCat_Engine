#include "tcpch.h"
#include "Entity.h"

namespace TomCat {

	Entity::Entity(entt::entity handle, Scene* scene)
		:m_EntityHandle(handle),m_Scene(scene)
	{
	
	}

}