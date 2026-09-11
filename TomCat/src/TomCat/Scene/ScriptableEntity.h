#pragma once

#include "Entity.h"

namespace TomCat {

	class ScriptableEntity
	{
	public:

		virtual ~ScriptableEntity() {};

		template<typename T>
		T& GetComponent()
		{
			return m_Entity.GetComponent<T>();
		}

	protected:
		virtual void OnCreate() {}
		virtual void OnDestroy() {}
		virtual void OnUpdate(Timestep ts){}
		virtual void OnCollisionEnter2D(const CollisionEnter2D&) {}
		virtual void OnCollisionExit2D(const CollisionExit2D&) {}
		virtual void OnTriggerEnter2D(const TriggerEnter2D&) {}
		virtual void OnTriggerExit2D(const TriggerExit2D&) {}

	private:
		Entity m_Entity;
		friend class Scene;
	};

}
