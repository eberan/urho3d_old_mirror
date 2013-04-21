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
#include "Font.h"
#include "Log.h"
#include "Profiler.h"
#include "ResourceCache.h"
#include "StringUtils.h"
#include "Text.h"
#include "Texture2D.h"

#include "DebugNew.h"

namespace Urho3D
{

static const float MIN_ROW_SPACING = 0.5f;

extern const char* horizontalAlignments[];

struct GlyphLocation
{
    int x_;
    int y_;
    const FontGlyph* glyph_;

    GlyphLocation(int x, int y, const FontGlyph* glyph) :
        x_(x),
        y_(y),
        glyph_(glyph)
    {
    }
};

OBJECTTYPESTATIC(Text);

Text::Text(Context* context) :
    UIElement(context),
    fontSize_(DEFAULT_FONT_SIZE),
    textAlignment_(HA_LEFT),
    rowSpacing_(1.0f),
    wordWrap_(false),
    selectionStart_(0),
    selectionLength_(0),
    selectionColor_(Color::TRANSPARENT),
    hoverColor_(Color::TRANSPARENT),
    rowHeight_(0)
{
    // By default Text does not derive opacity from parent elements
    useDerivedOpacity_ = false;
}

Text::~Text()
{
}

void Text::RegisterObject(Context* context)
{
    context->RegisterFactory<Text>();

    COPY_BASE_ATTRIBUTES(Text, UIElement);
    UPDATE_ATTRIBUTE_DEFAULT_VALUE(Text, "Use Derived Opacity", false);
    ACCESSOR_ATTRIBUTE(Text, VAR_RESOURCEREF, "Font", GetFontAttr, SetFontAttr, ResourceRef, ResourceRef(Font::GetTypeStatic()), AM_FILE);
    ATTRIBUTE(Text, VAR_INT, "Font Size", fontSize_, DEFAULT_FONT_SIZE, AM_FILE);
    ATTRIBUTE(Text, VAR_STRING, "Text", text_, String::EMPTY, AM_FILE);
    ENUM_ATTRIBUTE(Text, "Text Alignment", textAlignment_, horizontalAlignments, HA_LEFT, AM_FILE);
    ATTRIBUTE(Text, VAR_FLOAT, "Row Spacing", rowSpacing_, 1.0f, AM_FILE);
    ATTRIBUTE(Text, VAR_BOOL, "Word Wrap", wordWrap_, false, AM_FILE);
    REF_ACCESSOR_ATTRIBUTE(Text, VAR_COLOR, "Selection Color", GetSelectionColor, SetSelectionColor, Color, Color::TRANSPARENT, AM_FILE);
    REF_ACCESSOR_ATTRIBUTE(Text, VAR_COLOR, "Hover Color", GetHoverColor, SetHoverColor, Color, Color::TRANSPARENT, AM_FILE);

    // Change the default value for UseDerivedOpacity
    context->GetAttribute<Text>("Use Derived Opacity")->defaultValue_ = false;
}

void Text::ApplyAttributes()
{
    UIElement::ApplyAttributes();

    // Decode to Unicode now
    unicodeText_.Clear();
    for (unsigned i = 0; i < text_.Length();)
        unicodeText_.Push(text_.NextUTF8Char(i));

    fontSize_ = Max(fontSize_, 1);
    ValidateSelection();
    UpdateText();
}

void Text::GetBatches(PODVector<UIBatch>& batches, PODVector<float>& vertexData, const IntRect& currentScissor)
{
    // Hovering and/or whole selection batch
    if ((hovering_ && hoverColor_.a_ > 0.0) || (selected_ && selectionColor_.a_ > 0.0f))
    {
        bool both = hovering_ && selected_ && hoverColor_.a_ > 0.0 && selectionColor_.a_ > 0.0f;
        UIBatch batch(this, BLEND_ALPHA, currentScissor, 0, &vertexData);
        batch.AddQuad(0, 0, GetWidth(), GetHeight(), 0, 0, 0, 0, both ? selectionColor_.Lerp(hoverColor_, 0.5f) :
            (selected_ && selectionColor_.a_ > 0.0f ? selectionColor_ : hoverColor_));
        UIBatch::AddOrMerge(batch, batches);
    }

    // Partial selection batch
    if (!selected_ && selectionLength_ && charSizes_.Size() >= selectionStart_ + selectionLength_ && selectionColor_.a_ > 0.0f)
    {
        UIBatch batch(this, BLEND_ALPHA, currentScissor, 0, &vertexData);

        IntVector2 currentStart = charPositions_[selectionStart_];
        IntVector2 currentEnd = currentStart;
        for (unsigned i = selectionStart_; i < selectionStart_ + selectionLength_; ++i)
        {
            // Check if row changes, and start a new quad in that case
            if (charSizes_[i].x_ && charSizes_[i].y_)
            {
                if (charPositions_[i].y_ != currentStart.y_)
                {
                    batch.AddQuad(currentStart.x_, currentStart.y_, currentEnd.x_ - currentStart.x_, currentEnd.y_ - currentStart.y_,
                        0, 0, 0, 0, selectionColor_);
                    currentStart = charPositions_[i];
                    currentEnd = currentStart + charSizes_[i];
                }
                else
                {
                    currentEnd.x_ += charSizes_[i].x_;
                    currentEnd.y_ = Max(currentStart.y_ + charSizes_[i].y_, currentEnd.y_);
                }
            }
        }
        if (currentEnd != currentStart)
        {
            batch.AddQuad(currentStart.x_, currentStart.y_, currentEnd.x_ - currentStart.x_, currentEnd.y_ - currentStart.y_,
                0, 0, 0, 0, selectionColor_);
        }

        UIBatch::AddOrMerge(batch, batches);
    }

    // Text batch
    if (font_)
    {
        const FontFace* face = font_->GetFace(fontSize_);
        if (!face)
            return;

        if (face->textures_.Size() > 1)
        {
            // Only traversing thru the printText once regardless of number of textures/pages in the font
            Vector<PODVector<GlyphLocation> > pageGlyphLocations(face->textures_.Size());

            unsigned rowIndex = 0;
            int x = GetRowStartPosition(rowIndex);
            int y = 0;

            for (unsigned i = 0; i < printText_.Size(); ++i)
            {
                unsigned c = printText_[i];

                if (c != '\n')
                {
                    const FontGlyph* p = face->GetGlyph(c);
                    if (!p)
                        continue;

                    pageGlyphLocations[p->page_].Push(GlyphLocation(x, y, p));

                    x += p->advanceX_;
                    if (i < printText_.Size() - 1)
                        x += face->GetKerning(c, printText_[i + 1]);
                }
                else
                {
                    x = GetRowStartPosition(++rowIndex);
                    y += rowHeight_;
                }
            }

            for (unsigned n = 0; n < face->textures_.Size(); ++n)
            {
                // One batch per texture/page
                UIBatch pageBatch(this, BLEND_ALPHA, currentScissor, face->textures_[n], &vertexData);

                const PODVector<GlyphLocation>& pageGlyphLocation = pageGlyphLocations[n];
                for (unsigned i = 0; i < pageGlyphLocation.Size(); ++i)
                {
                    const GlyphLocation& glyphLocation = pageGlyphLocation[i];
                    const FontGlyph& glyph = *glyphLocation.glyph_;
                    pageBatch.AddQuad(glyphLocation.x_ + glyph.offsetX_, glyphLocation.y_ + glyph.offsetY_, glyph.width_, glyph.height_, glyph.x_, glyph.y_);
                }

                batches.Push(pageBatch);
            }
        }
        else
        {
            // If only one texture page, construct the UI batch directly
            unsigned rowIndex = 0;
            int x = GetRowStartPosition(rowIndex);
            int y = 0;

            UIBatch batch(this, BLEND_ALPHA, currentScissor, face->textures_[0], &vertexData);

            for (unsigned i = 0; i < printText_.Size(); ++i)
            {
                unsigned c = printText_[i];

                if (c != '\n')
                {
                    const FontGlyph* p = face->GetGlyph(c);
                    if (!p)
                        continue;

                    batch.AddQuad(x + p->offsetX_, y + p->offsetY_, p->width_, p->height_, p->x_, p->y_);

                    x += p->advanceX_;
                    if (i < printText_.Size() - 1)
                        x += face->GetKerning(c, printText_[i + 1]);
                }
                else
                {
                    x = GetRowStartPosition(++rowIndex);
                    y += rowHeight_;
                }
            }

            UIBatch::AddOrMerge(batch, batches);
        }
    }

    // Reset hovering for next frame
    hovering_ = false;
}

void Text::OnResize()
{
    if (wordWrap_)
        UpdateText();
}

bool Text::SetFont(const String& fontName, int size)
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    return SetFont(cache->GetResource<Font>(fontName), size);
}

