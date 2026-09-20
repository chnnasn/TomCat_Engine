#pragma once

#include <ekit/world.hpp>
#include <iterator>
#include <limits>
#include <tuple>
#include <unordered_map>

namespace TomCat {

// Scene-specific range and picking support. Entity/component ownership and
// generation validation belong to ekit::World.
class SceneWorld
{
public:
    void Swap(SceneWorld& other) noexcept
    {
        m_World.swap(other.m_World);
        m_PickingEntities.swap(other.m_PickingEntities);
        m_PickingIDs.swap(other.m_PickingIDs);
    }

    template<typename T> void RegisterSparseComponent() { m_World->RegisterSparseComponent<T>(); }
    template<typename T, typename... Args> T& Add(ekit::Entity e, Args&&... args)
    { return m_World->Add<T>(e, std::forward<Args>(args)...); }
    template<typename T, typename... Args> T& Set(ekit::Entity e, Args&&... args)
    { return m_World->Set<T>(e, std::forward<Args>(args)...); }
    template<typename T> T& Get(ekit::Entity e) { return m_World->Get<T>(e); }
    template<typename T> const T& Get(ekit::Entity e) const { return std::as_const(*m_World).Get<T>(e); }
    template<typename T> T* TryGet(ekit::Entity e) { return Has<T>(e) ? m_World->TryGet<T>(e) : nullptr; }
    template<typename T> const T* TryGet(ekit::Entity e) const { return Has<T>(e) ? std::as_const(*m_World).TryGet<T>(e) : nullptr; }
    template<typename T> bool Has(ekit::Entity e) const
    { return m_World && m_World->IsComponentRegistered<T>() && m_World->Has<T>(e); }
    template<typename T> bool Remove(ekit::Entity e) { return m_World->Remove<T>(e); }
    template<typename T> const auto& GetSparseStorage() const { return std::as_const(*m_World).GetSparseStorage<T>(); }
    bool IsAlive(ekit::Entity e) const { return m_World && m_World->IsAlive(e); }
    ekit::Entity GetEntity(ekit::EntityId index) const { return m_World->GetEntity(index); }

    ekit::Entity Create()
    {
        if (m_PickingEntities.size() >= static_cast<size_t>(std::numeric_limits<int>::max()))
            throw ekit::EkitException("Scene picking IDs exhausted.");
        auto entity = m_World->Create();
        try {
            m_PickingEntities.push_back(entity);
            m_PickingIDs.emplace(entity, static_cast<int>(m_PickingEntities.size() - 1));
        } catch (...) {
            if (!m_PickingEntities.empty() && m_PickingEntities.back() == entity)
                m_PickingEntities.pop_back();
            m_World->Destroy(entity);
            throw;
        }
        return entity;
    }

    void Destroy(ekit::Entity entity)
    {
        m_World->Destroy(entity);
        m_PickingIDs.erase(entity);
    }

    int GetPickingID(ekit::Entity entity) const
    {
        auto it = m_PickingIDs.find(entity);
        return it == m_PickingIDs.end() ? -1 : it->second;
    }

    ekit::Entity FindPickingEntity(int id) const
    {
        if (id < 0 || static_cast<size_t>(id) >= m_PickingEntities.size())
            return {};
        auto entity = m_PickingEntities[id];
        return IsAlive(entity) ? entity : ekit::Entity{};
    }

    template<typename... Ts> bool HasAll(ekit::Entity entity) const
    { return (Has<Ts>(entity) && ...); }
    template<typename... Ts> bool HasAny(ekit::Entity entity) const
    { return (Has<Ts>(entity) || ...); }

    // Read-only structural iteration over the smallest required sparse storage.
    // Components may be edited; structural changes must occur outside the range.
    template<typename WorldType, typename... Ts>
    class EntityRange
    {
        WorldType* m_World;
        const ekit::IComponentStorage* m_Storage = nullptr;
        ekit::EntityId (*m_EntityAt)(const ekit::IComponentStorage*, size_t) = nullptr;
        template<typename T> void Consider()
        {
            const auto& storage = m_World->template GetSparseStorage<T>();
            if (!m_Storage || storage.Size() < m_Storage->Size()) {
                m_Storage = &storage;
                m_EntityAt = [](const ekit::IComponentStorage* s, size_t i) {
                    return static_cast<const ekit::ComponentStorage<T>*>(s)->EntityAt(i);
                };
            }
        }
    public:
        explicit EntityRange(WorldType& world) : m_World(&world)
        { static_assert(sizeof...(Ts) > 0); (Consider<Ts>(), ...); }
        class Iterator
        {
            const EntityRange* m_Range;
            size_t m_Index;
            void Skip()
            {
                while (m_Index < m_Range->SizeHint() &&
                    !m_Range->m_World->template HasAll<Ts...>(**this)) ++m_Index;
            }
        public:
            using value_type = ekit::Entity;
            using difference_type = std::ptrdiff_t;
            using iterator_category = std::input_iterator_tag;
            Iterator(const EntityRange* range, size_t index) : m_Range(range), m_Index(index) { Skip(); }
            ekit::Entity operator*() const
            { return m_Range->m_World->GetEntity(m_Range->m_EntityAt(m_Range->m_Storage, m_Index)); }
            Iterator& operator++() { ++m_Index; Skip(); return *this; }
            Iterator operator++(int) { auto copy = *this; ++*this; return copy; }
            bool operator==(const Iterator& other) const = default;
        };
        Iterator begin() const { return {this, 0}; }
        Iterator end() const { return {this, SizeHint()}; }
        size_t SizeHint() const { return m_Storage->Size(); }
        template<typename... Us> decltype(auto) Get(ekit::Entity entity) const
        {
            if constexpr (sizeof...(Us) == 1)
                return (m_World->template Get<Us>(entity), ...);
            else
                return std::forward_as_tuple(m_World->template Get<Us>(entity)...);
        }
    };

    template<typename... Ts> auto View() { return EntityRange<SceneWorld, Ts...>(*this); }
    template<typename... Ts> auto View() const { return EntityRange<const SceneWorld, Ts...>(*this); }

private:
    // World is deliberately immovable; owning it lets scene deserialization
    // atomically adopt a validated world without moving its internal state.
    std::unique_ptr<ekit::World> m_World = std::make_unique<ekit::World>();
    std::vector<ekit::Entity> m_PickingEntities;
    std::unordered_map<ekit::Entity, int> m_PickingIDs;
};

}
