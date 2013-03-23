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
#include "Context.h"
#include "Log.h"
#include "Technique.h"
#include "Profiler.h"
#include "ResourceCache.h"
#include "ShaderVariation.h"
#include "StringUtils.h"
#include "XMLFile.h"

#include "DebugNew.h"

namespace Urho3D
{

const char* blendModeNames[] =
{
    "replace",
    "add",
    "multiply",
    "alpha",
    "addalpha",
    "premulalpha",
    "invdestalpha",
    0
};

static const String compareModeNames[] =
{
    "always",
    "equal",
    "notequal",
    "less",
    "lessequal",
    "greater",
    "greaterequal",
    ""
};

static const String lightingModeNames[] =
{
    "unlit",
    "pervertex",
    "perpixel",
    ""
};

Pass::Pass(StringHash type) :
    type_(type),
    blendMode_(BLEND_REPLACE),
    depthTestMode_(CMP_LESSEQUAL),
    lightingMode_(LIGHTING_UNLIT),
    shadersLoadedFrameNumber_(0),
    depthWrite_(true),
    alphaMask_(false)
{
    // Guess default lighting mode from pass name
    if (type == PASS_BASE || type == PASS_ALPHA || type == PASS_MATERIAL || type == PASS_DEFERRED)
        lightingMode_ = LIGHTING_PERVERTEX;
    else if (type == PASS_LIGHT || type == PASS_LITBASE || type == PASS_LITALPHA)
        lightingMode_ = LIGHTING_PERPIXEL;
}

Pass::~Pass()
{
}

void Pass::SetBlendMode(BlendMode mode)
{
    blendMode_ = mode;
}

void Pass::SetDepthTestMode(CompareMode mode)
{
    depthTestMode_ = mode;
}

void Pass::SetLightingMode(PassLightingMode mode)
{
    lightingMode_ = mode;
}

void Pass::SetDepthWrite(bool enable)
{
    depthWrite_ = enable;
}

void Pass::SetAlphaMask(bool enable)
{
    alphaMask_ = enable;
}

void Pass::SetVertexShader(const String& name)
{
    vertexShaderName_ = name;
    ReleaseShaders();
}

void Pass::SetPixelShader(const String& name)
{
    pixelShaderName_ = name;
    ReleaseShaders();
}

void Pass::ReleaseShaders()
{
    vertexShaders_.Clear();
    pixelShaders_.Clear();
}

void Pass::MarkShadersLoaded(unsigned frameNumber)
{
    shadersLoadedFrameNumber_ = frameNumber;
}

OBJECTTYPESTATIC(Technique);

Technique::Technique(Context* context) :
    Resource(context),
    isSM3_(false)
{
}

Technique::~Technique()
{
}

void Technique::RegisterObject(Context* context)
{
    context->RegisterFactory<Technique>();
}

bool Technique::Load(Deserializer& source)
{
    PROFILE(LoadTechnique);
    
    SharedPtr<XMLFile> xml(new XMLFile(context_));
    if (!xml->Load(source))
        return false;
    
    XMLElement rootElem = xml->GetRoot();
    if (rootElem.HasAttribute("sm3"))
        isSM3_ = rootElem.GetBool("sm3");
    
    XMLElement passElem = rootElem.GetChild("pass");
    while (passElem)
    {
        if (passElem.HasAttribute("name"))
        {
            StringHash nameHash(passElem.GetAttribute("name"));
            Pass* newPass = CreatePass(nameHash);
            
            if (passElem.HasAttribute("vs"))
                newPass->SetVertexShader(passElem.GetAttribute("vs"));
            
            if (passElem.HasAttribute("ps"))
                newPass->SetPixelShader(passElem.GetAttribute("ps"));
            
            if (passElem.HasAttribute("lighting"))
            {
                String lighting = passElem.GetAttributeLower("lighting");
                newPass->SetLightingMode((PassLightingMode)GetStringListIndex(lighting, lightingModeNames, LIGHTING_UNLIT));
            }
            
            if (passElem.HasAttribute("blend"))
            {
                String blend = passElem.GetAttributeLower("blend");
                newPass->SetBlendMode((BlendMode)GetStringListIndex(blend.CString(), blendModeNames, BLEND_REPLACE));
            }
            
            if (passElem.HasAttribute("depthtest"))
            {
                String depthTest = passElem.GetAttributeLower("depthtest");
                if (depthTest == "false")
                    newPass->SetDepthTestMode(CMP_ALWAYS);
                else
                    newPass->SetDepthTestMode((CompareMode)GetStringListIndex(depthTest, compareModeNames, CMP_LESS));
            }
            
            if (passElem.HasAttribute("depthwrite"))
                newPass->SetDepthWrite(passElem.GetBool("depthwrite"));
            
            if (passElem.HasAttribute("alphamask"))
                newPass->SetAlphaMask(passElem.GetBool("alphamask"));
        }
        else
            LOGERROR("Missing pass name");
        
        passElem = passElem.GetNext("pass");
    }
    
    // Rehash the pass map to ensure minimum load factor and fast queries
    passes_.Rehash(NextPowerOfTwo(passes_.Size()));
    
    // Calculate memory use
    unsigned memoryUse = sizeof(Technique);
    memoryUse += sizeof(HashMap<StringHash, SharedPtr<Pass> >) + passes_.Size() * sizeof(Pass);
    
    SetMemoryUse(memoryUse);
    return true;
}

void Technique::SetIsSM3(bool enable)
{
    isSM3_ = enable;
}

void Technique::ReleaseShaders()
{
    for (HashMap<StringHash, SharedPtr<Pass> >::Iterator i = passes_.Begin(); i != passes_.End(); ++i)
        i->second_->ReleaseShaders();
}

Pass* Technique::CreatePass(StringHash type)
{
    Pass* oldPass = GetPass(type);
    if (oldPass)
        return oldPass;
    
    SharedPtr<Pass> newPass(new Pass(type));
    passes_[type] = newPass;
    
    // Rehash the pass map to ensure minimum load factor and fast queries
    passes_.Rehash(NextPowerOfTwo(passes_.Size()));
    
    return newPass;
}

void Technique::RemovePass(StringHash type)
{
    passes_.Erase(type);
}

Pass* Technique::GetPass(StringHash type) const
{
    HashMap<StringHash, SharedPtr<Pass> >::ConstIterator i = passes_.Find(type);
    return i != passes_.End() ? i->second_ : (Pass*)0;
}

}
