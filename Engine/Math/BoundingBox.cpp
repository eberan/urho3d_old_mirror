//
// Urho3D Engine
// Copyright (c) 2008-2011 Lasse ��rni
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
#include "Frustum.h"

void BoundingBox::Define(const Vector3* vertices, unsigned count)
{
    if (!count)
        return;
    
    defined_ = false;
    Merge(vertices, count);
}

void BoundingBox::Define(const Frustum& frustum)
{
    Define(frustum.GetVertices(), NUM_FRUSTUM_VERTICES);
}

void BoundingBox::Define(const Sphere& sphere)
{
    const Vector3& center = sphere.center_;
    float radius = sphere.radius_;
    
    min_ = center + Vector3(-radius, -radius, -radius);
    max_ = center + Vector3(radius, radius, radius);
    defined_ = true;
}

void BoundingBox::Merge(const Vector3* vertices, unsigned count)
{
    while (count--)
    {
        Merge(*vertices);
        ++vertices;
    }
}

void BoundingBox::Merge(const Frustum& frustum)
{
    Merge(frustum.GetVertices(), NUM_FRUSTUM_VERTICES);
}

void BoundingBox::Merge(const Sphere& sphere)
{
    const Vector3& center = sphere.center_;
    float radius = sphere.radius_;
    
    Merge(center + Vector3(radius, radius, radius));
    Merge(center + Vector3(-radius, -radius, -radius));
}

void BoundingBox::Intersect(const BoundingBox& box)
{
    if (box.min_.x_ > min_.x_)
        min_.x_ = box.min_.x_;
    if (box.max_.x_ < max_.x_)
        max_.x_ = box.max_.x_;
    if (box.min_.y_ > min_.y_)
        min_.y_ = box.min_.y_;
    if (box.max_.y_ < max_.y_)
        max_.y_ = box.max_.y_;
    if (box.min_.z_ > min_.z_)
        min_.z_ = box.min_.z_;
    if (box.max_.z_ < max_.z_)
        max_.z_ = box.max_.z_;
    
    float temp;
    if (min_.x_ > max_.x_)
    {
        temp = min_.x_;
        min_.x_ = max_.x_;
        max_.x_ = temp;
    }
    if (min_.y_ > max_.y_)
    {
        temp = min_.y_;
        min_.y_ = max_.y_;
        max_.y_ = temp;
    }
    if (min_.z_ > max_.z_)
    {
        temp = min_.z_;
        min_.z_ = max_.z_;
        max_.z_ = temp;
    }
}

void BoundingBox::Transform(const Matrix3& transform)
{
    Vector3 newCenter = transform * GetCenter();
    Vector3 oldEdge = GetSize() * 0.5;
    
    Vector3 newEdge = Vector3(
        fabsf(transform.m00_) * oldEdge.x_ + fabsf(transform.m01_) * oldEdge.y_ + fabsf(transform.m02_) * oldEdge.z_,
        fabsf(transform.m10_) * oldEdge.x_ + fabsf(transform.m11_) * oldEdge.y_ + fabsf(transform.m12_) * oldEdge.z_,
        fabsf(transform.m20_) * oldEdge.x_ + fabsf(transform.m21_) * oldEdge.y_ + fabsf(transform.m22_) * oldEdge.z_
    );
    
    min_ = newCenter - newEdge;
    max_ = newCenter + newEdge;
}

void BoundingBox::Transform(const Matrix4x3& transform)
{
    Vector3 newCenter = transform * GetCenter();
    Vector3 oldEdge = GetSize() * 0.5;
    
    Vector3 newEdge = Vector3(
        fabsf(transform.m00_) * oldEdge.x_ + fabsf(transform.m01_) * oldEdge.y_ + fabsf(transform.m02_) * oldEdge.z_,
        fabsf(transform.m10_) * oldEdge.x_ + fabsf(transform.m11_) * oldEdge.y_ + fabsf(transform.m12_) * oldEdge.z_,
        fabsf(transform.m20_) * oldEdge.x_ + fabsf(transform.m21_) * oldEdge.y_ + fabsf(transform.m22_) * oldEdge.z_
    );
    
    min_ = newCenter - newEdge;
    max_ = newCenter + newEdge;
}

