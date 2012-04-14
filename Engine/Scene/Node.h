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

#include "Matrix3x4.h"
#include "Serializable.h"
#include "VectorBuffer.h"

class Component;
class Connection;
class Scene;
class SceneResolver;

/// Component and child node creation mode for networking.
enum CreateMode
{
    REPLICATED = 0,
    LOCAL = 1
};


/// %Scene node that may contain components and child nodes.
class Node : public Serializable
{
    OBJECT(Node);
    
    friend class Connection;
    
public:
    /// Construct.
    Node(Context* context);
    /// Destruct. Any child nodes are detached.
    virtual ~Node();
    /// Register object factory.
    static void RegisterObject(Context* context);
    
    /// Handle event. Targeted events will be forwarded to all components.
    virtual void OnEvent(Object* sender, bool broadcast, StringHash eventType, VariantMap& eventData);
    /// Load from binary data. Return true if successful.
    virtual bool Load(Deserializer& source);
    /// Load from XML data. Return true if successful.
    virtual bool LoadXML(const XMLElement& source);
    /// Save as binary data. Return true if successful.
    virtual bool Save(Serializer& dest);
    /// Save as XML data. Return true if successful.
    virtual bool SaveXML(XMLElement& dest);
    /// Apply attribute changes that can not be applied immediately recursively to child nodes and components.
    virtual void ApplyAttributes();
    
    /// Save to an XML file. Return true if successful.
    bool SaveXML(Serializer& dest);
    /// %Set name.
    void SetName(const String& name);
    /// %Set position.
    void SetPosition(const Vector3& position);
    /// %Set rotation.
    void SetRotation(const Quaternion& rotation);
    /// %Set direction. Positive Z equals identity.
    void SetDirection(const Vector3& direction);
    /// %Set uniform scale.
    void SetScale(float scale);
    /// %Set scale.
    void SetScale(const Vector3& scale);
    /// %Set transform.
    void SetTransform(const Vector3& position, const Quaternion& rotation);
    /// %Set transform.
    void SetTransform(const Vector3& position, const Quaternion& rotation, float scale);
    /// %Set transform.
    void SetTransform(const Vector3& position, const Quaternion& rotation, const Vector3& scale);
    /// %Set world position.
    void SetWorldPosition(const Vector3& position);
    /// %Set world rotation.
    void SetWorldRotation(const Quaternion& rotation);
    /// %Set world direction.
    void SetWorldDirection(const Vector3& direction);
    /// %Set uniform world scale.
    void SetWorldScale(float scale);
    /// %Set world scale.
    void SetWorldScale(const Vector3& scale);
    /// %Set world transform.
    void SetWorldTransform(const Vector3& position, const Quaternion& rotation);
    /// %Set world transform.
    void SetWorldTransform(const Vector3& position, const Quaternion& rotation, float scale);
    /// %Set world transform.
    void SetWorldTransform(const Vector3& position, const Quaternion& rotation, const Vector3& scale);
    /// Move the scene node.
    void Translate(const Vector3& delta);
    /// Move the scene node relative to its rotation.
    void TranslateRelative(const Vector3& delta);
    /// Rotate the scene node.
    void Rotate(const Quaternion& delta, bool fixedAxis = false);
    /// Rotate around the X axis.
    void Pitch(float angle, bool fixedAxis = false);
    /// Rotate around the Y axis.
    void Yaw(float angle, bool fixedAxis = false);
    /// Rotate around the Z axis.
    void Roll(float angle, bool fixedAxis = false);
    /// Look at a target position.
    void LookAt(const Vector3& target, const Vector3& upAxis = Vector3::UP, bool worldSpace = false);
    /// Modify scale uniformly.
    void Scale(float scale);
    /// Modify scale.
    void Scale(const Vector3& scale);
    /// %Set owner connection for networking.
    void SetOwner(Connection* owner);
    /// Mark node and child nodes to need world transform recalculation. Notify listener components.
    void MarkDirty();
    /// Create a child scene node.
    Node* CreateChild(const String& name = String(), CreateMode mode = REPLICATED);
    /// Add a child scene node.
    void AddChild(Node* node);
    /// Remove a child scene node.
    void RemoveChild(Node* node);
    /// Remove all child scene nodes.
    void RemoveAllChildren();
    /// Create a component to this node.
    Component* CreateComponent(ShortStringHash type, CreateMode mode = REPLICATED);
    /// Create a component to this node if it does not exist already.
    Component* GetOrCreateComponent(ShortStringHash type, CreateMode mode = REPLICATED);
    /// Remove a component from this node.
    void RemoveComponent(Component* component);
    /// Remove all components from this node.
    void RemoveAllComponents();
    /// Clone scene node, components and child nodes. Return the clone.
    Node* Clone(CreateMode mode = REPLICATED);
    /// Remove from the parent node. If no other shared pointer references exist, causes immediate deletion.
    void Remove();
    /// %Set parent scene node. Retains the world transform.
    void SetParent(Node* parent);
    /// Add listener component that is notified of node being dirtied. Can either be in the same node or another.
    void AddListener(Component* component);
    /// Remove listener component.
    void RemoveListener(Component* component);
    /// Template version of creating a component.
    template <class T> T* CreateComponent(CreateMode mode = REPLICATED);
    /// Template version of getting or creating a component.
    template <class T> T* GetOrCreateComponent(CreateMode mode = REPLICATED);
    
