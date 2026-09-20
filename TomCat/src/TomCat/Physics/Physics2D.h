#pragma once

// TomCat's runtime handles own Butter objects. This layer translates the scene
// lifecycle and query callbacks; integration and collision solving run in Butter.
#include <butter/physics2d/butter2d.h>
#include <cfloat>
#include <map>

namespace TomCat::Physics2D {
namespace Native = butter::physics2d;
struct Vec2 : Native::Vec2 {
    using Native::Vec2::Vec2;
    Vec2(Native::Vec2 value) : Native::Vec2(value) {}
    void Set(float px, float py) {
        x = px;
        y = py;
    }
    float Length() const { return length(); }
};
inline Vec2 Mul(const Native::Transform &t, Vec2 p) {
    return t.position + Native::rotate(p, t.angle);
}
inline constexpr float pi = 3.14159265358979323846f;
inline constexpr float epsilon = FLT_EPSILON;
inline constexpr float linearSlop = 0.005f;
using BodyType = Native::BodyType;
inline constexpr BodyType staticBody = BodyType::Static, dynamicBody = BodyType::Dynamic,
                          kinematicBody = BodyType::Kinematic;
struct BodyUserData {
    uint64_t pointer{};
};
using JointUserData = BodyUserData;
struct Filter {
    uint16_t categoryBits{1}, maskBits{0xffff};
};
struct AABB {
    Vec2 lowerBound, upperBound;
};
class Body;
class Fixture;
class World;
struct Shape {
    enum Type { e_polygon, e_circle };
    virtual ~Shape() = default;
    virtual Type GetType() const = 0;
    virtual std::unique_ptr<Shape> Clone() const = 0;
    virtual Native::Shape NativeShape() const = 0;
    virtual Vec2 Offset() const { return {}; }
};
struct PolygonShape : Shape {
    int m_count{4};
    Vec2 m_vertices[4];
    void SetAsBox(float hx, float hy, Vec2 center = {}, float angle = 0) {
        const Vec2 corners[4] = {{-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}};
        for (int i = 0; i < 4; ++i)
            m_vertices[i] = center + Native::rotate(corners[i], angle);
    }
    Type GetType() const override { return e_polygon; }
    std::unique_ptr<Shape> Clone() const override { return std::make_unique<PolygonShape>(*this); }
    Native::Shape NativeShape() const override {
        return Native::Polygon{{m_vertices, m_vertices + 4}};
    }
};
struct CircleShape : Shape {
    Vec2 m_p{};
    float m_radius{0.5f};
    Type GetType() const override { return e_circle; }
    std::unique_ptr<Shape> Clone() const override { return std::make_unique<CircleShape>(*this); }
    Native::Shape NativeShape() const override { return Native::Circle{m_radius}; }
    Vec2 Offset() const override { return m_p; }
};
struct FixtureDef {
    Shape *shape{};
    float density{0}, friction{0.2f}, restitution{0}, restitutionThreshold{1};
    bool isSensor{false};
    Filter filter{};
};
struct BodyDef {
    BodyType type{staticBody};
    Vec2 position{}, linearVelocity{};
    float angle{0}, angularVelocity{0};
    bool fixedRotation{false}, awake{true};
    BodyUserData userData{};
};
class Fixture {
  public:
    Body *GetBody() const { return body; }
    Fixture *GetNext() const { return next; }
    const Shape *GetShape() const { return shape.get(); }
    bool IsSensor() const { return native->trigger; }
    Filter GetFilterData() const { return filter; }
    float GetFriction() const { return native->material.friction; }

  private:
    friend class Body;
    friend class World;
    Body *body{};
    Fixture *next{};
    Native::Fixture *native{};
    std::unique_ptr<Shape> shape;
    Filter filter{};
};
class Contact {
  public:
    Fixture *GetFixtureA() const { return a; }
    Fixture *GetFixtureB() const { return b; }
    bool IsTouching() const { return true; }
    Contact *GetNext() const { return next; }