BoundingBox BoundingBox::GetTransformed(const Matrix3& transform) const
{
    Vector3 newCenter = transform * GetCenter();
    Vector3 oldEdge = GetSize() * 0.5;
    
    Vector3 newEdge = Vector3(
        fabsf(transform.m00_) * oldEdge.x_ + fabsf(transform.m01_) * oldEdge.y_ + fabsf(transform.m02_) * oldEdge.z_,
        fabsf(transform.m10_) * oldEdge.x_ + fabsf(transform.m11_) * oldEdge.y_ + fabsf(transform.m12_) * oldEdge.z_,
        fabsf(transform.m20_) * oldEdge.x_ + fabsf(transform.m21_) * oldEdge.y_ + fabsf(transform.m22_) * oldEdge.z_
    );
    
    return BoundingBox(newCenter - newEdge, newCenter + newEdge);
}

BoundingBox BoundingBox::GetTransformed(const Matrix4x3& transform) const
{
    Vector3 newCenter = transform * GetCenter();
    Vector3 oldEdge = GetSize() * 0.5f;
    
    Vector3 newEdge = Vector3(
        fabsf(transform.m00_) * oldEdge.x_ + fabsf(transform.m01_) * oldEdge.y_ + fabsf(transform.m02_) * oldEdge.z_,
        fabsf(transform.m10_) * oldEdge.x_ + fabsf(transform.m11_) * oldEdge.y_ + fabsf(transform.m12_) * oldEdge.z_,
        fabsf(transform.m20_) * oldEdge.x_ + fabsf(transform.m21_) * oldEdge.y_ + fabsf(transform.m22_) * oldEdge.z_
    );
    
    return BoundingBox(newCenter - newEdge, newCenter + newEdge);
}

Rect BoundingBox::GetProjected(const Matrix4& projection) const
{
    Vector3 projMin = min_;
    Vector3 projMax = max_;
    if (projMin.z_ < M_MIN_NEARCLIP)
        projMin.z_ = M_MIN_NEARCLIP;
    if (projMax.z_ < M_MIN_NEARCLIP)
        projMax.z_ = M_MIN_NEARCLIP;
    
    Vector3 vertices[8];
    
    vertices[0] = projMin;
    vertices[1] = Vector3(projMax.x_, projMin.y_, projMin.z_);
    vertices[2] = Vector3(projMin.x_, projMax.y_, projMin.z_);
    vertices[3] = Vector3(projMax.x_, projMax.y_, projMin.z_);
    vertices[4] = Vector3(projMin.x_, projMin.y_, projMax.z_);
    vertices[5] = Vector3(projMax.x_, projMin.y_, projMax.z_);
    vertices[6] = Vector3(projMin.x_, projMax.y_, projMax.z_);
    vertices[7] = projMax;
    
    Rect rect;
    for (unsigned i = 0; i < 8; ++i)
    {
        Vector3 projected = projection * vertices[i];
        rect.Merge(Vector2(projected.x_, projected.y_));
    }
    
    return rect;
}

Intersection BoundingBox::IsInside(const Sphere& sphere) const
{
    float distSquared = 0;
    float temp;
    
    const Vector3& center = sphere.center_;
    
    if (center.x_ < min_.x_)
    {
        temp = center.x_ - min_.x_;
        distSquared += temp * temp;
    }
    else if (center.x_ > max_.x_)
    {
        temp = center.x_ - max_.x_;
        distSquared += temp * temp;
    }
    if (center.y_ < min_.y_)
    {
        temp = center.y_ - min_.y_;
        distSquared += temp * temp;
    }
    else if (center.y_ > max_.y_)
    {
        temp = center.y_ - max_.y_;
        distSquared += temp * temp;
    }
    if (center.z_ < min_.z_)
    {
        temp = center.z_ - min_.z_;
        distSquared += temp * temp;
    }
    else if (center.z_ > max_.z_)
    {
        temp = center.z_ - max_.z_;
        distSquared += temp * temp;
    }
    
    float radius = sphere.radius_;
    if (distSquared >= radius * radius)
        return OUTSIDE;
    
    if ((center.x_ - radius < min_.x_) || (center.x_ + radius > max_.x_))
        return INTERSECTS;
    if ((center.y_ - radius < min_.y_) || (center.y_ + radius > max_.y_))
        return INTERSECTS;
    if ((center.z_ - radius < min_.z_) || (center.z_ + radius > max_.z_))
        return INTERSECTS;
    
    return INSIDE;
}