bool Text::SetFont(Font* font, int size)
{
    if (!font)
    {
        LOGERROR("Null font for Text");
        return false;
    }

    if (font != font_ || size != fontSize_)
    {
        font_ = font;
        fontSize_ = Max(size, 1);
        UpdateText();
    }

    return true;
}

void Text::SetText(const String& text)
{
    text_ = text;

    // Decode to Unicode now
    unicodeText_.Clear();
    for (unsigned i = 0; i < text_.Length();)
        unicodeText_.Push(text_.NextUTF8Char(i));

    ValidateSelection();
    UpdateText();
}

void Text::SetTextAlignment(HorizontalAlignment align)
{
    if (align != textAlignment_)
    {
        textAlignment_ = align;
        UpdateText();
    }
}

void Text::SetRowSpacing(float spacing)
{
    if (spacing != rowSpacing_)
    {
        rowSpacing_ = Max(spacing, MIN_ROW_SPACING);
        UpdateText();
    }
}

void Text::SetWordwrap(bool enable)
{
    if (enable != wordWrap_)
    {
        wordWrap_ = enable;
        UpdateText();
    }
}

void Text::SetSelection(unsigned start, unsigned length)
{
    selectionStart_ = start;
    selectionLength_ = length;
    ValidateSelection();
}

