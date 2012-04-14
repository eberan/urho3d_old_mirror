//
// Urho3D Engine
// Copyright (c) 2008-2012 Lasse ��rni
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#pragma once

#include "Node.h"

#include <LinearMath/btMotionState.h>

class CollisionShape;
class DebugRenderer;
class PhysicsWorld;

class btCompoundShape;
class btRigidBody;

/// Rigid body collision event signaling mode.
enum CollisionEventMode
{
    COLLISION_NEVER = 0,
    COLLISION_ACTIVE,
    COLLISION_ALWAYS
};

/// Physics rigid body component.
class RigidBody : public Component, public btMotionState
{
    OBJECT(RigidBody);
    
    friend class PhysicsWorld;
    
public:
    /// Construct.
    RigidBody(Context* context);
    /// Destruct. Free the rigid body and geometries.
    virtual ~RigidBody();
    /// Register object factory.
    static void RegisterObject(Context* context);
    
    /// Return initial world transform to Bullet.
    virtual void getWorldTransform(btTransform &worldTrans) const;
    /// Update world transform from Bullet.
    virtual void setWorldTransform(const btTransform &worldTrans);
    
    /// %Set mass. Zero mass makes the body static.
    void SetMass(float mass);
    /// %Set rigid body world-space position.
    void SetPosition(Vector3 position);
    /// %Set rigid body world-space rotation.
    void SetRotation(Quaternion rotation);
    /// %Set rigid body world-space position and rotation.
    void SetTransform(const Vector3& position, const Quaternion& rotation);
    /// %Set linear velocity.
    void SetLinearVelocity(Vector3 velocity);
    /// %Set linear degrees of freedom.
    void SetLinearFactor(Vector3 factor);
    /// %Set linear velocity deactivation threshold.
    void SetLinearRestThreshold(float threshold);
    /// %Set linear velocity damping factor.
    void SetLinearDamping(float damping);
    /// %Set angular velocity.
    void SetAngularVelocity(Vector3 angularVelocity);
    /// %Set angular degrees of freedom.
    void SetAngularFactor(Vector3 factor);
    /// %Set angular velocity deactivation threshold.
    void SetAngularRestThreshold(float threshold);
    /// %Set angular velocity damping factor.
    void SetAngularDamping(float factor);
    /// %Set friction coefficient.
    void SetFriction(float friction);
    /// %Set restitution coefficient.
    void SetRestitution(float restitution);
    /// %Set whether gravity is applied to rigid body.
    void SetUseGravity(bool enable);
    /// %Set rigid body kinematic mode. In kinematic mode forces are not applied to the rigid body.
    void SetKinematic(bool enable);
    /// %Set rigid body phantom mode. In phantom mode collisions are registered but do not apply forces.
    void SetPhantom(bool enable);
    /// %Set continuous collision detection radius.
    void SetCcdRadius(float radius);
    /// %Set collision layer.
    void SetCollisionLayer(unsigned layer);
    /// %Set collision mask.
    void SetCollisionMask(unsigned mask);
    /// %Set collision group and mask.
    void SetCollisionLayerAndMask(unsigned layer, unsigned mask);
    /// %Set collision event signaling mode. Default is to signal when active.
    void SetCollisionEventMode(CollisionEventMode mode);
    /// Apply force to center of mass.
    void ApplyForce(const Vector3& force);
    /// Apply force at position.
    void ApplyForce(const Vector3& force, const Vector3& position);
    /// Apply torque.
    void ApplyTorque(const Vector3& torque);
    /// Apply impulse to center of mass.
    void ApplyImpulse(const Vector3& impulse);
    /// Apply impulse at position.
    void ApplyImpulse(const Vector3& impulse, const Vector3& position);
    /// Apply torque impulse.
    void ApplyTorqueImpulse(const Vector3& torque);
    /// Reset accumulated forces.
    void ResetForces();
    /// Activate rigid body if it was resting.
    void Activate();
    
