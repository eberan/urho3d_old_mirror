//
// Copyright (c) 2008-2013 the Urho3D project.
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
#include "Camera.h"
#include "Geometry.h"
#include "Graphics.h"
#include "GraphicsImpl.h"
#include "Material.h"
#include "Node.h"
#include "Renderer.h"
#include "Profiler.h"
#include "Scene.h"
#include "ShaderVariation.h"
#include "Sort.h"
#include "Technique.h"
#include "Texture2D.h"
#include "VertexBuffer.h"
#include "View.h"
#include "Zone.h"

#include "DebugNew.h"

namespace Urho3D
{

inline bool CompareBatchesState(Batch* lhs, Batch* rhs)
{
    if (lhs->sortKey_ != rhs->sortKey_)
        return lhs->sortKey_ < rhs->sortKey_;
    else
        return lhs->distance_ < rhs->distance_;
}

inline bool CompareBatchesFrontToBack(Batch* lhs, Batch* rhs)
{
    if (lhs->distance_ != rhs->distance_)
        return lhs->distance_ < rhs->distance_;
    else
        return lhs->sortKey_ < rhs->sortKey_;
}

inline bool CompareBatchesBackToFront(Batch* lhs, Batch* rhs)
{
    if (lhs->distance_ != rhs->distance_)
        return lhs->distance_ > rhs->distance_;
    else
        return lhs->sortKey_ < rhs->sortKey_;
}

inline bool CompareInstancesFrontToBack(const InstanceData& lhs, const InstanceData& rhs)
{
    return lhs.distance_ < rhs.distance_;
}

void CalculateShadowMatrix(Matrix4& dest, LightBatchQueue* queue, unsigned split, Renderer* renderer, const Vector3& translation)
{
    Camera* shadowCamera = queue->shadowSplits_[split].shadowCamera_;
    const IntRect& viewport = queue->shadowSplits_[split].shadowViewport_;
    
    Matrix3x4 posAdjust(translation, Quaternion::IDENTITY, 1.0f);
    Matrix3x4 shadowView(shadowCamera->GetView());
    Matrix4 shadowProj(shadowCamera->GetProjection());
    Matrix4 texAdjust(Matrix4::IDENTITY);
    
    Texture2D* shadowMap = queue->shadowMap_;
    if (!shadowMap)
        return;
    
    float width = (float)shadowMap->GetWidth();
    float height = (float)shadowMap->GetHeight();
    
    Vector2 offset(
        (float)viewport.left_ / width,
        (float)viewport.top_ / height
    );
    
    Vector2 scale(
        0.5f * (float)viewport.Width() / width,
        0.5f * (float)viewport.Height() / height
    );
    
    #ifdef USE_OPENGL
    offset.x_ += scale.x_;
    offset.y_ += scale.y_;
    offset.y_ = 1.0f - offset.y_;
    // If using 4 shadow samples, offset the position diagonally by half pixel
    if (renderer->GetShadowQuality() & SHADOWQUALITY_HIGH_16BIT)
    {
        offset.x_ -= 0.5f / width;
        offset.y_ -= 0.5f / height;
    }
    texAdjust.SetTranslation(Vector3(offset.x_, offset.y_, 0.5f));
    texAdjust.SetScale(Vector3(scale.x_, scale.y_, 0.5f));
    #else
    offset.x_ += scale.x_ + 0.5f / width;
    offset.y_ += scale.y_ + 0.5f / height;
    if (renderer->GetShadowQuality() & SHADOWQUALITY_HIGH_16BIT)
    {
        offset.x_ -= 0.5f / width;
        offset.y_ -= 0.5f / height;
    }
    scale.y_ = -scale.y_;
    texAdjust.SetTranslation(Vector3(offset.x_, offset.y_, 0.0f));
    texAdjust.SetScale(Vector3(scale.x_, scale.y_, 1.0f));
    #endif
    
    dest = texAdjust * shadowProj * shadowView * posAdjust;
}

void CalculateSpotMatrix(Matrix4& dest, Light* light, const Vector3& translation)
{
    Node* lightNode = light->GetNode();
    Matrix3x4 posAdjust(translation, Quaternion::IDENTITY, 1.0f);
    Matrix3x4 spotView = Matrix3x4(lightNode->GetWorldPosition(), lightNode->GetWorldRotation(), 1.0f).Inverse();
    Matrix4 spotProj(Matrix4::ZERO);
    Matrix4 texAdjust(Matrix4::IDENTITY);
    
    // Make the projected light slightly smaller than the shadow map to prevent light spill
    float h = 1.005f / tanf(light->GetFov() * M_DEGTORAD * 0.5f);
    float w = h / light->GetAspectRatio();
    spotProj.m00_ = w;
    spotProj.m11_ = h;
    spotProj.m22_ = 1.0f / Max(light->GetRange(), M_EPSILON);
    spotProj.m32_ = 1.0f;
    
    #ifdef USE_OPENGL
    texAdjust.SetTranslation(Vector3(0.5f, 0.5f, 0.5f));
    texAdjust.SetScale(Vector3(0.5f, -0.5f, 0.5f));
    #else
    texAdjust.SetTranslation(Vector3(0.5f, 0.5f, 0.0f));
    texAdjust.SetScale(Vector3(0.5f, -0.5f, 1.0f));
    #endif
    
    dest = texAdjust * spotProj * spotView * posAdjust;
}

void Batch::CalculateSortKey()
{
    unsigned shaderID = ((*((unsigned*)&vertexShader_) / sizeof(ShaderVariation)) + (*((unsigned*)&pixelShader_) / sizeof(ShaderVariation))) & 0x3fff;
    if (!isBase_)
        shaderID |= 0x8000;
    if (pass_ && pass_->GetAlphaMask())
        shaderID |= 0x4000;
    
    unsigned lightQueueID = (*((unsigned*)&lightQueue_) / sizeof(LightBatchQueue)) & 0xffff;
    unsigned materialID = (*((unsigned*)&material_) / sizeof(Material)) & 0xffff;
    unsigned geometryID = (*((unsigned*)&geometry_) / sizeof(Geometry)) & 0xffff;
    
    sortKey_ = (((unsigned long long)shaderID) << 48) | (((unsigned long long)lightQueueID) << 32) |
        (((unsigned long long)materialID) << 16) | geometryID;
}

void Batch::Prepare(View* view, bool setModelTransform) const
{
    if (!vertexShader_ || !pixelShader_)
        return;
    
    Graphics* graphics = view->GetGraphics();
    Renderer* renderer = view->GetRenderer();
    Node* cameraNode = camera_ ? camera_->GetNode() : 0;
    
    // Set pass / material-specific renderstates
    if (pass_ && material_)
    {
        bool isShadowPass = pass_->GetType() == PASS_SHADOW;
        
        graphics->SetBlendMode(pass_->GetBlendMode());
        renderer->SetCullMode(isShadowPass ? material_->GetShadowCullMode() : material_->GetCullMode(), camera_);
        if (!isShadowPass)
        {
            const BiasParameters& depthBias = material_->GetDepthBias();
            graphics->SetDepthBias(depthBias.constantBias_, depthBias.slopeScaledBias_);
        }
        graphics->SetDepthTest(pass_->GetDepthTestMode());
        graphics->SetDepthWrite(pass_->GetDepthWrite());
    }
    
    // Set shaders
    graphics->SetShaders(vertexShader_, pixelShader_);
    
    // Set global frame parameters
    if (graphics->NeedParameterUpdate(SP_FRAME, (void*)0))
    {
        Scene* scene = view->GetScene();
        if (scene)
        {
            float elapsedTime = scene->GetElapsedTime();
            graphics->SetShaderParameter(VSP_ELAPSEDTIME, elapsedTime);
            graphics->SetShaderParameter(PSP_ELAPSEDTIME, elapsedTime);
        }
    }
    
    // Set camera shader parameters
    unsigned cameraHash = overrideView_ ? (unsigned)(size_t)camera_ + 4 : (unsigned)(size_t)camera_;
    if (graphics->NeedParameterUpdate(SP_CAMERA, (void*)cameraHash))
    {
        // Calculate camera rotation just once
        Matrix3 cameraWorldRotation = cameraNode->GetWorldRotation().RotationMatrix();
        
        graphics->SetShaderParameter(VSP_CAMERAPOS, cameraNode->GetWorldPosition());
        graphics->SetShaderParameter(VSP_CAMERAROT, cameraWorldRotation);
        Vector4 depthMode = Vector4::ZERO;
        if (camera_->IsOrthographic())
        {
            depthMode.x_ = 1.0f;
            #ifdef USE_OPENGL
            depthMode.z_ = 0.5f;
            depthMode.w_ = 0.5f;
            #else
            depthMode.z_ = 1.0f;
            #endif
        }
        else
            depthMode.w_ = 1.0f / camera_->GetFarClip();
        
        graphics->SetShaderParameter(VSP_DEPTHMODE, depthMode);
        
        Vector3 nearVector, farVector;
        camera_->GetFrustumSize(nearVector, farVector);
        Vector4 viewportParams(farVector.x_, farVector.y_, farVector.z_, 0.0f);
        graphics->SetShaderParameter(VSP_FRUSTUMSIZE, viewportParams);
        
        Matrix4 projection = camera_->GetProjection();
        #ifdef USE_OPENGL
        // Add constant depth bias manually to the projection matrix due to glPolygonOffset() inconsistency
        float constantBias = 2.0f * graphics->GetDepthConstantBias();
        // On OpenGL ES slope-scaled bias can not be guaranteed to be available, and the shadow filtering is more coarse,
        // so use a higher constant bias
        #ifdef GL_ES_VERSION_2_0
        constantBias *= 1.5f;
        #endif
        projection.m22_ += projection.m32_ * constantBias;
        projection.m23_ += projection.m33_ * constantBias;
        #endif
        
        if (overrideView_)
            graphics->SetShaderParameter(VSP_VIEWPROJ, projection);
        else
            graphics->SetShaderParameter(VSP_VIEWPROJ, projection * camera_->GetView());
        
        graphics->SetShaderParameter(VSP_VIEWRIGHTVECTOR, cameraWorldRotation * Vector3::RIGHT);
        graphics->SetShaderParameter(VSP_VIEWUPVECTOR, cameraWorldRotation * Vector3::UP);
    }
    
    // Set viewport shader parameters
    IntVector2 rtSize = graphics->GetRenderTargetDimensions();
    IntRect viewport = graphics->GetViewport();
    unsigned viewportHash = (viewport.left_) | (viewport.top_ << 8) | (viewport.right_ << 16) | (viewport.bottom_ << 24);
    
    if (graphics->NeedParameterUpdate(SP_VIEWPORT, (void*)viewportHash))
    {
        float rtWidth = (float)rtSize.x_;
        float rtHeight = (float)rtSize.y_;
        float widthRange = 0.5f * viewport.Width() / rtWidth;
        float heightRange = 0.5f * viewport.Height() / rtHeight;
        
        #ifdef USE_OPENGL
        Vector4 bufferUVOffset(((float)viewport.left_) / rtWidth + widthRange,
            1.0f - (((float)viewport.top_) / rtHeight + heightRange), widthRange, heightRange);
        #else
        Vector4 bufferUVOffset((0.5f + (float)viewport.left_) / rtWidth + widthRange,
            (0.5f + (float)viewport.top_) / rtHeight + heightRange, widthRange, heightRange);
        #endif
        graphics->SetShaderParameter(VSP_GBUFFEROFFSETS, bufferUVOffset);
        
        float sizeX = 1.0f / rtWidth;
        float sizeY = 1.0f / rtHeight;
        graphics->SetShaderParameter(PSP_GBUFFERINVSIZE, Vector4(sizeX, sizeY, 0.0f, 0.0f));
    }
    
    // Set model transform
    if (setModelTransform && graphics->NeedParameterUpdate(SP_OBJECTTRANSFORM, worldTransform_))
        graphics->SetShaderParameter(VSP_MODEL, *worldTransform_);
    
    // Set skinning transforms
    if (shaderData_ && shaderDataSize_ && graphics->NeedParameterUpdate(SP_OBJECTDATA, shaderData_))
        graphics->SetShaderParameter(VSP_SKINMATRICES, shaderData_, shaderDataSize_);
    
    // Set zone-related shader parameters
    BlendMode blend = graphics->GetBlendMode();
    Zone* fogColorZone = (blend == BLEND_ADD || blend == BLEND_ADDALPHA) ? renderer->GetDefaultZone() : zone_;
    unsigned zoneHash = (unsigned)(size_t)zone_ + (unsigned)(size_t)fogColorZone;
    if (zone_ && graphics->NeedParameterUpdate(SP_ZONE, (void*)zoneHash))
    {
        graphics->SetShaderParameter(VSP_AMBIENTSTARTCOLOR, zone_->GetAmbientStartColor());
        graphics->SetShaderParameter(VSP_AMBIENTENDCOLOR, zone_->GetAmbientEndColor().ToVector4() - zone_->GetAmbientStartColor().ToVector4());
        
        const BoundingBox& box = zone_->GetBoundingBox();
        Vector3 boxSize = box.Size();
        Matrix3x4 adjust(Matrix3x4::IDENTITY);
        adjust.SetScale(Vector3(1.0f / boxSize.x_, 1.0f / boxSize.y_, 1.0f / boxSize.z_));
        adjust.SetTranslation(Vector3(0.5f, 0.5f, 0.5f));
        Matrix3x4 zoneTransform = adjust * zone_->GetInverseWorldTransform();
        graphics->SetShaderParameter(VSP_ZONE, zoneTransform);
        
        graphics->SetShaderParameter(PSP_AMBIENTCOLOR, zone_->GetAmbientColor());
        
        // If the pass is additive, override fog color to black so that shaders do not need a separate additive path
        graphics->SetShaderParameter(PSP_FOGCOLOR, fogColorZone->GetFogColor());
        
        float farClip = camera_->GetFarClip();
        float fogStart = Min(zone_->GetFogStart(), farClip);
        float fogEnd = Min(zone_->GetFogEnd(), farClip);
        if (fogStart >= fogEnd * (1.0f - M_LARGE_EPSILON))
            fogStart = fogEnd * (1.0f - M_LARGE_EPSILON);
        float fogRange = Max(fogEnd - fogStart, M_EPSILON);
        Vector4 fogParams(fogEnd / farClip, farClip / fogRange, 0.0f, 0.0f);
        graphics->SetShaderParameter(PSP_FOGPARAMS, fogParams);
    }
    
    // Set light-related shader parameters
    Light* light = 0;
    Texture2D* shadowMap = 0;
    if (lightQueue_)
    {
        light = lightQueue_->light_;
        shadowMap = lightQueue_->shadowMap_;
        
        if (graphics->NeedParameterUpdate(SP_VERTEXLIGHTS, lightQueue_) && graphics->HasShaderParameter(VS, VSP_VERTEXLIGHTS))
        {
            Vector4 vertexLights[MAX_VERTEX_LIGHTS * 3];
            const PODVector<Light*>& lights = lightQueue_->vertexLights_;
            
            for (unsigned i = 0; i < lights.Size(); ++i)
            {
                Light* vertexLight = lights[i];
                Node* vertexLightNode = vertexLight->GetNode();
                LightType type = vertexLight->GetLightType();
                
                // Attenuation
                float invRange, cutoff, invCutoff;
                if (type == LIGHT_DIRECTIONAL)
                    invRange = 0.0f;
                else
                    invRange = 1.0f / Max(vertexLight->GetRange(), M_EPSILON);
                if (type == LIGHT_SPOT)
                {
                    cutoff = cosf(vertexLight->GetFov() * 0.5f * M_DEGTORAD);
                    invCutoff = 1.0f / (1.0f - cutoff);
                }
                else
                {
                    cutoff = -1.0f;
                    invCutoff = 1.0f;
                }
                
                // Color
                float fade = 1.0f;
                float fadeEnd = vertexLight->GetDrawDistance();
                float fadeStart = vertexLight->GetFadeDistance();
                
                // Do fade calculation for light if both fade & draw distance defined
                if (vertexLight->GetLightType() != LIGHT_DIRECTIONAL && fadeEnd > 0.0f && fadeStart > 0.0f && fadeStart < fadeEnd)
                    fade = Min(1.0f - (vertexLight->GetDistance() - fadeStart) / (fadeEnd - fadeStart), 1.0f);
                
                Color color = vertexLight->GetColor() * fade;
                vertexLights[i * 3] = Vector4(color.r_, color.g_, color.b_, invRange);
                
                // Direction
                vertexLights[i * 3 + 1] = Vector4(-(vertexLightNode->GetWorldDirection()), cutoff);
                
                // Position
                vertexLights[i * 3 + 2] = Vector4(vertexLightNode->GetWorldPosition(), invCutoff);
            }
            
            if (lights.Size())
                graphics->SetShaderParameter(VSP_VERTEXLIGHTS, vertexLights[0].Data(), lights.Size() * 3 * 4);
        }
    }
    
    if (light && graphics->NeedParameterUpdate(SP_LIGHT, light))
    {
        Node* lightNode = light->GetNode();
        Matrix3 lightWorldRotation = lightNode->GetWorldRotation().RotationMatrix();
        
        graphics->SetShaderParameter(VSP_LIGHTDIR, lightWorldRotation * Vector3::BACK);
        
        float atten = 1.0f / Max(light->GetRange(), M_EPSILON);
        graphics->SetShaderParameter(VSP_LIGHTPOS, Vector4(lightNode->GetWorldPosition(), atten));
        
        if (graphics->HasShaderParameter(VS, VSP_LIGHTMATRICES))
        {
            switch (light->GetLightType())
            {
            case LIGHT_DIRECTIONAL:
                {
                    Matrix4 shadowMatrices[MAX_CASCADE_SPLITS];
                    unsigned numSplits = lightQueue_->shadowSplits_.Size();
                    for (unsigned i = 0; i < numSplits; ++i)
                        CalculateShadowMatrix(shadowMatrices[i], lightQueue_, i, renderer, Vector3::ZERO);
                    
                    graphics->SetShaderParameter(VSP_LIGHTMATRICES, shadowMatrices[0].Data(), 16 * numSplits);
                }
                break;
                
            case LIGHT_SPOT:
                {
                    Matrix4 shadowMatrices[2];
                    
                    CalculateSpotMatrix(shadowMatrices[0], light, Vector3::ZERO);
                    bool isShadowed = shadowMap && graphics->HasTextureUnit(TU_SHADOWMAP);
                    if (isShadowed)
                        CalculateShadowMatrix(shadowMatrices[1], lightQueue_, 0, renderer, Vector3::ZERO);
                    
                    graphics->SetShaderParameter(VSP_LIGHTMATRICES, shadowMatrices[0].Data(), isShadowed ? 32 : 16);
                }
                break;
                
            case LIGHT_POINT:
                {
                    Matrix4 lightVecRot(lightNode->GetWorldRotation().RotationMatrix());
                    // HLSL compiler will pack the parameters as if the matrix is only 3x4, so must be careful to not overwrite
                    // the next parameter
                    #ifdef USE_OPENGL
                    graphics->SetShaderParameter(VSP_LIGHTMATRICES, lightVecRot.Data(), 16);
                    #else
                    graphics->SetShaderParameter(VSP_LIGHTMATRICES, lightVecRot.Data(), 12);
                    #endif
                }
                break;
            }
        }
        
        float fade = 1.0f;
        float fadeEnd = light->GetDrawDistance();
        float fadeStart = light->GetFadeDistance();
        
        // Do fade calculation for light if both fade & draw distance defined
        if (light->GetLightType() != LIGHT_DIRECTIONAL && fadeEnd > 0.0f && fadeStart > 0.0f && fadeStart < fadeEnd)
            fade = Min(1.0f - (light->GetDistance() - fadeStart) / (fadeEnd - fadeStart), 1.0f);
        
        graphics->SetShaderParameter(PSP_LIGHTCOLOR, Vector4(light->GetColor().RGBValues(), light->GetSpecularIntensity()) * fade);
        graphics->SetShaderParameter(PSP_LIGHTDIR, lightWorldRotation * Vector3::BACK);
        graphics->SetShaderParameter(PSP_LIGHTPOS, Vector4(lightNode->GetWorldPosition() - cameraNode->GetWorldPosition(), atten));
        
        if (graphics->HasShaderParameter(PS, PSP_LIGHTMATRICES))
        {
            switch (light->GetLightType())
            {
            case LIGHT_DIRECTIONAL:
                {
                    Matrix4 shadowMatrices[MAX_CASCADE_SPLITS];
                    unsigned numSplits = lightQueue_->shadowSplits_.Size();
                    for (unsigned i = 0; i < numSplits; ++i)
                        CalculateShadowMatrix(shadowMatrices[i], lightQueue_, i, renderer, cameraNode->GetWorldPosition());
                    
                    graphics->SetShaderParameter(PSP_LIGHTMATRICES, shadowMatrices[0].Data(), 16 * numSplits);
                }
                break;
                
            case LIGHT_SPOT:
                {
                    Matrix4 shadowMatrices[2];
                    
                    CalculateSpotMatrix(shadowMatrices[0], light, cameraNode->GetWorldPosition());
                    bool isShadowed = lightQueue_->shadowMap_ != 0;
                    if (isShadowed)
                        CalculateShadowMatrix(shadowMatrices[1], lightQueue_, 0, renderer, cameraNode->GetWorldPosition());
                    
                    graphics->SetShaderParameter(PSP_LIGHTMATRICES, shadowMatrices[0].Data(), isShadowed ? 32 : 16);
                }
                break;
                
            case LIGHT_POINT:
                {
                    Matrix4 lightVecRot(lightNode->GetWorldRotation().RotationMatrix());
                    // HLSL compiler will pack the parameters as if the matrix is only 3x4, so must be careful to not overwrite
                    // the next parameter
                    #ifdef USE_OPENGL
                    graphics->SetShaderParameter(PSP_LIGHTMATRICES, lightVecRot.Data(), 16);
                    #else
                    graphics->SetShaderParameter(PSP_LIGHTMATRICES, lightVecRot.Data(), 12);
                    #endif
                }
                break;
            }
        }
        
        // Set shadow mapping shader parameters
        if (shadowMap)
        {
            {
                unsigned faceWidth = shadowMap->GetWidth() / 2;
                unsigned faceHeight = shadowMap->GetHeight() / 3;
                float width = (float)shadowMap->GetWidth();
                float height = (float)shadowMap->GetHeight();
                #ifdef USE_OPENGL
                    float mulX = (float)(faceWidth - 3) / width;
                    float mulY = (float)(faceHeight - 3) / height;
                    float addX = 1.5f / width;
                    float addY = 1.5f / height;
                #else
                    float mulX = (float)(faceWidth - 4) / width;
                    float mulY = (float)(faceHeight - 4) / height;
                    float addX = 2.5f / width;
                    float addY = 2.5f / height;
                #endif
                // If using 4 shadow samples, offset the position diagonally by half pixel
                if (renderer->GetShadowQuality() & SHADOWQUALITY_HIGH_16BIT)
                {
                    addX -= 0.5f / width;
                    addY -= 0.5f / height;
                }
                graphics->SetShaderParameter(PSP_SHADOWCUBEADJUST, Vector4(mulX, mulY, addX, addY));
            }
            
            {
                Camera* shadowCamera = lightQueue_->shadowSplits_[0].shadowCamera_;
                float nearClip = shadowCamera->GetNearClip();
                float farClip = shadowCamera->GetFarClip();
                float q = farClip / (farClip - nearClip);
                float r = -q * nearClip;
                
                const CascadeParameters& parameters = light->GetShadowCascade();
                float viewFarClip = camera_->GetFarClip();
                float shadowRange = parameters.GetShadowRange();
                float fadeStart = parameters.fadeStart_ * shadowRange / viewFarClip;
                float fadeEnd = shadowRange / viewFarClip;
                float fadeRange = fadeEnd - fadeStart;
                
                graphics->SetShaderParameter(PSP_SHADOWDEPTHFADE, Vector4(q, r, fadeStart, 1.0f / fadeRange));
            }
            
            {
                float intensity = light->GetShadowIntensity();
                float fadeStart = light->GetShadowFadeDistance();
                float fadeEnd = light->GetShadowDistance();
                if (fadeStart > 0.0f && fadeEnd > 0.0f && fadeEnd > fadeStart)
                    intensity = Lerp(intensity, 1.0f, Clamp((light->GetDistance() - fadeStart) / (fadeEnd - fadeStart), 0.0f, 1.0f));
                float pcfValues = (1.0f - intensity);
                float samples = renderer->GetShadowQuality() >= SHADOWQUALITY_HIGH_16BIT ? 4.0f : 1.0f;

                graphics->SetShaderParameter(PSP_SHADOWINTENSITY, Vector4(pcfValues / samples, intensity, 0.0f, 0.0f));
            }
            
            float sizeX = 1.0f / (float)shadowMap->GetWidth();
            float sizeY = 1.0f / (float)shadowMap->GetHeight();
            graphics->SetShaderParameter(PSP_SHADOWMAPINVSIZE, Vector4(sizeX, sizeY, 0.0f, 0.0f));
            
            Vector4 lightSplits(M_LARGE_VALUE, M_LARGE_VALUE, M_LARGE_VALUE, M_LARGE_VALUE);
            if (lightQueue_->shadowSplits_.Size() > 1)
                lightSplits.x_ = lightQueue_->shadowSplits_[0].farSplit_ / camera_->GetFarClip();
            if (lightQueue_->shadowSplits_.Size() > 2)
                lightSplits.y_ = lightQueue_->shadowSplits_[1].farSplit_ / camera_->GetFarClip();
            if (lightQueue_->shadowSplits_.Size() > 3)
                lightSplits.z_ = lightQueue_->shadowSplits_[2].farSplit_ / camera_->GetFarClip();
            
            graphics->SetShaderParameter(PSP_SHADOWSPLITS, lightSplits);
        }
    }
    
    // Set material-specific shader parameters and textures
    if (material_)
    {
        if (graphics->NeedParameterUpdate(SP_MATERIAL, material_))
        {
            const HashMap<StringHash, MaterialShaderParameter>& parameters = material_->GetShaderParameters();
            for (HashMap<StringHash, MaterialShaderParameter>::ConstIterator i = parameters.Begin(); i != parameters.End(); ++i)
                graphics->SetShaderParameter(i->first_, i->second_.value_);
        }
        
        const SharedPtr<Texture>* textures = material_->GetTextures();
        for (unsigned i = 0; i < MAX_MATERIAL_TEXTURE_UNITS; ++i)
        {
            TextureUnit unit = (TextureUnit)i;
            if (graphics->HasTextureUnit(unit))
                graphics->SetTexture(i, textures[i]);
        }
    }
    
    // Set light-related textures
    if (light)
    {
        if (shadowMap && graphics->HasTextureUnit(TU_SHADOWMAP))
            graphics->SetTexture(TU_SHADOWMAP, shadowMap);
        if (graphics->HasTextureUnit(TU_LIGHTRAMP))
        {
            Texture* rampTexture = light->GetRampTexture();
            if (!rampTexture)
                rampTexture = renderer->GetDefaultLightRamp();
            graphics->SetTexture(TU_LIGHTRAMP, rampTexture);
        }
        if (graphics->HasTextureUnit(TU_LIGHTSHAPE))
        {
            Texture* shapeTexture = light->GetShapeTexture();
            if (!shapeTexture && light->GetLightType() == LIGHT_SPOT)
                shapeTexture = renderer->GetDefaultLightSpot();
            graphics->SetTexture(TU_LIGHTSHAPE, shapeTexture);
        }
    }
}

void Batch::Draw(View* view) const
{
    if (!geometry_->IsEmpty())
    {
        Prepare(view);
        geometry_->Draw(view->GetGraphics());
    }
}

void BatchGroup::SetTransforms(View* view, void* lockedData, unsigned& freeIndex)
{
    // Do not use up buffer space if not going to draw as instanced
    if (geometry_->GetIndexCount() > (unsigned)view->GetRenderer()->GetMaxInstanceTriangles() * 3)
        return;
    
    startIndex_ = freeIndex;
    Matrix3x4* dest = (Matrix3x4*)lockedData;
    dest += freeIndex;
    
    for (unsigned i = 0; i < instances_.Size(); ++i)
        *dest++ = *instances_[i].worldTransform_;
    
    freeIndex += instances_.Size();
}

void BatchGroup::Draw(View* view) const
{
    Graphics* graphics = view->GetGraphics();
    Renderer* renderer = view->GetRenderer();
    
    if (instances_.Size() && !geometry_->IsEmpty())
    {
        // Draw as individual objects if instancing not supported
        VertexBuffer* instanceBuffer = renderer->GetInstancingBuffer();
        if (!instanceBuffer || geometry_->GetIndexCount() > (unsigned)renderer->GetMaxInstanceTriangles() * 3)
        {
            Batch::Prepare(view, false);
            
            graphics->SetIndexBuffer(geometry_->GetIndexBuffer());
            graphics->SetVertexBuffers(geometry_->GetVertexBuffers(), geometry_->GetVertexElementMasks());
            
            for (unsigned i = 0; i < instances_.Size(); ++i)
            {
                if (graphics->NeedParameterUpdate(SP_OBJECTTRANSFORM, instances_[i].worldTransform_))
                    graphics->SetShaderParameter(VSP_MODEL, *instances_[i].worldTransform_);
                
                graphics->Draw(geometry_->GetPrimitiveType(), geometry_->GetIndexStart(), geometry_->GetIndexCount(),
                    geometry_->GetVertexStart(), geometry_->GetVertexCount());
            }
        }
        else
        {
            Batch::Prepare(view, false);
            
            // Get the geometry vertex buffers, then add the instancing stream buffer
            // Hack: use a const_cast to avoid dynamic allocation of new temp vectors
            Vector<SharedPtr<VertexBuffer> >& vertexBuffers = const_cast<Vector<SharedPtr<VertexBuffer> >&>
                (geometry_->GetVertexBuffers());
            PODVector<unsigned>& elementMasks = const_cast<PODVector<unsigned>&>(geometry_->GetVertexElementMasks());
            vertexBuffers.Push(SharedPtr<VertexBuffer>(instanceBuffer));
            elementMasks.Push(instanceBuffer->GetElementMask());
            
            // No stream offset support, instancing buffer not pre-filled with transforms: have to fill now
            if (startIndex_ == M_MAX_UNSIGNED)
            {
                unsigned startIndex = 0;
                while (startIndex < instances_.Size())
                {
                    unsigned instances = instances_.Size() - startIndex;
                    if (instances > instanceBuffer->GetVertexCount())
                        instances = instanceBuffer->GetVertexCount();
                    
                    // Copy the transforms
                    Matrix3x4* dest = (Matrix3x4*)instanceBuffer->Lock(0, instances, true);
                    if (dest)
                    {
                        for (unsigned i = 0; i < instances; ++i)
                            dest[i] = *instances_[i + startIndex].worldTransform_;
                        instanceBuffer->Unlock();
                        
                        graphics->SetIndexBuffer(geometry_->GetIndexBuffer());
                        graphics->SetVertexBuffers(vertexBuffers, elementMasks);
                        graphics->DrawInstanced(geometry_->GetPrimitiveType(), geometry_->GetIndexStart(),
                            geometry_->GetIndexCount(), geometry_->GetVertexStart(), geometry_->GetVertexCount(), instances);
                    }
                    
                    startIndex += instances;
                }
            }
            // Stream offset supported, and instancing buffer has been already filled, so just draw
            else
            {
                graphics->SetIndexBuffer(geometry_->GetIndexBuffer());
                graphics->SetVertexBuffers(vertexBuffers, elementMasks, startIndex_);
                graphics->DrawInstanced(geometry_->GetPrimitiveType(), geometry_->GetIndexStart(), geometry_->GetIndexCount(),
                    geometry_->GetVertexStart(), geometry_->GetVertexCount(), instances_.Size());
            }
            
            // Remove the instancing buffer & element mask now
            vertexBuffers.Pop();
            elementMasks.Pop();
        }
    }
}

unsigned BatchGroupKey::ToHash() const
{
    return ((unsigned)(size_t)zone_) / sizeof(Zone) +
        ((unsigned)(size_t)lightQueue_) / sizeof(LightBatchQueue) +
        ((unsigned)(size_t)pass_) / sizeof(Pass) +
        ((unsigned)(size_t)material_) / sizeof(Material) +
        ((unsigned)(size_t)geometry_) / sizeof(Geometry);
}

void BatchQueue::Clear(int maxSortedInstances)
{
    batches_.Clear();
    sortedBaseBatches_.Clear();
    sortedBatches_.Clear();
    baseBatchGroups_.Clear();
    batchGroups_.Clear();
    maxSortedInstances_ = maxSortedInstances;
}

void BatchQueue::SortBackToFront()
{
    sortedBaseBatches_.Clear();
    sortedBatches_.Resize(batches_.Size());
    
    for (unsigned i = 0; i < batches_.Size(); ++i)
        sortedBatches_[i] = &batches_[i];
    
    Sort(sortedBatches_.Begin(), sortedBatches_.End(), CompareBatchesBackToFront);
    
    // Do not actually sort batch groups, just list them
    sortedBaseBatchGroups_.Resize(baseBatchGroups_.Size());
    sortedBatchGroups_.Resize(batchGroups_.Size());
    
    unsigned index = 0;
    for (HashMap<BatchGroupKey, BatchGroup>::Iterator i = baseBatchGroups_.Begin(); i != baseBatchGroups_.End(); ++i)
        sortedBaseBatchGroups_[index++] = &i->second_;
    index = 0;
    for (HashMap<BatchGroupKey, BatchGroup>::Iterator i = batchGroups_.Begin(); i != batchGroups_.End(); ++i)
        sortedBatchGroups_[index++] = &i->second_;
}

void BatchQueue::SortFrontToBack()
{
    sortedBaseBatches_.Clear();
    sortedBatches_.Clear();
    
    // Need to divide into base and non-base batches here to ensure proper order in relation to grouped batches
    for (unsigned i = 0; i < batches_.Size(); ++i)
    {
        if (batches_[i].isBase_)
            sortedBaseBatches_.Push(&batches_[i]);
        else
            sortedBatches_.Push(&batches_[i]);
    }
    
    SortFrontToBack2Pass(sortedBaseBatches_);
    SortFrontToBack2Pass(sortedBatches_);
    
    // Sort each group front to back
    for (HashMap<BatchGroupKey, BatchGroup>::Iterator i = baseBatchGroups_.Begin(); i != baseBatchGroups_.End(); ++i)
    {
        if (i->second_.instances_.Size() <= maxSortedInstances_)
        {
            Sort(i->second_.instances_.Begin(), i->second_.instances_.End(), CompareInstancesFrontToBack);
            if (i->second_.instances_.Size())
                i->second_.distance_ = i->second_.instances_[0].distance_;
        }
        else
        {
            float minDistance = M_INFINITY;
            for (PODVector<InstanceData>::ConstIterator j = i->second_.instances_.Begin(); j != i->second_.instances_.End(); ++j)
                minDistance = Min(minDistance, j->distance_);
            i->second_.distance_ = minDistance;
        }
    }
    
    for (HashMap<BatchGroupKey, BatchGroup>::Iterator i = batchGroups_.Begin(); i != batchGroups_.End(); ++i)
    {
        if (i->second_.instances_.Size() <= maxSortedInstances_)
        {
            Sort(i->second_.instances_.Begin(), i->second_.instances_.End(), CompareInstancesFrontToBack);
            if (i->second_.instances_.Size())
                i->second_.distance_ = i->second_.instances_[0].distance_;
        }
        else
        {
            float minDistance = M_INFINITY;
            for (PODVector<InstanceData>::ConstIterator j = i->second_.instances_.Begin(); j != i->second_.instances_.End(); ++j)
                minDistance = Min(minDistance, j->distance_);
            i->second_.distance_ = minDistance;
        }
    }
    
    sortedBaseBatchGroups_.Resize(baseBatchGroups_.Size());
    sortedBatchGroups_.Resize(batchGroups_.Size());
    
    unsigned index = 0;
    for (HashMap<BatchGroupKey, BatchGroup>::Iterator i = baseBatchGroups_.Begin(); i != baseBatchGroups_.End(); ++i)
        sortedBaseBatchGroups_[index++] = &i->second_;
    index = 0;
    for (HashMap<BatchGroupKey, BatchGroup>::Iterator i = batchGroups_.Begin(); i != batchGroups_.End(); ++i)
        sortedBatchGroups_[index++] = &i->second_;
    
    SortFrontToBack2Pass(reinterpret_cast<PODVector<Batch*>& >(sortedBaseBatchGroups_));
    SortFrontToBack2Pass(reinterpret_cast<PODVector<Batch*>& >(sortedBatchGroups_));
}

void BatchQueue::SortFrontToBack2Pass(PODVector<Batch*>& batches)
{
    // Mobile devices likely use a tiled deferred approach, with which front-to-back sorting is irrelevant. The 2-pass
    // method is also time consuming, so just sort with state having priority
    #ifdef GL_ES_VERSION_2_0
    Sort(batches.Begin(), batches.End(), CompareBatchesState);
    #else
    // For desktop, first sort by distance and remap shader/material/geometry IDs in the sort key
    Sort(batches.Begin(), batches.End(), CompareBatchesFrontToBack);
    
    unsigned freeShaderID = 0;
    unsigned short freeMaterialID = 0;
    unsigned short freeGeometryID = 0;
    
    for (PODVector<Batch*>::Iterator i = batches.Begin(); i != batches.End(); ++i)
    {
        Batch* batch = *i;
        
        unsigned shaderID = (batch->sortKey_ >> 32);
        HashMap<unsigned, unsigned>::ConstIterator j = shaderRemapping_.Find(shaderID);
        if (j != shaderRemapping_.End())
            shaderID = j->second_;
        else
        {
            shaderID = shaderRemapping_[shaderID] = freeShaderID | (shaderID & 0xc0000000);
            ++freeShaderID;
        }
        
        unsigned short materialID = (unsigned short)(batch->sortKey_ & 0xffff0000);
        HashMap<unsigned short, unsigned short>::ConstIterator k = materialRemapping_.Find(materialID);
        if (k != materialRemapping_.End())
            materialID = k->second_;
        else
        {
            materialID = materialRemapping_[materialID] = freeMaterialID;
            ++freeMaterialID;
        }
        
        unsigned short geometryID = (unsigned short)(batch->sortKey_ & 0xffff);
        HashMap<unsigned short, unsigned short>::ConstIterator l = geometryRemapping_.Find(geometryID);
        if (l != geometryRemapping_.End())
            geometryID = l->second_;
        else
        {
            geometryID = geometryRemapping_[geometryID] = freeGeometryID;
            ++freeGeometryID;
        }
        
        batch->sortKey_ = (((unsigned long long)shaderID) << 32) || (((unsigned long long)materialID) << 16) | geometryID;
    }
    
    shaderRemapping_.Clear();
    materialRemapping_.Clear();
    geometryRemapping_.Clear();
    
    // Finally sort again with the rewritten ID's
    Sort(batches.Begin(), batches.End(), CompareBatchesState);
    #endif
}

void BatchQueue::SetTransforms(View* view, void* lockedData, unsigned& freeIndex)
{
    for (HashMap<BatchGroupKey, BatchGroup>::Iterator i = baseBatchGroups_.Begin(); i != baseBatchGroups_.End(); ++i)
        i->second_.SetTransforms(view, lockedData, freeIndex);
    for (HashMap<BatchGroupKey, BatchGroup>::Iterator i = batchGroups_.Begin(); i != batchGroups_.End(); ++i)
        i->second_.SetTransforms(view, lockedData, freeIndex);
}

void BatchQueue::Draw(View* view, bool useScissor, bool markToStencil) const
{
    Graphics* graphics = view->GetGraphics();
    Renderer* renderer = view->GetRenderer();
    
    graphics->SetScissorTest(false);
    
    // During G-buffer rendering, mark opaque pixels to stencil buffer
    if (!markToStencil)
        graphics->SetStencilTest(false);
    
    // Base instanced
    for (PODVector<BatchGroup*>::ConstIterator i = sortedBaseBatchGroups_.Begin(); i != sortedBaseBatchGroups_.End(); ++i)
    {
        BatchGroup* group = *i;
        if (markToStencil)
            graphics->SetStencilTest(true, CMP_ALWAYS, OP_REF, OP_KEEP, OP_KEEP, group->lightMask_);
        
        group->Draw(view);
    }
    // Base non-instanced
    for (PODVector<Batch*>::ConstIterator i = sortedBaseBatches_.Begin(); i != sortedBaseBatches_.End(); ++i)
    {
        Batch* batch = *i;
        if (markToStencil)
            graphics->SetStencilTest(true, CMP_ALWAYS, OP_REF, OP_KEEP, OP_KEEP, batch->lightMask_);
        
        batch->Draw(view);
    }
    
    // Non-base instanced
    for (PODVector<BatchGroup*>::ConstIterator i = sortedBatchGroups_.Begin(); i != sortedBatchGroups_.End(); ++i)
    {
        BatchGroup* group = *i;
        if (useScissor && group->lightQueue_)
            renderer->OptimizeLightByScissor(group->lightQueue_->light_, group->camera_);
        if (markToStencil)
            graphics->SetStencilTest(true, CMP_ALWAYS, OP_REF, OP_KEEP, OP_KEEP, group->lightMask_);
        
        group->Draw(view);
    }
    // Non-base non-instanced
    for (PODVector<Batch*>::ConstIterator i = sortedBatches_.Begin(); i != sortedBatches_.End(); ++i)
    {
        Batch* batch = *i;
        if (useScissor)
        {
            if (!batch->isBase_ && batch->lightQueue_)
                renderer->OptimizeLightByScissor(batch->lightQueue_->light_, batch->camera_);
            else
                graphics->SetScissorTest(false);
        }
        if (markToStencil)
            graphics->SetStencilTest(true, CMP_ALWAYS, OP_REF, OP_KEEP, OP_KEEP, batch->lightMask_);
        
        batch->Draw(view);
    }
}

void BatchQueue::Draw(Light* light, View* view) const
{
    Graphics* graphics = view->GetGraphics();
    Renderer* renderer = view->GetRenderer();
    
    graphics->SetScissorTest(false);
    graphics->SetStencilTest(false);
    
    // Base instanced
    for (PODVector<BatchGroup*>::ConstIterator i = sortedBaseBatchGroups_.Begin(); i != sortedBaseBatchGroups_.End(); ++i)
    {
        BatchGroup* group = *i;
        group->Draw(view);
    }
    // Base non-instanced
    for (PODVector<Batch*>::ConstIterator i = sortedBaseBatches_.Begin(); i != sortedBaseBatches_.End(); ++i)
    {
        Batch* batch = *i;
        batch->Draw(view);
    }
    
    // All base passes have been drawn. Optimize at this point by both stencil volume and scissor
    bool optimized = false;
    
    // Non-base instanced
    for (PODVector<BatchGroup*>::ConstIterator i = sortedBatchGroups_.Begin(); i != sortedBatchGroups_.End(); ++i)
    {
        BatchGroup* group = *i;
        if (!optimized)
        {
            renderer->OptimizeLightByStencil(light, group->camera_);
            renderer->OptimizeLightByScissor(light, group->camera_);
            optimized = true;
        }
        group->Draw(view);
    }
    // Non-base non-instanced
    for (PODVector<Batch*>::ConstIterator i = sortedBatches_.Begin(); i != sortedBatches_.End(); ++i)
    {
        Batch* batch = *i;
        if (!optimized)
        {
            renderer->OptimizeLightByStencil(light, batch->camera_);
            renderer->OptimizeLightByScissor(light, batch->camera_);
            optimized = true;
        }
        batch->Draw(view);
    }
}

unsigned BatchQueue::GetNumInstances() const
{
    unsigned total = 0;
    
    for (HashMap<BatchGroupKey, BatchGroup>::ConstIterator i = baseBatchGroups_.Begin(); i != baseBatchGroups_.End(); ++i)
    {
        if (i->second_.geometryType_ == GEOM_INSTANCED)
            total += i->second_.instances_.Size();
    }
    for (HashMap<BatchGroupKey, BatchGroup>::ConstIterator i = batchGroups_.Begin(); i != batchGroups_.End(); ++i)
    {
       if (i->second_.geometryType_ == GEOM_INSTANCED)
            total += i->second_.instances_.Size();
    }
    
    return total;
}

}