  private:
    friend class World;
    Fixture *a{}, *b{};
    Contact *next{};
};
struct ContactListener {
    virtual ~ContactListener() = default;
    virtual void BeginContact(Contact *) {}
    virtual void EndContact(Contact *) {}
};
struct ContactFilter {
    virtual ~ContactFilter() = default;
    virtual bool ShouldCollide(Fixture *a, Fixture *b) {
        return (a->GetFilterData().categoryBits & b->GetFilterData().maskBits) &&
               (b->GetFilterData().categoryBits & a->GetFilterData().maskBits);
    }
};
struct RayCastCallback {
    virtual ~RayCastCallback() = default;
    virtual float ReportFixture(Fixture *, const Vec2 &, const Vec2 &, float) = 0;
};
struct QueryCallback {
    virtual ~QueryCallback() = default;
    virtual bool ReportFixture(Fixture *) = 0;
};
class Body {
  public:
    Body *GetNext() const { return next; }
    Fixture *GetFixtureList() const { return fixtures.empty() ? nullptr : fixtures.front().get(); }
    BodyUserData &GetUserData() { return userData; }
    Vec2 GetPosition() const { return native->transform.position; }
    float GetAngle() const { return native->transform.angle; }
    const Native::Transform &GetTransform() const { return native->transform; }
    Vec2 GetLinearVelocity() const { return native->velocity; }
    float GetAngularVelocity() const { return native->angular_velocity; }
    BodyType GetType() const { return native->type; }
    bool IsAwake() const { return !native->sleeping; }
    bool IsFixedRotation() const { return native->fixed_rotation; }
    float GetMass() const { return native->mass; }
    Vec2 GetWorldCenter() const { return native->transform.position; }
    void SetTransform(Vec2 p, float angle) { native->transform = {p, angle}; }
    void SetLinearVelocity(Vec2 v) {
        if (native->type != staticBody) {
            native->velocity = v;
            if (v.length_squared() > 0)
                native->wake();
        }
    }
    void SetAngularVelocity(float v) {
        if (native->type != staticBody && !native->fixed_rotation) {
            native->angular_velocity = v;
            if (v != 0)
                native->wake();
        }
    }
    void SetAwake(bool awake) {
        if (awake)
            native->wake();
        else {
            native->sleeping = true;
            native->velocity = {};
            native->angular_velocity = 0;
            native->force = {};
            native->torque = 0;
        }
    }
    void SetType(BodyType type) {
        native->type = type;
        if (type == staticBody) {
            native->velocity = {};
            native->angular_velocity = 0;
        }
        ResetMass();
    }
    void SetFixedRotation(bool value) {
        native->fixed_rotation = value;
        if (value)
            native->angular_velocity = 0;
        ResetMass();
    }
    Fixture *CreateFixture(const FixtureDef *);
    void DestroyFixture(Fixture *);
    void ApplyLinearImpulse(Vec2 impulse, Vec2 point, bool wake) {
        if (native->type != dynamicBody)
            return;
        if (wake)
            native->wake();
        if (!IsAwake())
            return;
        native->velocity += impulse * native->inverse_mass;
        native->angular_velocity +=
            (point - native->transform.position).cross(impulse) * native->inverse_inertia;
    }
    void ApplyLinearImpulseToCenter(Vec2 impulse, bool wake) {
        ApplyLinearImpulse(impulse, GetWorldCenter(), wake);
    }
    void ApplyForce(Vec2 force, Vec2 point, bool wake) {
        if (native->type != dynamicBody)
            return;
        if (wake)
            native->wake();
        if (!IsAwake())
            return;
        native->force += force;
        native->torque += (point - native->transform.position).cross(force);
    }
    void ApplyForceToCenter(Vec2 force, bool wake) { ApplyForce(force, GetWorldCenter(), wake); }

