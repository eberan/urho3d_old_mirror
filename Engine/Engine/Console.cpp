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
#include "BorderImage.h"
#include "Console.h"
#include "Engine.h"
#include "Font.h"
#include "LineEdit.h"
#include "Renderer.h"
#include "RendererEvents.h"
#include "ResourceCache.h"
#include "ScriptEngine.h"
#include "StringUtils.h"
#include "Text.h"
#include "UI.h"
#include "UIEvents.h"

#include "DebugNew.h"

static const int DEFAULT_CONSOLE_ROWS = 16;

Console::Console(Engine* engine) :
    mEngine(engine)
{
    LOGINFO("Console created");
    
    if (!mEngine)
        EXCEPTION("Null Engine for Console");
    
    UIElement* uiRoot = mEngine->getUIRoot();
    if (uiRoot)
    {
        Log* log = getLog();
        if (log)
            log->addListener(this);
        
        mBackground = new BorderImage();
        mBackground->setFixedWidth(uiRoot->getWidth());
        mBackground->setBringToBack(false);
        mBackground->setClipChildren(true);
        mBackground->setEnabled(true);
        mBackground->setVisible(false); // Hide by default
        mBackground->setPriority(200); // Show on top of the debug HUD
        mBackground->setLayout(LM_VERTICAL);
        
        mLineEdit = new LineEdit();
        mLineEdit->setFocusMode(FM_FOCUSABLE); // Do not allow defocus with ESC
        mBackground->addChild(mLineEdit);
        
        uiRoot->addChild(mBackground);
        
        setNumRows(DEFAULT_CONSOLE_ROWS);
        updateElements();
        
        subscribeToEvent(mLineEdit, EVENT_TEXTFINISHED, EVENT_HANDLER(Console, handleTextFinished));
        subscribeToEvent(EVENT_WINDOWRESIZED, EVENT_HANDLER(Console, handleWindowResized));
    }
}

Console::~Console()
{
    Log* log = getLog();
    if (log)
        log->removeListener(this);
    
    UIElement* uiRoot = mEngine->getUIRoot();
    if (uiRoot)
        uiRoot->removeChild(mBackground);
    
    LOGINFO("Console shut down");
}

void Console::write(const std::string& message)
{
    if (!mRows.size())
        return;
    // If the rows are not fully initialized yet, do not write the message
    if (!mRows[mRows.size() - 1])
        return;
    
    // Be prepared for possible multi-line messages
    std::vector<std::string> rows = split(message, '\n');
    
    for (unsigned i = 0; i < rows.size(); ++i)
    {
        for (int j = 0; j < (int)mRows.size() - 1; ++j)
            mRows[j]->setText(mRows[j + 1]->getText());
        
        mRows[mRows.size() - 1]->setText(rows[i]);
    }
}

void Console::setStyle(XMLFile* style)
{
    if ((!style) || (!mEngine) || (!mBackground) || (!mLineEdit))
        return;
    
    mStyle = style;
    ResourceCache* cache = mEngine->getResourceCache();
    XMLElement backgroundElem = UIElement::getStyleElement(style, "ConsoleBackground");
    if (backgroundElem)
        mBackground->setStyle(backgroundElem, cache);
    XMLElement textElem = UIElement::getStyleElement(style, "ConsoleText");
    if (textElem)
    {
        for (unsigned i = 0; i < mRows.size(); ++i)
            mRows[i]->setStyle(textElem, cache);
    }
    XMLElement lineEditElem = UIElement::getStyleElement(style, "ConsoleLineEdit");
    if (lineEditElem)
        mLineEdit->setStyle(lineEditElem, cache);
    
    updateElements();
}

void Console::setVisible(bool enable)
{
    if (!mBackground)
        return;
    
    mBackground->setVisible(enable);
    if (enable)
        mEngine->getUI()->setFocusElement(mLineEdit);
    else
        mLineEdit->setFocus(false);
}

void Console::toggle()
{
    setVisible(!isVisible());
}

void Console::setNumRows(unsigned rows)
{
    if ((!mBackground) || (!rows))
        return;
    
    mBackground->removeAllChildren();
    
    mRows.resize(rows);
    for (unsigned i = 0; i < mRows.size(); ++i)
    {
        if (!mRows[i])
        {
            mRows[i] = new Text();
            XMLElement textElem = UIElement::getStyleElement(mStyle, "ConsoleText");
            if (textElem)
                mRows[i]->setStyle(textElem, mEngine->getResourceCache());
        }
        mBackground->addChild(mRows[i]);
    }
    mBackground->addChild(mLineEdit);
    
    updateElements();
}

void Console::updateElements()
{
    int width = mEngine->getRenderer()->getWidth();
    mLineEdit->setFixedHeight(mLineEdit->getTextElement()->getRowHeight());
    mBackground->setFixedWidth(width);
}

bool Console::isVisible() const
{
    if (!mBackground)
        return false;
    return mBackground->isVisible();
}

void Console::handleTextFinished(StringHash eventType, VariantMap& eventData)
{
    using namespace TextFinished;
    
    std::string line = mLineEdit->getText();
    ScriptEngine* scriptEngine = mEngine->getScriptEngine();
    if (scriptEngine)
        scriptEngine->execute(line);
    mLineEdit->setText(std::string());
}

void Console::handleWindowResized(StringHash eventType, VariantMap& eventData)
{
    updateElements();
}