void Text::ClearSelection()
{
    selectionStart_ = 0;
    selectionLength_ = 0;
}

void Text::SetSelectionColor(const Color& color)
{
    selectionColor_ = color;
}

void Text::SetHoverColor(const Color& color)
{
    hoverColor_ = color;
}

void Text::SetFontAttr(ResourceRef value)
{
    ResourceCache* cache = GetSubsystem<ResourceCache>();
    font_ = cache->GetResource<Font>(value.id_);
}

ResourceRef Text::GetFontAttr() const
{
    return GetResourceRef(font_, Font::GetTypeStatic());
}

bool Text::FilterImplicitAttributes(XMLElement& dest)
{
    if (!UIElement::FilterImplicitAttributes(dest))
        return false;

    if (!IsFixedWidth())
    {
        if (!RemoveChildXML(dest, "Size"))
            return false;
        if (!RemoveChildXML(dest, "Min Size"))
            return false;
        if (!RemoveChildXML(dest, "Max Size"))
            return false;
    }

    return true;
}

void Text::UpdateText()
{
    int width = 0;
    int height = 0;

    rowWidths_.Clear();
    printText_.Clear();

    PODVector<unsigned> printToText;

    if (font_)
    {
        const FontFace* face = font_->GetFace(fontSize_);
        if (!face)
            return;

        rowHeight_ = face->rowHeight_;
        int rowWidth = 0;
        int rowHeight = (int)(rowSpacing_ * rowHeight_);

        // First see if the text must be split up
        if (!wordWrap_)
        {
            printText_ = unicodeText_;
            printToText.Resize(printText_.Size());
            for (unsigned i = 0; i < printText_.Size(); ++i)
                printToText[i] = i;
        }
        else
        {
            int maxWidth = GetWidth();
            unsigned nextBreak = 0;
            unsigned lineStart = 0;
            for (unsigned i = 0; i < unicodeText_.Size(); ++i)
            {
                unsigned j;
                unsigned c = unicodeText_[i];

                if (c != '\n')
                {
                    bool ok = true;

                    if (nextBreak <= i)
                    {
                        int futureRowWidth = rowWidth;
                        for (j = i; j < unicodeText_.Size(); ++j)
                        {
                            unsigned d = unicodeText_[j];
                            if (d == ' ' || d == '\n')
                            {
                                nextBreak = j;
                                break;
                            }
                            const FontGlyph* glyph = face->GetGlyph(d);
                            if (glyph)
                            {
                                futureRowWidth += glyph->advanceX_;
                                if (j < unicodeText_.Size() - 1)
                                    futureRowWidth += face->GetKerning(d, unicodeText_[j + 1]);
                            }
                            if (d == '-' && futureRowWidth <= maxWidth)
                            {
                                nextBreak = j + 1;
                                break;
                            }
                            if (futureRowWidth > maxWidth)
                            {
                                ok = false;
                                break;
                            }
                        }
                    }

                    if (!ok)
                    {
                        // If did not find any breaks on the line, copy until j, or at least 1 char, to prevent infinite loop
                        if (nextBreak == lineStart)
                        {
                            while (i < j)
                            {
                                printText_.Push(unicodeText_[i]);
                                printToText.Push(i);
                                ++i;
                            }
                        }
                        printText_.Push('\n');
                        printToText.Push(Min((int)i, (int)unicodeText_.Size() - 1));
                        rowWidth = 0;
                        nextBreak = lineStart = i;
                    }

                    if (i < unicodeText_.Size())
                    {
                        // When copying a space, position is allowed to be over row width
                        c = unicodeText_[i];
                        const FontGlyph* glyph = face->GetGlyph(c);
                        if (glyph)
                        {
                            rowWidth += glyph->advanceX_;
                            if (i < text_.Length() - 1)
                                rowWidth += face->GetKerning(c, unicodeText_[i + 1]);
                        }
                        if (rowWidth <= maxWidth)
                        {
                            printText_.Push(c);
                            printToText.Push(i);
                        }
                    }
                }
                else
                {
                    printText_.Push('\n');
                    printToText.Push(Min((int)i, (int)unicodeText_.Size() - 1));
                    rowWidth = 0;
                    nextBreak = lineStart = i;
                }
            }
        }

        rowWidth = 0;

        for (unsigned i = 0; i < printText_.Size(); ++i)
        {
            unsigned c = printText_[i];

            if (c != '\n')
            {
                const FontGlyph* glyph = face->GetGlyph(c);
                if (glyph)
                {
                    rowWidth += glyph->advanceX_;
                    if (i < printText_.Size() - 1)
                        rowWidth += face->GetKerning(c, printText_[i + 1]);
                }
            }
            else
            {
                width = Max(width, rowWidth);
                height += rowHeight;
                rowWidths_.Push(rowWidth);
                rowWidth = 0;
            }
        }

        if (rowWidth)
        {
            width = Max(width, rowWidth);
            height += rowHeight;
            rowWidths_.Push(rowWidth);
        }

        // Set row height even if text is empty
        if (!height)
            height = rowHeight;

        // Store position & size of each character
        charPositions_.Resize(unicodeText_.Size() + 1);
        charSizes_.Resize(unicodeText_.Size());

        unsigned rowIndex = 0;
        int x = GetRowStartPosition(rowIndex);
        int y = 0;
        for (unsigned i = 0; i < printText_.Size(); ++i)
        {
            charPositions_[printToText[i]] = IntVector2(x, y);
            unsigned c = printText_[i];
            if (c != '\n')
            {
                const FontGlyph* glyph = face->GetGlyph(c);
                charSizes_[printToText[i]] = IntVector2(glyph ? glyph->advanceX_ : 0, rowHeight_);
                if (glyph)
                {
                    x += glyph->advanceX_;
                    if (i < printText_.Size() - 1)
                        x += face->GetKerning(c, printText_[i + 1]);
                }
            }
            else
            {
                charSizes_[printToText[i]] = IntVector2::ZERO;
                x = GetRowStartPosition(++rowIndex);
                y += rowHeight;
            }
        }
        // Store the ending position
        charPositions_[unicodeText_.Size()] = IntVector2(x, y);
    }

    // Set minimum and current size according to the text size, but respect fixed width if set
    if (!IsFixedWidth())
    {
        SetMinWidth(wordWrap_ ? 0 : width);
        SetWidth(width);
    }
    SetFixedHeight(height);
}

void Text::ValidateSelection()
{
    unsigned textLength = unicodeText_.Size();

    if (textLength)
    {
        if (selectionStart_ >= textLength)
            selectionStart_ = textLength - 1;
        if (selectionStart_ + selectionLength_ > textLength)
            selectionLength_ = textLength - selectionStart_;
    }
    else
    {
        selectionStart_ = 0;
        selectionLength_ = 0;
    }
}

int Text::GetRowStartPosition(unsigned rowIndex) const
{
    int rowWidth = 0;

    if (rowIndex < rowWidths_.Size())
        rowWidth = rowWidths_[rowIndex];

    int ret = GetIndentWidth();

    switch (textAlignment_)
    {
    case HA_LEFT:
        break;
    case HA_CENTER:
        ret += (GetSize().x_ - rowWidth) / 2;
        break;
    case HA_RIGHT:
        ret += GetSize().x_ - rowWidth;
        break;
    }

    return ret;
}

}
