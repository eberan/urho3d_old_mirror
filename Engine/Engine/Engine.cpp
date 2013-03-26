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
#include "Log.h"
#include "Network.h"
#include "PackageFile.h"
#include "PhysicsWorld.h"
#include "ProcessUtils.h"
#include "Profiler.h"
#include "Renderer.h"
#include "ResourceCache.h"
#include "Scene.h"
#include "SceneEvents.h"
#include "Script.h"
#include "ScriptAPI.h"
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

OBJECTTYPESTATIC(Engine);

Engine::Engine(Context* context) :
    Object(context),
    timeStep_(0.0f),
    minFps_(10),
    #if defined(ANDROID) || defined(IOS)
    maxFps_(60),
    maxInactiveFps_(10),
    pauseMinimized_(true),
    #else
    maxFps_(200),
    maxInactiveFps_(60),
    pauseMinimized_(false),
    #endif
    initialized_(false),
    exiting_(false),
    headless_(false),
    audioPaused_(false)
{
}

Engine::~Engine()
{
    // Remove subsystems that use SDL in reverse order of construction
    context_->RemoveSubsystem<Audio>();
    context_->RemoveSubsystem<UI>();
    context_->RemoveSubsystem<Input>();
    context_->RemoveSubsystem<Renderer>();
    context_->RemoveSubsystem<Graphics>();
}

bool Engine::Initialize(const String& windowTitle, const String& logName, const Vector<String>& arguments, void* externalWindow)
{
    if (initialized_)
        return true;
    
    String renderPath;
    int width = 0;
    int height = 0;
    int multiSample = 1;
    int buffer = 100;
    int mixRate = 44100;
    bool fullscreen = true;
    bool resizable = false;
    bool vsync = false;
    bool tripleBuffer = false;
    bool forceSM2 = false;
    bool shadows = true;
    bool lqShadows = false;
    bool sound = true;
    bool stereo = true;
    bool interpolation = true;
    bool threads = true;
    int logLevel = -1;
    bool quiet = false;
    
    for (unsigned i = 0; i < arguments.Size(); ++i)
    {
        if (arguments[i][0] == '-' && arguments[i].Length() >= 2)
        {
            String argument = arguments[i].Substring(1).ToLower();
            
            if (argument == "headless")
                headless_ = true;
            else if (argument.Substring(0, 3) == "log")
            {
                argument = argument.Substring(3).ToUpper();
                for (logLevel = sizeof(levelPrefixes) / sizeof(String) - 1; logLevel > -1; --logLevel)
                {
                    if (argument == levelPrefixes[logLevel])
                        break;
                }
            }
            else if (argument == "nolimit")
                SetMaxFps(0);
            else if (argument == "nosound")
                sound = false;
            else if (argument == "noip")
                interpolation = false;
            else if (argument == "mono")
                stereo = false;
            else if (argument == "prepass")
                renderPath = "RenderPaths/Prepass.xml";
            else if (argument == "deferred")
                renderPath = "RenderPaths/Deferred.xml";
            else if (argument == "noshadows")
                shadows = false;
            else if (argument == "lqshadows")
                lqShadows = true;
            else if (argument == "nothreads")
                threads = false;
            else if (argument == "sm2")
                forceSM2 = true;
            else
            {
                int value;
                if (argument.Length() > 1)
                    value = ToInt(argument.Substring(1));
                
                switch (tolower(argument[0]))
                {
                case 'x':
                    width = value;
                    break;
                    
                case 'y':
                    height = value;
                    break;
                
                case 'm':
                    multiSample = value;
                    break;
                    
                case 'b':
                    buffer = value;
                    break;
                    
                case 'r':
                    mixRate = value;
                    break;
                    
                case 'v':
                    vsync = true;
                    break;
                    
                case 't':
                    tripleBuffer = true;
                    break;
                    
                case 'w':
                    fullscreen = false;
                    break;
                        
                case 's':
                    resizable = true;
                    break;

                case 'q':
                    quiet = true;
                    break;
                }
            }
        }
    }
    
    // Register object factories and attributes first, then subsystems
    RegisterObjects();
    RegisterSubsystems();
    
    // Start logging
    Log* log = GetSubsystem<Log>();
    if (log)
    {
        if (logLevel != -1)
            log->SetLevel(logLevel);
        log->SetQuiet(quiet);
        log->Open(logName);
    }
    
    // Set maximally accurate low res timer
    GetSubsystem<Time>()->SetTimerPeriod(1);
    
    // Set amount of worker threads according to the available physical CPU cores. Using also hyperthreaded cores results in
    // unpredictable extra synchronization overhead. Also reserve one core for the main thread
    unsigned numThreads = threads ? GetNumPhysicalCPUs() - 1 : 0;
    if (numThreads)
    {
        GetSubsystem<WorkQueue>()->CreateThreads(numThreads);
        
        LOGINFO(ToString("Created %u worker thread%s", numThreads, numThreads > 1 ? "s" : ""));
    }
    
    // Add default resource paths: CoreData package or directory, Data package or directory
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    FileSystem* fileSystem = GetSubsystem<FileSystem>();
    String exePath = fileSystem->GetProgramDir();
    
    if (fileSystem->FileExists(exePath + "CoreData.pak"))
    {
        SharedPtr<PackageFile> package(new PackageFile(context_));
        package->Open(exePath + "CoreData.pak");
        cache->AddPackageFile(package);
    }
    else if (fileSystem->DirExists(exePath + "CoreData"))
        cache->AddResourceDir(exePath + "CoreData");
    
    if (fileSystem->FileExists(exePath + "Data.pak"))
    {
        SharedPtr<PackageFile> package(new PackageFile(context_));
        package->Open(exePath + "Data.pak");
        cache->AddPackageFile(package);
    }
    else if (fileSystem->DirExists(exePath + "Data"))
        cache->AddResourceDir(exePath + "Data");
    
    // Initialize graphics & audio output
    if (!headless_)
    {
        Graphics* graphics = GetSubsystem<Graphics>();
        Renderer* renderer = GetSubsystem<Renderer>();
        
        if (externalWindow)
            graphics->SetExternalWindow(externalWindow);
        graphics->SetForceSM2(forceSM2);
        graphics->SetWindowTitle(windowTitle);
        if (!graphics->SetMode(width, height, fullscreen, resizable, vsync, tripleBuffer, multiSample))
            return false;
        
        if (!renderPath.Empty())
            renderer->SetDefaultRenderPath(cache->GetResource<XMLFile>(renderPath));
        renderer->SetDrawShadows(shadows);
        if (shadows && lqShadows)
            renderer->SetShadowQuality(SHADOWQUALITY_LOW_16BIT);
        
        if (sound)
            GetSubsystem<Audio>()->SetMode(buffer, mixRate, stereo, interpolation);
    }
    
    // Init FPU state of main thread
    InitFPU();
    
    frameTimer_.Reset();
    
    initialized_ = true;
    return true;
}