  private:
    friend class World;
    friend void LinearStiffness(float &, float &, float, float, Body *, Body *);
    World *world{};
    Native::Body *native{};
    Body *next{};
    BodyUserData userData{};
    std::vector<std::unique_ptr<Fixture>> fixtures;
    void ResetMass() {
        float mass = 0, inertia = 0;
        for (auto &f : fixtures) {
            const float density = f->native->material.density;
            if (auto *circle = dynamic_cast<CircleShape *>(f->shape.get())) {
                const float m =
                    3.14159265358979323846f * circle->m_radius * circle->m_radius * density;
                mass += m;
                inertia +=
                    m * (0.5f * circle->m_radius * circle->m_radius + circle->m_p.length_squared());
            } else if (auto *polygon = dynamic_cast<PolygonShape *>(f->shape.get())) {
                float area = 0, moment = 0;
                for (int i = 0; i < 4; ++i) {
                    auto a = polygon->m_vertices[i], b = polygon->m_vertices[(i + 1) % 4];
                    float cross = a.cross(b);
                    area += cross;
                    moment += cross * (a.dot(a) + a.dot(b) + b.dot(b));
                }
                mass += density * std::abs(area) * 0.5f;
                inertia += density * std::abs(moment) / 12;
            }
        }
        native->mass = native->type == dynamicBody ? std::max(mass, 1.0e-6f) : 0;
        if (native->type == dynamicBody && mass == 0)
            native->mass = 1;
        native->inverse_mass = native->mass > 0 ? 1 / native->mass : 0;
        native->inertia = inertia;
        native->inverse_inertia =
            native->type == dynamicBody && !native->fixed_rotation && inertia > 0 ? 1 / inertia : 0;
    }
};
struct DistanceJointDef {
    Body *bodyA{}, *bodyB{};
    Vec2 localAnchorA{}, localAnchorB{};
    float length{1}, minLength{0}, maxLength{FLT_MAX}, stiffness{0}, damping{0};
    bool collideConnected{false};
    JointUserData userData{};
};
class Joint {
    friend class World;
    Native::DistanceJoint *native{};
};
inline void LinearStiffness(float &stiffness, float &damping, float frequency, float ratio, Body *a,
                            Body *b) {
    const float inv = a->native->inverse_mass + b->native->inverse_mass;
    const float mass = inv > 0 ? 1 / inv : 0, omega = 2 * 3.14159265358979323846f * frequency;
    stiffness = mass * omega * omega;
    damping = 2 * mass * ratio * omega;
}
class World {
  public:
    explicit World(Vec2 gravity) : native(Native::World::Config{gravity}) {
        native.contact_filter = [this](const Native::Fixture &a, const Native::Fixture &b) {
            return !filter || filter->ShouldCollide(fixtureMap.at(&a), fixtureMap.at(&b));
        };
        native.on_contact = [this](Native::Fixture &a, Native::Fixture &b, bool enter) {
            auto key = std::make_pair(&a, &b);
            if (enter) {
                auto contact = std::make_unique<Contact>();
                contact->a = fixtureMap.at(&a);
                contact->b = fixtureMap.at(&b);
                auto *ptr = contact.get();
                contacts.emplace(key, std::move(contact));
                RelinkContacts();
                if (listener)
                    listener->BeginContact(ptr);
            } else if (auto it = contacts.find(key); it != contacts.end()) {
                if (listener)
                    listener->EndContact(it->second.get());
                contacts.erase(it);
                RelinkContacts();
            }
        };
    }
    Body *CreateBody(const BodyDef *def) {
        auto body = std::make_unique<Body>();
        body->world = this;
        body->native = &native.create_empty_body();
        auto &n = *body->native;
        n.type = def->type;
        n.transform = {def->position, def->angle};
        n.velocity = def->linearVelocity;
        n.angular_velocity = def->angularVelocity;
        n.fixed_rotation = def->fixedRotation;
        n.sleeping = !def->awake;
        n.linear_damping = 0;
        n.angular_damping = 0;
        n.user_data = def->userData.pointer;
        body->userData = def->userData;
        body->ResetMass();
        auto *ptr = body.get();
        bodies.push_back(std::move(body));
        RelinkBodies();
        return ptr;
    }
    void DestroyBody(Body *body) {
        // Scene destroys attached joints before bodies. Also handle direct clients.
        std::erase_if(joints, [&](auto &j) {
            return j->native->a == body->native || j->native->b == body->native;
        });
        native.destroy_body(*body->native);
        for (auto &f : body->fixtures)
            fixtureMap.erase(f->native);
        std::erase_if(bodies, [&](auto &b) { return b.get() == body; });
        RelinkBodies();
    }
    Joint *CreateJoint(const DistanceJointDef *def) {
        auto joint = std::make_unique<Joint>();
        joint->native =
            &native.add_distance_joint(*def->bodyA->native, *def->bodyB->native, def->length);
        auto &n = *joint->native;
        n.anchor_a = def->localAnchorA;
        n.anchor_b = def->localAnchorB;
        n.spring_stiffness = def->stiffness;
        n.damping = def->damping;
        n.collide_connected = def->collideConnected;
        n.user_data = def->userData.pointer;
        auto *ptr = joint.get();
        joints.push_back(std::move(joint));
        return ptr;
    }
    void DestroyJoint(Joint *joint) {
        native.destroy_joint(*joint->native);
        std::erase_if(joints, [&](auto &j) { return j.get() == joint; });
    }
    bool IsLocked() const { return native.locked(); }
    void SetContactListener(ContactListener *value) { listener = value; }
    void SetContactFilter(ContactFilter *value) { filter = value; }
    Body *GetBodyList() const { return bodies.empty() ? nullptr : bodies.front().get(); }
    Contact *GetContactList() const {
        return contacts.empty() ? nullptr : contacts.begin()->second.get();
    }
    void Step(float dt, int velocityIterations, int positionIterations) {
        native.set_solver_iterations(std::max(velocityIterations, positionIterations));
        native.step(dt);
    }
    void QueryAABB(QueryCallback *callback, const AABB &area) {
        for (auto &b : bodies)
            for (auto &f : b->fixtures)
                if (Native::compute_aabb(f->native->shape, Native::fixture_transform(*f->native))
                        .overlaps({area.lowerBound, area.upperBound}))
                    if (!callback->ReportFixture(f.get()))
                        return;
    }
    void RayCast(RayCastCallback *callback, Vec2 start, Vec2 end) {
        const Vec2 delta = end - start;
        const float length = delta.length();
        if (length <= 0)
            return;
        float fraction = 1;
        for (auto &b : bodies)
            for (auto &f : b->fixtures) {
                auto hit = Native::ray_shape(start, delta, length * fraction, f->native->shape,
                                             Native::fixture_transform(*f->native));
                if (!hit)
                    continue;
                float response = callback->ReportFixture(f.get(), Vec2(hit->point),
                                                         Vec2(hit->normal), hit->distance / length);
                if (response == 0)
                    return;
                if (response > 0)
                    fraction = std::min(fraction, response);
            }
    }