    /// Return mass.
    float GetMass() const { return mass_; }
    /// Return rigid body world-space position.
    Vector3 GetPosition() const;
    /// Return rigid body world-space rotation.
    Quaternion GetRotation() const;
    /// Return linear velocity.
    Vector3 GetLinearVelocity() const;
    /// Return linear degrees of freedom.
    Vector3 GetLinearFactor() const;
    /// Return linear velocity deactivation threshold.
    float GetLinearRestThreshold() const;
    /// Return linear velocity damping threshold.
    float GetLinearDamping() const;
    /// Return linear velocity damping scale.
    float GetLinearDampingScale() const;
    /// Return angular velocity.
    Vector3 GetAngularVelocity() const;
    /// Return angular degrees of freedom.
    Vector3 GetAngularFactor() const;
    /// Return angular velocity deactivation threshold.
    float GetAngularRestThreshold() const;
    /// Return angular velocity damping threshold.
    float GetAngularDamping() const;
    /// Return angular velocity damping scale.
    float GetAngularDampingScale() const;
    /// Return friction coefficient.
    float GetFriction() const;
    /// Return restitution coefficient.
    float GetRestitution() const;
    /// Return whether rigid body uses gravity.
    bool GetUseGravity() const;
    /// Return kinematic mode flag.
    bool IsKinematic() const;
    /// Return phantom mode flag.
    bool IsPhantom() const;
    /// Return whether rigid body is active.
    bool IsActive() const;
    /// Return continuous collision detection radius.
    float GetCcdRadius() const;
    /// Return collision layer.
    unsigned GetCollisionLayer() const { return collisionLayer_; }
    /// Return collision mask.
    unsigned GetCollisionMask() const { return collisionMask_; }
    /// Return collision event signaling mode.
    CollisionEventMode GetCollisionEventMode() const { return collisionEventMode_; }
    
    /// Return physics world.
    PhysicsWorld* GetPhysicsWorld() const { return physicsWorld_; }
    /// Return Bullet rigid body.
    btRigidBody* GetBody() const { return body_; }
    /// Return Bullet compound collision shape.
    btCompoundShape* GetCompoundShape() const { return compoundShape_; }
    /// Update mass and inertia of rigid body.
    void UpdateMass();
    /// %Set network angular velocity attribute.
    void SetNetAngularVelocityAttr(const PODVector<unsigned char>& value);
    /// Return network angular velocity attribute.
    const PODVector<unsigned char>& GetNetAngularVelocityAttr() const;
    
    /// Add debug geometry to the debug renderer.
    void DrawDebugGeometry(DebugRenderer* debug, bool depthTest);
    
protected:
    /// Handle node being assigned.
    virtual void OnNodeSet(Node* node);
    /// Handle node transform being dirtied.
    virtual void OnMarkedDirty(Node* node);
    
private:
    /// Create the rigid body, or re-add to the physics world with changed flags. Calls UpdateMass().
    void AddBodyToWorld();
    /// Remove the rigid body.
    void ReleaseBody();
    
    /// Bullet rigid body.
    btRigidBody* body_;
    /// Bullet compound collision shape.
    btCompoundShape* compoundShape_;
    /// Physics world.
    WeakPtr<PhysicsWorld> physicsWorld_;
    /// Mass.
    float mass_;
    /// Attribute buffer for network replication.
    mutable VectorBuffer attrBuffer_;
    /// Collision layer.
    unsigned collisionLayer_;
    /// Collision mask.
    unsigned collisionMask_;
    /// Collision event signaling mode.
    CollisionEventMode collisionEventMode_;
    /// Last interpolated position from the simulation.
    mutable Vector3 lastPosition_;
    /// Last interpolated rotation from the simulation.
    mutable Quaternion lastRotation_;
    /// Whether is in Bullet's transform update. Node dirtying is ignored at this point to prevent endless recursion.
    bool inSetTransform_;
};
