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

#include "Precompiled.h"
#include "Context.h"
#include "Geometry.h"
#include "Model.h"
#include "Node.h"
#include "PhysicsUtils.h"
#include "PhysicsWorld.h"
#include "ResourceCache.h"
#include "TriangleMeshShape.h"

#include <BulletCollision/CollisionShapes/btScaledBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>

TriangleMeshData::TriangleMeshData(Model* model, unsigned lodLevel) :
    meshData_(0),
    shape_(0)
{
    modelName_ = model->GetName();
    meshData_ = new btTriangleMesh();
    const Vector<Vector<SharedPtr<Geometry> > >& geometries = model->GetGeometries();
    
    for (unsigned i = 0; i < geometries.Size(); ++i)
    {
        unsigned subGeometryLodLevel = lodLevel;
        if (subGeometryLodLevel >= geometries[i].Size())
            subGeometryLodLevel = geometries[i].Size() - 1;
        
        Geometry* geom = geometries[i][subGeometryLodLevel];
        if (!geom)
            continue;
        
        const unsigned char* vertexData;
        const unsigned char* indexData;
        unsigned vertexSize;
        unsigned indexSize;
        
        geom->GetRawData(vertexData, vertexSize, indexData, indexSize);
        if (!vertexData || !indexData)
            continue;
        
        unsigned indexStart = geom->GetIndexStart();
        unsigned indexCount = geom->GetIndexCount();
        
        // 16-bit indices
        if (indexSize == sizeof(unsigned short))
        {
            const unsigned short* indices = (const unsigned short*)indexData;
            
            for (unsigned j = indexStart; j < indexStart + indexCount; j += 3)
            {
                const Vector3& v0 = *((const Vector3*)(&vertexData[indices[j] * vertexSize]));
                const Vector3& v1 = *((const Vector3*)(&vertexData[indices[j + 1] * vertexSize]));
                const Vector3& v2 = *((const Vector3*)(&vertexData[indices[j + 2] * vertexSize]));
                meshData_->addTriangle(ToBtVector3(v0), ToBtVector3(v1), ToBtVector3(v2), true);
            }
        }
        // 32-bit indices
        else
        {
            const unsigned* indices = (const unsigned*)indexData;
            
            for (unsigned j = indexStart; j < indexStart + indexCount; j += 3)
            {
                const Vector3& v0 = *((const Vector3*)(&vertexData[indices[j] * vertexSize]));
                const Vector3& v1 = *((const Vector3*)(&vertexData[indices[j + 1] * vertexSize]));
                const Vector3& v2 = *((const Vector3*)(&vertexData[indices[j + 2] * vertexSize]));
                meshData_->addTriangle(ToBtVector3(v0), ToBtVector3(v1), ToBtVector3(v2), true);
            }
        }
    }
    
    shape_ = new btBvhTriangleMeshShape(meshData_, true, true);
}

TriangleMeshData::~TriangleMeshData()
{
    delete shape_;
    shape_ = 0;
    
    delete meshData_;
    meshData_ = 0;
}

OBJECTTYPESTATIC(TriangleMeshShape);

TriangleMeshShape::TriangleMeshShape(Context* context) :
    CollisionShape(context),
    size_(Vector3::ONE),
    lodLevel_(0)
{
}

TriangleMeshShape::~TriangleMeshShape()
{
    // Release shape first before letting go of the mesh geometry
    ReleaseShape();
    
    geometry_.Reset();
    if (physicsWorld_)
        physicsWorld_->CleanupGeometryCache();
}

void TriangleMeshShape::RegisterObject(Context* context)
{
    context->RegisterFactory<TriangleMeshShape>();
    
    ACCESSOR_ATTRIBUTE(TriangleMeshShape, VAR_RESOURCEREF, "Model", GetModelAttr, SetModelAttr, ResourceRef, ResourceRef(Model::GetTypeStatic()), AM_DEFAULT);
    ATTRIBUTE(TriangleMeshShape, VAR_INT, "LOD Level", lodLevel_, 0, AM_DEFAULT);
    ATTRIBUTE(TriangleMeshShape, VAR_VECTOR3, "Offset Position", position_, Vector3::ZERO, AM_DEFAULT);
    ATTRIBUTE(TriangleMeshShape, VAR_QUATERNION, "Offset Rotation", rotation_, Quaternion::IDENTITY, AM_DEFAULT);
    ATTRIBUTE(TriangleMeshShape, VAR_VECTOR3, "Size", size_, Vector3::ONE, AM_DEFAULT);
}

void TriangleMeshShape::SetModel(Model* model)
{
    if (model != model_)
    {
        model_ = model;
        UpdateCollisionShape();
        NotifyRigidBody();
    }
}

void TriangleMeshShape::SetLodLevel(unsigned lodLevel)
{
    if (lodLevel != lodLevel_)
    {
        lodLevel_ = lodLevel;
        UpdateCollisionShape();
        NotifyRigidBody();
    }
}

void TriangleMeshShape::SetSize(const Vector3& size)
{
    if (size != size_)
    {
        size_ = size;
        UpdateCollisionShape();
        NotifyRigidBody();
    }
}

Model* TriangleMeshShape::GetModel() const
{
    return model_;
}

void TriangleMeshShape::SetModelAttr(ResourceRef value)
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    model_ = cache->GetResource<Model>(value.id_);
    dirty_ = true;
}

ResourceRef TriangleMeshShape::GetModelAttr() const
{
    return GetResourceRef(model_, Model::GetTypeStatic());
}

void TriangleMeshShape::OnMarkedDirty(Node* node)
{
    Vector3 newWorldScale = node_->GetWorldScale();
    if (newWorldScale != cachedWorldScale_)
    {
        if (shape_)
            shape_->setLocalScaling(ToBtVector3(newWorldScale * size_));
        NotifyRigidBody();
        
        cachedWorldScale_ = newWorldScale;
    }
}

void TriangleMeshShape::UpdateCollisionShape()
{
    ReleaseShape();
    
    if (node_ && model_ && physicsWorld_)
    {
        // Check the geometry cache
        String id = model_->GetName() + "_" + String(lodLevel_);
        
        Map<String, SharedPtr<TriangleMeshData> >& cache = physicsWorld_->GetTriangleMeshCache();
        Map<String, SharedPtr<TriangleMeshData> >::Iterator j = cache.Find(id);
        if (j != cache.End())
            geometry_ = j->second_;
        else
        {
            geometry_ = new TriangleMeshData(model_, lodLevel_);
            cache[id] = geometry_;
        }
        
        shape_ = new btScaledBvhTriangleMeshShape(geometry_->shape_, ToBtVector3(node_->GetWorldScale() * size_));
    }
    else
        geometry_.Reset();
    
    if (physicsWorld_)
        physicsWorld_->CleanupGeometryCache();
}
