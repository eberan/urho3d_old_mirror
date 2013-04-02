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
#include "BorderImage.h"
#include "InputEvents.h"
#include "ScrollBar.h"
#include "ScrollView.h"
#include "UIEvents.h"

#include "DebugNew.h"

namespace Urho3D
{

static const float STEP_FACTOR = 300.0f;

OBJECTTYPESTATIC(ScrollView);

ScrollView::ScrollView(Context* context) :
    UIElement(context),
    viewPosition_(IntVector2::ZERO),
    viewSize_(IntVector2::ZERO),
    viewPositionAttr_(IntVector2::ZERO),
    pageStep_(1.0f),
    scrollBarsAutoVisible_(true),
    ignoreEvents_(false),
    resizeContentWidth_(false)
{
    clipChildren_ = true;
    enabled_ = true;
    focusMode_ = FM_FOCUSABLE_DEFOCUSABLE;

    horizontalScrollBar_ = CreateChild<ScrollBar>();
    horizontalScrollBar_->SetInternal(true);
    horizontalScrollBar_->SetAlignment(HA_LEFT, VA_BOTTOM);
    horizontalScrollBar_->SetOrientation(O_HORIZONTAL);
    verticalScrollBar_ = CreateChild<ScrollBar>();
    verticalScrollBar_->SetInternal(true);
    verticalScrollBar_->SetAlignment(HA_RIGHT, VA_TOP);
    verticalScrollBar_->SetOrientation(O_VERTICAL);
    scrollPanel_ = CreateChild<BorderImage>();
    scrollPanel_->SetInternal(true);
    scrollPanel_->SetEnabled(true);
    scrollPanel_->SetClipChildren(true);

    SubscribeToEvent(horizontalScrollBar_, E_SCROLLBARCHANGED, HANDLER(ScrollView, HandleScrollBarChanged));
    SubscribeToEvent(horizontalScrollBar_, E_VISIBLECHANGED, HANDLER(ScrollView, HandleScrollBarVisibleChanged));
    SubscribeToEvent(verticalScrollBar_, E_SCROLLBARCHANGED, HANDLER(ScrollView, HandleScrollBarChanged));
    SubscribeToEvent(verticalScrollBar_, E_VISIBLECHANGED, HANDLER(ScrollView, HandleScrollBarVisibleChanged));
}

ScrollView::~ScrollView()
{
}

void ScrollView::RegisterObject(Context* context)
{
    context->RegisterFactory<ScrollView>();

    COPY_BASE_ATTRIBUTES(ScrollView, UIElement);
    REF_ACCESSOR_ATTRIBUTE(ScrollView, VAR_INTVECTOR2, "View Position", GetViewPosition, SetViewPositionAttr, IntVector2, IntVector2::ZERO, AM_FILE);
    ACCESSOR_ATTRIBUTE(ScrollView, VAR_FLOAT, "Scroll Step", GetScrollStep, SetScrollStep, float, 0.1f, AM_FILE);
    ACCESSOR_ATTRIBUTE(ScrollView, VAR_FLOAT, "Page Step", GetPageStep, SetPageStep, float, 1.0f, AM_FILE);
    ACCESSOR_ATTRIBUTE(ScrollView, VAR_BOOL, "Auto Show/Hide Scrollbars", GetScrollBarsAutoVisible, SetScrollBarsAutoVisible, bool, true, AM_FILE);
}

void ScrollView::ApplyAttributes()
{
    UIElement::ApplyAttributes();

    // Set the scrollbar orientations again and perform size update now that the style is known
    horizontalScrollBar_->SetOrientation(O_HORIZONTAL);
    verticalScrollBar_->SetOrientation(O_VERTICAL);

    // If the scroll panel has a child, it should be the content element, which has some special handling
    if (scrollPanel_->GetNumChildren())
        SetContentElement(scrollPanel_->GetChild(0));

    OnResize();

    // Reapply view position with proper content element and size
    SetViewPosition(viewPositionAttr_);
}

void ScrollView::OnWheel(int delta, int buttons, int qualifiers)
{
    if (delta > 0)
        verticalScrollBar_->StepBack();
    if (delta < 0)
        verticalScrollBar_->StepForward();
}

void ScrollView::OnKey(int key, int buttons, int qualifiers)
{
    switch (key)
    {
    case KEY_LEFT:
        if (horizontalScrollBar_->IsVisible())
        {
            if (qualifiers & QUAL_CTRL)
                horizontalScrollBar_->SetValue(0.0f);
            else
                horizontalScrollBar_->StepBack();
        }
        break;

    case KEY_RIGHT:
        if (horizontalScrollBar_->IsVisible())
        {
            if (qualifiers & QUAL_CTRL)
                horizontalScrollBar_->SetValue(horizontalScrollBar_->GetRange());
            else
                horizontalScrollBar_->StepForward();
        }
        break;

    case KEY_HOME:
        qualifiers |= QUAL_CTRL;
        // Fallthru

    case KEY_UP:
        if (verticalScrollBar_->IsVisible())
        {
            if (qualifiers & QUAL_CTRL)
                verticalScrollBar_->SetValue(0.0f);
            else
                verticalScrollBar_->StepBack();
        }
        break;

    case KEY_END:
        qualifiers |= QUAL_CTRL;
        // Fallthru

    case KEY_DOWN:
        if (verticalScrollBar_->IsVisible())
        {
            if (qualifiers & QUAL_CTRL)
                verticalScrollBar_->SetValue(verticalScrollBar_->GetRange());
            else
                verticalScrollBar_->StepForward();
        }
        break;

    case KEY_PAGEUP:
        if (verticalScrollBar_->IsVisible())
            verticalScrollBar_->ChangeValue(-pageStep_);
        break;

    case KEY_PAGEDOWN:
        if (verticalScrollBar_->IsVisible())
            verticalScrollBar_->ChangeValue(pageStep_);
        break;
    }
}

void ScrollView::OnResize()
{
    UpdatePanelSize();
    UpdateViewSize();

    // If scrollbar autovisibility is enabled, check whether scrollbars should be visible.
    // This may force another update of the panel size
    if (scrollBarsAutoVisible_)
    {
        ignoreEvents_ = true;
        horizontalScrollBar_->SetVisible(horizontalScrollBar_->GetRange() > M_EPSILON);
        verticalScrollBar_->SetVisible(verticalScrollBar_->GetRange() > M_EPSILON);
        ignoreEvents_ = false;

        UpdatePanelSize();
    }
}

void ScrollView::SetContentElement(UIElement* element)
{
    if (element == contentElement_)
        return;

    if (contentElement_)
    {
        scrollPanel_->RemoveChild(contentElement_);
        UnsubscribeFromEvent(contentElement_, E_RESIZED);
    }
    contentElement_ = element;
    if (contentElement_)
    {
        scrollPanel_->AddChild(contentElement_);
        SubscribeToEvent(contentElement_, E_RESIZED, HANDLER(ScrollView, HandleElementResized));
    }

    OnResize();
}

void ScrollView::SetViewPosition(const IntVector2& position)
{
    UpdateView(position);
    UpdateScrollBars();
}

void ScrollView::SetViewPosition(int x, int y)
{
    SetViewPosition(IntVector2(x, y));
}

void ScrollView::SetScrollBarsVisible(bool horizontal, bool vertical)
{
    scrollBarsAutoVisible_ = false;
    horizontalScrollBar_->SetVisible(horizontal);
    verticalScrollBar_->SetVisible(vertical);
}

void ScrollView::SetScrollBarsAutoVisible(bool enable)
{
    if (enable != scrollBarsAutoVisible_)
    {
        scrollBarsAutoVisible_ = enable;
        // Check whether scrollbars should be visible now
        if (enable)
            OnResize();
    }
}

void ScrollView::SetScrollStep(float step)
{
    horizontalScrollBar_->SetScrollStep(step);
    verticalScrollBar_->SetScrollStep(step);
}

void ScrollView::SetPageStep(float step)
{
    pageStep_ = Max(step, 0.0f);
}

float ScrollView::GetScrollStep() const
{
    return horizontalScrollBar_->GetScrollStep();
}

void ScrollView::SetViewPositionAttr(const IntVector2& value)
{
    viewPositionAttr_ = value;
    SetViewPosition(value);
}

void ScrollView::UpdatePanelSize()
{
    // Ignore events in case content element resizes itself along with the panel
    // (content element resize triggers our OnResize(), so it could lead to infinite recursion)
    ignoreEvents_ = true;

    IntVector2 panelSize = GetSize();
    if (verticalScrollBar_->IsVisible())
        panelSize.x_ -= verticalScrollBar_->GetWidth();
    if (horizontalScrollBar_->IsVisible())
        panelSize.y_ -= horizontalScrollBar_->GetHeight();

    scrollPanel_->SetSize(panelSize);
    horizontalScrollBar_->SetWidth(scrollPanel_->GetWidth());
    verticalScrollBar_->SetHeight(scrollPanel_->GetHeight());

    if (resizeContentWidth_ && contentElement_)
    {
        IntRect panelBorder = scrollPanel_->GetClipBorder();
        contentElement_->SetWidth(scrollPanel_->GetWidth() - panelBorder.left_ - panelBorder.right_);
        UpdateViewSize();
    }

    ignoreEvents_ = false;
}

void ScrollView::UpdateViewSize()
{
    IntVector2 size(IntVector2::ZERO);
    if (contentElement_)
        size = contentElement_->GetSize();
    IntRect panelBorder = scrollPanel_->GetClipBorder();

    viewSize_.x_ = Max(size.x_, scrollPanel_->GetWidth() - panelBorder.left_ - panelBorder.right_);
    viewSize_.y_ = Max(size.y_, scrollPanel_->GetHeight() - panelBorder.top_ - panelBorder.bottom_);

    UpdateView(viewPosition_);
    UpdateScrollBars();
}

void ScrollView::UpdateScrollBars()
{
    ignoreEvents_ = true;

    IntVector2 size = scrollPanel_->GetSize();
    IntRect panelBorder = scrollPanel_->GetClipBorder();
    size.x_ -= panelBorder.left_ + panelBorder.right_;
    size.y_ -= panelBorder.top_ + panelBorder.bottom_;

    if (size.x_ > 0 && viewSize_.x_ > 0)
    {
        float range = (float)viewSize_.x_ / (float)size.x_ - 1.0f;
        horizontalScrollBar_->SetRange(range);
        horizontalScrollBar_->SetValue((float)viewPosition_.x_ / (float)size.x_);
        horizontalScrollBar_->SetStepFactor(STEP_FACTOR / (float)size.x_);
    }
    if (size.y_ > 0 && viewSize_.y_ > 0)
    {
        float range = (float)viewSize_.y_ / (float)size.y_ - 1.0f;
        verticalScrollBar_->SetRange(range);
        verticalScrollBar_->SetValue((float)viewPosition_.y_ / (float)size.y_);
        verticalScrollBar_->SetStepFactor(STEP_FACTOR / (float)size.y_);
    }

    ignoreEvents_ = false;
}

void ScrollView::UpdateView(const IntVector2& position)
{
    IntVector2 oldPosition = viewPosition_;
    IntRect panelBorder = scrollPanel_->GetClipBorder();
    IntVector2 panelSize(scrollPanel_->GetWidth() - panelBorder.left_ - panelBorder.right_, scrollPanel_->GetHeight() -
        panelBorder.top_ - panelBorder.bottom_);

    viewPosition_.x_ = Clamp(position.x_, 0, viewSize_.x_ - panelSize.x_);
    viewPosition_.y_ = Clamp(position.y_, 0, viewSize_.y_ - panelSize.y_);
    scrollPanel_->SetChildOffset(IntVector2(-viewPosition_.x_ + panelBorder.left_, -viewPosition_.y_ + panelBorder.top_));

    if (viewPosition_ != oldPosition)
    {
        using namespace ViewChanged;

        VariantMap eventData;
        eventData[P_ELEMENT] = (void*)this;
        eventData[P_X] = viewPosition_.x_;
        eventData[P_Y] = viewPosition_.y_;
        SendEvent(E_VIEWCHANGED, eventData);
    }
}

void ScrollView::HandleScrollBarChanged(StringHash eventType, VariantMap& eventData)
{
    if (!ignoreEvents_)
    {
        IntVector2 size = scrollPanel_->GetSize();
        IntRect panelBorder = scrollPanel_->GetClipBorder();
        size.x_ -= panelBorder.left_ + panelBorder.right_;
        size.y_ -= panelBorder.top_ + panelBorder.bottom_;

        UpdateView(IntVector2(
            (int)(horizontalScrollBar_->GetValue() * (float)size.x_),
            (int)(verticalScrollBar_->GetValue() * (float)size.y_)
        ));
    }
}

void ScrollView::HandleScrollBarVisibleChanged(StringHash eventType, VariantMap& eventData)
{
    // Need to recalculate panel size when scrollbar visibility changes
    if (!ignoreEvents_)
        OnResize();
}

void ScrollView::HandleElementResized(StringHash eventType, VariantMap& eventData)
{
    if (!ignoreEvents_)
        OnResize();
}

}