    /// Return ID.
    unsigned GetID() const { return id_; }
    /// Return name.
    const String& GetName() const { return name_; }
    /// Return name hash.
    StringHash GetNameHash() const { return nameHash_; }
    /// Return parent scene node.
    Node* GetParent() const { return parent_; }
    /// Return scene.
    Scene* GetScene() const { return scene_; }
    /// Return owner connection in networking.
    Connection* GetOwner() const { return owner_; }
    /// Return position.
    const Vector3& GetPosition() const { return position_; }
    /// Return rotation.
    const Quaternion& GetRotation() const { return rotation_; }
    /// Return direction. Identity rotation equals positive Z.
    Vector3 GetDirection() const { return rotation_ * Vector3::FORWARD; }
    /// Return scale.
    const Vector3& GetScale() const { return scale_; }
    /// Return local transform.
    Matrix3x4 GetTransform() const { return Matrix3x4(position_, rotation_, scale_); }
    
    /// Return world-space position.
    Vector3 GetWorldPosition() const
    {
        if (dirty_)
            UpdateWorldTransform();
        
        return worldTransform_.Translation();
    }
    
    /// Return world-space rotation.
    Quaternion GetWorldRotation() const
    {
        if (dirty_)
            UpdateWorldTransform();
        
        return worldTransform_.Rotation();
    }
    
    /// Return world-space direction.
    Vector3 GetWorldDirection() const
    {
        if (dirty_)
            UpdateWorldTransform();
        
        return worldTransform_.RotationMatrix() * Vector3::FORWARD;
    }
    
    /// Return world-space scale.
    Vector3 GetWorldScale() const
    {
        if (dirty_)
            UpdateWorldTransform();
        
        return worldTransform_.Scale();
    }
    
    /// Return world-space transform.
    const Matrix3x4& GetWorldTransform() const
    {
        if (dirty_)
            UpdateWorldTransform();
        
        return worldTransform_;
    }
    
    /// Convert a local-space position to world space.
    Vector3 LocalToWorld(const Vector3& position) const;
    /// Convert a local-space position or rotation to world space.
    Vector3 LocalToWorld(const Vector4& vector) const;
    /// Convert a world-space position to local space.
    Vector3 WorldToLocal(const Vector3& position) const;
    /// Convert a world-space position or rotation to local space.
    Vector3 WorldToLocal(const Vector4& vector) const;
    /// Return whether transform has changed and world transform needs recalculation.
    bool IsDirty() const { return dirty_; }
    /// Return number of child scene nodes.
    unsigned GetNumChildren(bool recursive = false) const;
    /// Return immediate child scene nodes.
    const Vector<SharedPtr<Node> >& GetChildren() const { return children_; }
    /// Return child scene nodes, optionally recursive.
    void GetChildren(PODVector<Node*>& dest, bool recursive = false) const;
    /// Return child scene nodes with a specific component.
    void GetChildrenWithComponent(PODVector<Node*>& dest, ShortStringHash type, bool recursive = false) const;
    /// Return child scene node by index.
    Node* GetChild(unsigned index) const;
    /// Return child scene node by name.
    Node* GetChild(const String& name, bool recursive = false) const;
    /// Return child scene node by name hash.
    Node* GetChild(StringHash nameHash, bool recursive = false) const;
    /// Return number of components.
    unsigned GetNumComponents() const { return components_.Size(); }
    /// Return number of non-local components.
    unsigned GetNumNetworkComponents() const;
    /// Return all components.
    const Vector<SharedPtr<Component> >& GetComponents() const { return components_; }
    /// Return all components of type.
    void GetComponents(PODVector<Component*>& dest, ShortStringHash type) const;
    /// Return component by type. If there are several, returns the first.
    Component* GetComponent(ShortStringHash type) const;
    /// Return whether has a specific component.
    bool HasComponent(ShortStringHash type) const;
    /// Return listener components.
    const Vector<WeakPtr<Component> > GetListeners() const { return listeners_; }
    /// Return user variables.
    VariantMap& GetVars() { return vars_; }
    /// Return the depended on nodes to order network updates.
    void GetDependencyNodes(PODVector<Node*>& dest) const;
    /// Return first component derived from class.
    template <class T> T* GetDerivedComponent() const;
    /// Return components derived from class.
    template <class T> void GetDerivedComponents(PODVector<T*>& dest) const;
    /// Template version of returning child nodes with a specific component.
    template <class T> void GetChildrenWithComponent(PODVector<Node*>& dest, bool recursive = false) const;
    /// Template version of returning a component by type.
    template <class T> T* GetComponent() const;
    /// Template version of returning all components of type.
    template <class T> void GetComponents(PODVector<T*>& dest) const;
    /// Template version of checking whether has a specific component.
    template <class T> bool HasComponent() const;
    