Intersection BoundingBox::IsInsideFast(const Sphere& sphere) const
{
    float distSquared = 0;
    float temp;
    
    const Vector3& center = sphere.center_;
    
    if (center.x_ < min_.x_)
    {
        temp = center.x_ - min_.x_;
        distSquared += temp * temp;
    }
    else if (center.x_ > max_.x_)
    {
        temp = center.x_ - max_.x_;
        distSquared += temp * temp;
    }
    if (center.y_ < min_.y_)
    {
        temp = center.y_ - min_.y_;
        distSquared += temp * temp;
    }
    else if (center.y_ > max_.y_)
    {
        temp = center.y_ - max_.y_;
        distSquared += temp * temp;
    }
    if (center.z_ < min_.z_)
    {
        temp = center.z_ - min_.z_;
        distSquared += temp * temp;
    }
    else if (center.z_ > max_.z_)
    {
        temp = center.z_ - max_.z_;
        distSquared += temp * temp;
    }
    
    float radius = sphere.radius_;
    if (distSquared >= radius * radius)
        return OUTSIDE;
    
    return INSIDE;
}

float BoundingBox::GetDistance(const Ray& ray) const
{
    // If undefined, no hit (infinite distance)
    if (!defined_)
        return M_INFINITY;
    
    // Check for ray origin being inside the box
    if (IsInside(ray.origin_))
        return 0.0f;
    
    float dist = M_INFINITY;
    
    // Check for intersecting in the X-direction
    if ((ray.origin_.x_ < min_.x_) && (ray.direction_.x_ > 0.0f))
    {
        float x = (min_.x_ - ray.origin_.x_) / ray.direction_.x_;
        if (x < dist)
        {
            Vector3 point = ray.origin_ + x * ray.direction_;
            if ((point.y_ >= min_.y_) && (point.y_ <= max_.y_) &&
                (point.z_ >= min_.z_) && (point.z_ <= max_.z_))
                dist = x;
        }
    }
    if ((ray.origin_.x_ > max_.x_) && (ray.direction_.x_ < 0.0f))
    {
        float x = (max_.x_ - ray.origin_.x_) / ray.direction_.x_;
        if (x < dist)
        {
            Vector3 point = ray.origin_ + x * ray.direction_;
            if ((point.y_ >= min_.y_) && (point.y_ <= max_.y_) &&
                (point.z_ >= min_.z_) && (point.z_ <= max_.z_))
                dist = x;
        }
    }
    // Check for intersecting in the Y-direction
    if ((ray.origin_.y_ < min_.y_) && (ray.direction_.y_ > 0.0f))
    {
        float x = (min_.y_ - ray.origin_.y_) / ray.direction_.y_;
        if (x < dist)
        {
            Vector3 point = ray.origin_ + x * ray.direction_;
            if ((point.x_ >= min_.x_) && (point.x_ <= max_.x_) &&
                (point.z_ >= min_.z_) && (point.z_ <= max_.z_))
                dist = x;
        }
    }
    if ((ray.origin_.y_ > max_.y_) && (ray.direction_.y_ < 0.0f))
    {
        float x = (max_.y_ - ray.origin_.y_) / ray.direction_.y_;
        if (x < dist)
        {
            Vector3 point = ray.origin_ + x * ray.direction_;
            if ((point.x_ >= min_.x_) && (point.x_ <= max_.x_) &&
                (point.z_ >= min_.z_) && (point.z_ <= max_.z_))
                dist = x;
        }
    }
    // Check for intersecting in the Z-direction
    if ((ray.origin_.z_ < min_.z_) && (ray.direction_.z_ > 0.0f))
    {
        float x = (min_.z_ - ray.origin_.z_) / ray.direction_.z_;
        if (x < dist)
        {
            Vector3 point = ray.origin_ + x * ray.direction_;
            if ((point.x_ >= min_.x_) && (point.x_ <= max_.x_) &&
                (point.y_ >= min_.y_) && (point.y_ <= max_.y_))
                dist = x;
        }
    }
    if ((ray.origin_.z_ > max_.z_) && (ray.direction_.z_ < 0.0f))
    {
        float x = (max_.z_ - ray.origin_.z_) / ray.direction_.z_;
        if (x < dist)
        {
            Vector3 point = ray.origin_ + x * ray.direction_;
            if ((point.x_ >= min_.x_) && (point.x_ <= max_.x_) &&
                (point.y_ >= min_.y_) && (point.y_ <= max_.y_))
                dist = x;
        }
    }
    
    return dist;
}