bool Engine::InitializeScripting()
{
    // Check if scripting already initialized
    if (GetSubsystem<Script>())
        return true;
    
    RegisterScriptLibrary(context_);
    context_->RegisterSubsystem(new Script(context_));
   
    {
        PROFILE(RegisterScriptAPI);
        
        asIScriptEngine* engine = GetSubsystem<Script>()->GetScriptEngine();
        RegisterMathAPI(engine);
        RegisterCoreAPI(engine);
        RegisterIOAPI(engine);
        RegisterResourceAPI(engine);
        RegisterSceneAPI(engine);
        RegisterGraphicsAPI(engine);
        RegisterInputAPI(engine);
        RegisterAudioAPI(engine);
        RegisterUIAPI(engine);
        RegisterNetworkAPI(engine);
        RegisterPhysicsAPI(engine);
        RegisterScriptAPI(engine);
        RegisterEngineAPI(engine);
    }
    
    return true;
}

void Engine::RunFrame()
{
    assert(initialized_ && !exiting_);
    
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

void Engine::Exit()
{
    Graphics* graphics = GetSubsystem<Graphics>();
    if (graphics)
        graphics->Close();
    
    exiting_ = true;
}

void Engine::DumpProfilingData()
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

void Engine::RegisterObjects()
{
    RegisterResourceLibrary(context_);
    RegisterSceneLibrary(context_);
    RegisterNetworkLibrary(context_);
    RegisterGraphicsLibrary(context_);
    RegisterAudioLibrary(context_);
    RegisterUILibrary(context_);
    RegisterPhysicsLibrary(context_);
    
    // In debug mode, check that all factory created objects can be created without crashing
    #ifdef _DEBUG
    const HashMap<ShortStringHash, SharedPtr<ObjectFactory> >& factories = context_->GetObjectFactories();
    for (HashMap<ShortStringHash, SharedPtr<ObjectFactory> >::ConstIterator i = factories.Begin(); i != factories.End(); ++i)
        SharedPtr<Object> object = i->second_->CreateObject();
    #endif
}

void Engine::RegisterSubsystems()
{
    // Register self as a subsystem
    context_->RegisterSubsystem(this);
    
    // Create and register the rest of the subsystems
    context_->RegisterSubsystem(new Time(context_));
    context_->RegisterSubsystem(new WorkQueue(context_));
    #ifdef ENABLE_PROFILING
    context_->RegisterSubsystem(new Profiler(context_));
    #endif
    context_->RegisterSubsystem(new FileSystem(context_));
    context_->RegisterSubsystem(new ResourceCache(context_));
    context_->RegisterSubsystem(new Network(context_));
    
    if (!headless_)
    {
        context_->RegisterSubsystem(new Graphics(context_));
        context_->RegisterSubsystem(new Renderer(context_));
    }
    
    context_->RegisterSubsystem(new Input(context_));
    context_->RegisterSubsystem(new UI(context_));
    context_->RegisterSubsystem(new Audio(context_));
    #ifdef ENABLE_LOGGING
    context_->RegisterSubsystem(new Log(context_));
    #endif
}

}