    /// %Set ID. Called by Scene.
    void SetID(unsigned id);
    /// %Set scene. Called by Scene.
    void SetScene(Scene* scene);
    /// %Set network position attribute.
    void SetNetPositionAttr(const Vector3& value);
    /// %Set network rotation attribute.
    void SetNetRotationAttr(const PODVector<unsigned char>& value);
    /// %Set network parent attribute.
    void SetNetParentAttr(const PODVector<unsigned char>& value);
    /// Return network position attribute.
    const Vector3& GetNetPositionAttr() const;
    /// Return network rotation attribute.
    const PODVector<unsigned char>& GetNetRotationAttr() const;
    /// Return network parent attribute.
    const PODVector<unsigned char>& GetNetParentAttr() const;
    /// Load components and optionally load child nodes.
    bool Load(Deserializer& source, SceneResolver& resolver, bool loadChildren = true, bool rewriteIDs = false, CreateMode mode = REPLICATED);
    /// Load components from XML data and optionally load child nodes.
    bool LoadXML(const XMLElement& source, SceneResolver& resolver, bool loadChildren = true, bool rewriteIDs = false, CreateMode mode = REPLICATED);
    
    /// User variables.
    VariantMap vars_;
    
protected:
    /// Create a component with specific ID.
    Component* CreateComponent(ShortStringHash type, unsigned id, CreateMode mode);
    /// Create a child node with specific ID.
    Node* CreateChild(unsigned id, CreateMode mode);
    
private:
    /// Recalculate the world transform.
    void UpdateWorldTransform() const;
    /// Remove child node by iterator.
    void RemoveChild(Vector<SharedPtr<Node> >::Iterator i);
    /// Return child nodes recursively.
    void GetChildrenRecursive(PODVector<Node*>& dest) const;
    /// Return child nodes with a specific component recursively.
    void GetChildrenWithComponentRecursive(PODVector<Node*>& dest, ShortStringHash type) const;
    /// Clone node recursively.
    Node* CloneRecursive(Node* parent, SceneResolver& resolver, CreateMode mode);
    
    /// Unique ID within the scene.
    unsigned id_;
    /// Parent scene node.
    Node* parent_;
    /// Scene (root node.)
    Scene* scene_;
    /// Owner connection in networking.
    Connection* owner_;
    /// Position.
    Vector3 position_;
    /// Rotation.
    Quaternion rotation_;
    /// Scale.
    Vector3 scale_;
    /// World-space transform matrix.
    mutable Matrix3x4 worldTransform_;
    /// Name.
    String name_;
    /// Name hash.
    StringHash nameHash_;
    /// Child scene nodes.
    Vector<SharedPtr<Node> > children_;
    /// Components.
    Vector<SharedPtr<Component> > components_;
    /// Node listeners.
    Vector<WeakPtr<Component> > listeners_;
    /// Attribute buffer for network replication.
    mutable VectorBuffer attrBuffer_;
    /// Consecutive rotation count for rotation renormalization.
    unsigned char rotateCount_;
    /// World transform needs update flag.
    mutable bool dirty_;
};

template <class T> T* Node::CreateComponent(CreateMode mode) { return static_cast<T*>(CreateComponent(T::GetTypeStatic(), mode)); }
template <class T> T* Node::GetOrCreateComponent(CreateMode mode) { return static_cast<T*>(GetOrCreateComponent(T::GetTypeStatic(), mode)); }
template <class T> void Node::GetChildrenWithComponent(PODVector<Node*>& dest, bool recursive) const { GetChildrenWithComponent(dest, T::GetTypeStatic(), recursive); }
template <class T> T* Node::GetComponent() const { return static_cast<T*>(GetComponent(T::GetTypeStatic())); }
template <class T> void Node::GetComponents(PODVector<T*>& dest) const { GetComponents(reinterpret_cast<PODVector<Component*>&>(dest), T::GetTypeStatic()); }
template <class T> bool Node::HasComponent() const { return HasComponent(T::GetTypeStatic()); }

template <class T> T* Node::GetDerivedComponent() const
{
    for (Vector<SharedPtr<Component> >::ConstIterator i = components_.Begin(); i != components_.End(); ++i)
    {
        T* component = dynamic_cast<T*>(i->Get());
        if (component)
            return component;
    }
    
    return 0;
}

template <class T> void Node::GetDerivedComponents(PODVector<T*>& dest) const
{
    dest.Clear();
    
    for (Vector<SharedPtr<Component> >::ConstIterator i = components_.Begin(); i != components_.End(); ++i)
    {
        T* component = dynamic_cast<T*>(i->Get());
        if (component)
            dest.Push(component);
    }
}
