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
#include "Audio.h"
#include "Console.h"
#include "Context.h"
#include "CoreEvents.h"
#include "DebugHud.h"
#include "Engine.h"
#include "FileSystem.h"
#include "Graphics.h"
#include "Input.h"
#include "InputEvents.h"
#include "Log.h"
#include "Navigation.h"
#include "Network.h"
#include "PackageFile.h"
#include "PhysicsWorld.h"
#include "ProcessUtils.h"
#include "Profiler.h"
#include "Renderer.h"
#include "ResourceCache.h"
#include "Scene.h"
#include "SceneEvents.h"
#include "StringUtils.h"
#include "UI.h"
#include "WorkQueue.h"
#include "XMLFile.h"

#include "DebugNew.h"

#if defined(_MSC_VER) && defined(_DEBUG)
// From dbgint.h
#define nNoMansLandSize 4

typedef struct _CrtMemBlockHeader
{
    struct _CrtMemBlockHeader* pBlockHeaderNext;
    struct _CrtMemBlockHeader* pBlockHeaderPrev;
    char* szFileName;
    int nLine;
    size_t nDataSize;
    int nBlockUse;
    long lRequest;
    unsigned char gap[nNoMansLandSize];
} _CrtMemBlockHeader;
#endif

namespace Urho3D
{

extern const char* logLevelPrefixes[];

Engine::Engine(Context* context) :
    Object(context),
    timeStep_(0.0f),
    minFps_(10),
    #if defined(ANDROID) || defined(IOS) || defined(RASPI)
    maxFps_(60),
    maxInactiveFps_(10),
    pauseMinimized_(true),
    #else
    maxFps_(200),
    maxInactiveFps_(60),
    pauseMinimized_(false),
    #endif
    autoExit_(true),
    initialized_(false),
    exiting_(false),
    headless_(false),
    audioPaused_(false)
{
    // Register self as a subsystem
    context_->RegisterSubsystem(this);
    
    // Create subsystems which do not depend on engine initialization or startup parameters
    context_->RegisterSubsystem(new Time(context_));
    context_->RegisterSubsystem(new WorkQueue(context_));
    #ifdef ENABLE_PROFILING
    context_->RegisterSubsystem(new Profiler(context_));
    #endif
    context_->RegisterSubsystem(new FileSystem(context_));
    #ifdef ENABLE_LOGGING
    context_->RegisterSubsystem(new Log(context_));
    #endif
    context_->RegisterSubsystem(new ResourceCache(context_));
    context_->RegisterSubsystem(new Network(context_));
    context_->RegisterSubsystem(new Input(context_));
    context_->RegisterSubsystem(new Audio(context_));
    context_->RegisterSubsystem(new UI(context_));
    
    // Register object factories for libraries which are not automatically registered along with subsystem creation
    RegisterSceneLibrary(context_);
    RegisterPhysicsLibrary(context_);
    RegisterNavigationLibrary(context_);
    
    SubscribeToEvent(E_EXITREQUESTED, HANDLER(Engine, HandleExitRequested));
}

Engine::~Engine()
{
}

bool Engine::Initialize(const VariantMap& parameters)
{
    if (initialized_)
        return true;
    
    PROFILE(InitEngine);
    
    // Set headless mode
    headless_ = GetParameter(parameters, "Headless", false).GetBool();
    
    // Register the rest of the subsystems
    if (!headless_)
    {
        context_->RegisterSubsystem(new Graphics(context_));
        context_->RegisterSubsystem(new Renderer(context_));
    }
    else
    {
        // Register graphics library objects explicitly in headless mode to allow them to work without using actual GPU resources
        RegisterGraphicsLibrary(context_);
    }
    
    // In debug mode, check now that all factory created objects can be created without crashing
    #ifdef _DEBUG
    const HashMap<ShortStringHash, SharedPtr<ObjectFactory> >& factories = context_->GetObjectFactories();
    for (HashMap<ShortStringHash, SharedPtr<ObjectFactory> >::ConstIterator i = factories.Begin(); i != factories.End(); ++i)
        SharedPtr<Object> object = i->second_->CreateObject();
    #endif
    
    // Start logging
    Log* log = GetSubsystem<Log>();
    if (log)
    {
        if (HasParameter(parameters, "LogLevel"))
            log->SetLevel(GetParameter(parameters, "LogLevel").GetInt());
        log->SetQuiet(GetParameter(parameters, "LogQuiet", false).GetBool());
        log->Open(GetParameter(parameters, "LogName", "Urho3D.log").GetString());
    }
    
    // Set maximally accurate low res timer
    GetSubsystem<Time>()->SetTimerPeriod(1);
    
    // Configure max FPS
    if (GetParameter(parameters, "FrameLimiter", true) == false)
        SetMaxFps(0);
    
    // Set amount of worker threads according to the available physical CPU cores. Using also hyperthreaded cores results in
    // unpredictable extra synchronization overhead. Also reserve one core for the main thread
    unsigned numThreads = GetParameter(parameters, "WorkerThreads", true).GetBool() ? GetNumPhysicalCPUs() - 1 : 0;
    if (numThreads)
    {
        GetSubsystem<WorkQueue>()->CreateThreads(numThreads);
        
        LOGINFO(ToString("Created %u worker thread%s", numThreads, numThreads > 1 ? "s" : ""));
    }
    
    // Add resource paths
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    FileSystem* fileSystem = GetSubsystem<FileSystem>();
    String exePath = fileSystem->GetProgramDir();
    
    Vector<String> resourcePaths = GetParameter(parameters, "ResourcePaths", "CoreData;Data").GetString().Split(';');
    Vector<String> resourcePackages = GetParameter(parameters, "ResourcePackages").GetString().Split(';');
    
    for (unsigned i = 0; i < resourcePaths.Size(); ++i)
    {
        bool success = false;
        
        // If path is not absolute, prefer to add it as a package if possible
        if (!IsAbsolutePath(resourcePaths[i]))
        {
            String packageName = exePath + resourcePaths[i] + ".pak";
            if (fileSystem->FileExists(packageName))
            {
                SharedPtr<PackageFile> package(new PackageFile(context_));
                if (package->Open(packageName))
                {
                    cache->AddPackageFile(package);
                    success = true;
                }
            }
            
            if (!success)
            {
                String pathName = exePath + resourcePaths[i];
                if (fileSystem->DirExists(pathName))
                    success = cache->AddResourceDir(pathName);
            }
        }
        else
        {
            String pathName = resourcePaths[i];
            if (fileSystem->DirExists(pathName))
                success = cache->AddResourceDir(pathName);
        }
        
        if (!success)
        {
            LOGERROR("Failed to add resource path " + resourcePaths[i]);
            return false;
        }
    }
    
    // Then add specified packages
    for (unsigned i = 0; i < resourcePackages.Size(); ++i)
    {
        bool success = false;
        
        String packageName = exePath + resourcePackages[i];
        if (fileSystem->FileExists(packageName))
        {
            SharedPtr<PackageFile> package(new PackageFile(context_));
            if (package->Open(packageName))
            {
                cache->AddPackageFile(package);
                success = true;
            }
        }
        
        if (!success)
        {
            LOGERROR("Failed to add resource package " + resourcePackages[i]);
            return false;
        }
    }
    

    // Initialize graphics & audio output
    if (!headless_)
    {
        Graphics* graphics = GetSubsystem<Graphics>();
        Renderer* renderer = GetSubsystem<Renderer>();
        
        if (HasParameter(parameters, "ExternalWindow"))
            graphics->SetExternalWindow(GetParameter(parameters, "ExternalWindow").GetPtr());
        graphics->SetForceSM2(GetParameter(parameters, "ForceSM2", false).GetBool());
        graphics->SetWindowTitle(GetParameter(parameters, "WindowTitle", "Urho3D").GetString());
        if (!graphics->SetMode(
            GetParameter(parameters, "WindowWidth", 0).GetInt(),
            GetParameter(parameters, "WindowHeight", 0).GetInt(),
            GetParameter(parameters, "FullScreen", true).GetBool(),
            GetParameter(parameters, "WindowResizable", false).GetBool(),
            GetParameter(parameters, "VSync", false).GetBool(),
            GetParameter(parameters, "TripleBuffer", false).GetBool(),
            GetParameter(parameters, "MultiSample", 1).GetInt()
        ))
            return false;
        
        if (HasParameter(parameters, "RenderPath"))
            renderer->SetDefaultRenderPath(cache->GetResource<XMLFile>(GetParameter(parameters, "RenderPath").GetString()));
        renderer->SetDrawShadows(GetParameter(parameters, "Shadows", true).GetBool());
        if (renderer->GetDrawShadows() && GetParameter(parameters, "LowQualityShadows", false).GetBool())
            renderer->SetShadowQuality(SHADOWQUALITY_LOW_16BIT);
        
        if (GetParameter(parameters, "Sound", true).GetBool())
        {
            GetSubsystem<Audio>()->SetMode(
                GetParameter(parameters, "SoundBuffer", 100).GetInt(),
                GetParameter(parameters, "SoundMixRate", 44100).GetInt(),
                GetParameter(parameters, "SoundStereo", true).GetBool(),
                GetParameter(parameters, "SoundInterpolation", true).GetBool()
            );
        }
    }
    
    // Init FPU state of main thread
    InitFPU();
    
    frameTimer_.Reset();
    
    initialized_ = true;
    return true;
}

void Engine::RunFrame()
{
    assert(initialized_);
    
    // If graphics subsystem exists, but does not have a window open, assume we should exit
    Graphics* graphics = GetSubsystem<Graphics>();
    if (graphics && !graphics->IsInitialized())
        exiting_ = true;
    
    if (exiting_)
        return;
    
    // Note: there is a minimal performance cost to looking up subsystems (uses a hashmap); if they would be looked up several
    // times per frame it would be better to cache the pointers
    Time* time = GetSubsystem<Time>();
    Input* input = GetSubsystem<Input>();
    Audio* audio = GetSubsystem<Audio>();
    
    time->BeginFrame(timeStep_);
    
    // If pause when minimized -mode is in use, stop updates and audio as necessary
    if (pauseMinimized_ && input->IsMinimized())
    {
        if (audio->IsPlaying())
        {
            audio->Stop();
            audioPaused_ = true;
        }
    }
    else
    {
        // Only unpause when it was paused by the engine
        if (audioPaused_)
        {
            audio->Play();
            audioPaused_ = false;
        }
        
        Update();
    }
    
    Render();
    ApplyFrameLimit();
    
    time->EndFrame();
}

Console* Engine::CreateConsole()
{
    if (headless_ || !initialized_)
        return 0;
    
    // Return existing console if possible
    Console* console = GetSubsystem<Console>();
    if (!console)
    {
        console = new Console(context_);
        context_->RegisterSubsystem(console);
    }
    
    return console;
}

DebugHud* Engine::CreateDebugHud()
{
    if (headless_ || !initialized_)
        return 0;
    
     // Return existing debug HUD if possible
    DebugHud* debugHud = GetSubsystem<DebugHud>();
    if (!debugHud)
    {
        debugHud = new DebugHud(context_);
        context_->RegisterSubsystem(debugHud);
    }
    
    return debugHud;
}

void Engine::SetMinFps(int fps)
{
    minFps_ = Max(fps, 0);
}

void Engine::SetMaxFps(int fps)
{
    maxFps_ = Max(fps, 0);
}

void Engine::SetMaxInactiveFps(int fps)
{
    maxInactiveFps_ = Max(fps, 0);
}

void Engine::SetPauseMinimized(bool enable)
{
    pauseMinimized_ = enable;
}

void Engine::SetAutoExit(bool enable)
{
    autoExit_ = enable;
}

void Engine::Exit()
{
    Graphics* graphics = GetSubsystem<Graphics>();
    if (graphics)
        graphics->Close();
    
    exiting_ = true;
}

void Engine::DumpProfiler()
{
    Profiler* profiler = GetSubsystem<Profiler>();
    if (profiler)
        LOGRAW(profiler->GetData(true, true) + "\n");
}

void Engine::DumpResources()
{
    #ifdef ENABLE_LOGGING
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    const HashMap<ShortStringHash, ResourceGroup>& resourceGroups = cache->GetAllResources();
    LOGRAW("\n");
    
    for (HashMap<ShortStringHash, ResourceGroup>::ConstIterator i = resourceGroups.Begin();
        i != resourceGroups.End(); ++i)
    {
        unsigned num = i->second_.resources_.Size();
        unsigned memoryUse = i->second_.memoryUse_;
        
        if (num)
        {
            LOGRAW("Resource type " + i->second_.resources_.Begin()->second_->GetTypeName() +
                ": count " + String(num) + " memory use " + String(memoryUse) + "\n");
        }
    }
    
    LOGRAW("Total memory use of all resources " + String(cache->GetTotalMemoryUse()) + "\n\n");
    #endif
}

void Engine::DumpMemory()
{
    #ifdef ENABLE_LOGGING
    #if defined(_MSC_VER) && defined(_DEBUG)
    _CrtMemState state;
    _CrtMemCheckpoint(&state);
    _CrtMemBlockHeader* block = state.pBlockHeader;
    unsigned total = 0;
    unsigned blocks = 0;
    
    for (;;)
    {
        if (block && block->pBlockHeaderNext)
            block = block->pBlockHeaderNext;
        else
            break;
    }
    
    while (block)
    {
        if (block->nBlockUse > 0)
        {
            if (block->szFileName)
                LOGRAW("Block " + String((int)block->lRequest) + ": " + String(block->nDataSize) + " bytes, file " + String(block->szFileName) + " line " + String(block->nLine) + "\n");
            else
                LOGRAW("Block " + String((int)block->lRequest) + ": " + String(block->nDataSize) + " bytes\n");
            
            total += block->nDataSize;
            ++blocks;
        }
        block = block->pBlockHeaderPrev;
    }
    
    LOGRAW("Total allocated memory " + String(total) + " bytes in " + String(blocks) + " blocks\n\n");
    #else
    LOGRAW("DumpMemory() supported on MSVC debug mode only\n\n");
    #endif
    #endif
}

void Engine::Update()
{
    PROFILE(Update);
    
    // Logic update event
    using namespace Update;
    
    VariantMap eventData;
    eventData[P_TIMESTEP] = timeStep_;
    SendEvent(E_UPDATE, eventData);
    
    // Logic post-update event
    SendEvent(E_POSTUPDATE, eventData);
    
    // Rendering update event
    SendEvent(E_RENDERUPDATE, eventData);
    
    // Post-render update event
    SendEvent(E_POSTRENDERUPDATE, eventData);
}

void Engine::Render()
{
    PROFILE(Render);
    
    // Do not render if device lost
    Graphics* graphics = GetSubsystem<Graphics>();
    if (!graphics || !graphics->BeginFrame())
        return;
    
    GetSubsystem<Renderer>()->Render();
    GetSubsystem<UI>()->Render();
    graphics->EndFrame();
}

void Engine::ApplyFrameLimit()
{
    if (!initialized_)
        return;
    
    int maxFps = maxFps_;
    Input* input = GetSubsystem<Input>();
    if (input && !input->HasFocus())
        maxFps = Min(maxInactiveFps_, maxFps);
    
    long long elapsed = 0;
    
    // Perform waiting loop if maximum FPS set
    if (maxFps)
    {
        PROFILE(ApplyFrameLimit);
        
        long long targetMax = 1000000LL / maxFps;
        
        for (;;)
        {
            elapsed = frameTimer_.GetUSec(false);
            if (elapsed >= targetMax)
                break;
            
            // Sleep if 1 ms or more off the frame limiting goal
            if (targetMax - elapsed >= 1000LL)
            {
                unsigned sleepTime = (unsigned)((targetMax - elapsed) / 1000LL);
                Time::Sleep(sleepTime);
            }
        }
    }
    
    elapsed = frameTimer_.GetUSec(true);
    
    // If FPS lower than minimum, clamp elapsed time
    if (minFps_)
    {
        long long targetMin = 1000000LL / minFps_;
        if (elapsed > targetMin)
            elapsed = targetMin;
    }
    
    timeStep_ = elapsed / 1000000.0f;
}

VariantMap Engine::ParseParameters(const Vector<String>& arguments)
{
    VariantMap ret;
    
    for (unsigned i = 0; i < arguments.Size(); ++i)
    {
        if (arguments[i][0] == '-' && arguments[i].Length() >= 2)
        {
            String argument = arguments[i].Substring(1).ToLower();
            
            if (argument == "headless")
                ret["Headless"] = true;
            else if (argument.Substring(0, 3) == "log")
            {
                argument = argument.Substring(3);
                int logLevel = GetStringListIndex(argument.CString(), logLevelPrefixes, -1);
                if (logLevel != -1)
                    ret["LogLevel"] = logLevel;
            }
            else if (argument == "nolimit")
                ret["FrameLimiter"] = false;
            else if (argument == "nosound")
                ret["Sound"] = false;
            else if (argument == "noip")
                ret["SoundInterpolation"] = false;
            else if (argument == "mono")
                ret["SoundStereo"] = false;
            else if (argument == "prepass")
                ret["RenderPath"] = "RenderPaths/Prepass.xml";
            else if (argument == "deferred")
                ret["RenderPath"] = "RenderPaths/Deferred.xml";
            else if (argument == "noshadows")
                ret["Shadows"] = false;
            else if (argument == "lqshadows")
                ret["LowQualityShadows"] = true;
            else if (argument == "nothreads")
                ret["WorkerThreads"] = false;
            else if (argument == "sm2")
                ret["ForceSM2"] = true;
            else
            {
                int value;
                if (argument.Length() > 1)
                    value = ToInt(argument.Substring(1));
                
                switch (tolower(argument[0]))
                {
                case 'x':
                    ret["WindowWidth"] = value;
                    break;
                    
                case 'y':
                    ret["WindowHeight"] = value;
                    break;
                
                case 'm':
                    ret["MultiSample"] = value;
                    break;
                    
                case 'b':
                    ret["SoundBuffer"] = value;
                    break;
                    
                case 'r':
                    ret["SoundMixRate"] = value;
                    break;
                    
                case 'v':
                    ret["VSync"] = true;
                    break;
                    
                case 't':
                    ret["TripleBuffer"] = true;
                    break;
                    
                case 'w':
                    ret["FullScreen"] = false;
                    break;
                        
                case 's':
                    ret["WindowResizable"] = true;
                    break;
                    
                case 'q':
                    ret["LogQuiet"] = true;
                    break;
                    
                case 'p':
                    ret["ResourcePaths"] = arguments[i].Substring(2);
                    break;
                }
            }
        }
    }
    
    return ret;
}

bool Engine::HasParameter(const VariantMap& parameters, const String& parameter)
{
    ShortStringHash nameHash(parameter);
    return parameters.Find(nameHash) != parameters.End();
}

const Variant& Engine::GetParameter(const VariantMap& parameters, const String& parameter, const Variant& defaultValue)
{
    ShortStringHash nameHash(parameter);
    VariantMap::ConstIterator i = parameters.Find(nameHash);
    return i != parameters.End() ? i->second_ : defaultValue;
}

void Engine::HandleExitRequested(StringHash eventType, VariantMap& eventData)
{
    if (autoExit_)
        Exit();
}

}