  private:
    friend class Body;
    Native::World native;
    ContactListener *listener{};
    ContactFilter *filter{};
    std::vector<std::unique_ptr<Body>> bodies;
    std::vector<std::unique_ptr<Joint>> joints;
    std::unordered_map<const Native::Fixture *, Fixture *> fixtureMap;
    std::map<std::pair<Native::Fixture *, Native::Fixture *>, std::unique_ptr<Contact>> contacts;
    void RelinkBodies() {
        for (size_t i = 0; i < bodies.size(); ++i)
            bodies[i]->next = i + 1 < bodies.size() ? bodies[i + 1].get() : nullptr;
    }
    void RelinkContacts() {
        Contact *previous = nullptr;
        for (auto &[key, c] : contacts) {
            if (previous)
                previous->next = c.get();
            previous = c.get();
        }
        if (previous)
            previous->next = nullptr;
    }
};
inline Fixture *Body::CreateFixture(const FixtureDef *def) {
    auto fixture = std::make_unique<Fixture>();
    fixture->body = this;
    fixture->shape = def->shape->Clone();
    fixture->filter = def->filter;
    Native::Fixture definition;
    definition.shape = fixture->shape->NativeShape();
    definition.local.position = fixture->shape->Offset();
    definition.material = {def->friction, def->restitution, def->density};
    definition.trigger = def->isSensor;
    definition.restitution_threshold = def->restitutionThreshold;
    definition.collision_group = def->filter.categoryBits;
    definition.collision_mask = def->filter.maskBits;
    fixture->native = &world->native.add_fixture(*native, definition);
    auto *ptr = fixture.get();
    if (!fixtures.empty())
        fixtures.back()->next = ptr;
    fixtures.push_back(std::move(fixture));
    world->fixtureMap.emplace(ptr->native, ptr);
    ResetMass();
    return ptr;
}
inline void Body::DestroyFixture(Fixture *fixture) {
    world->native.destroy_fixture(*fixture->native);
    world->fixtureMap.erase(fixture->native);
    std::erase_if(fixtures, [&](auto &f) { return f.get() == fixture; });
    for (size_t i = 0; i < fixtures.size(); ++i)
        fixtures[i]->next = i + 1 < fixtures.size() ? fixtures[i + 1].get() : nullptr;
    ResetMass();
}
} // namespace TomCat::Physics2D
