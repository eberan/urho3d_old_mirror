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

#pragma once

#include "Color.h"
#include "Quaternion.h"
#include "Rect.h"
#include "StringHash.h"
#include "Vector4.h"

namespace Urho3D
{

/// Parse a bool from a string. Check for the first non-empty character (converted to lowercase) being either 't', 'y' or '1'.
bool ToBool(const String& source);
/// Parse a bool from a C string. Check for the first non-empty character (converted to lowercase) being either 't', 'y' or '1'.
bool ToBool(const char* source);
/// Parse a float from a string.
float ToFloat(const String& source);
/// Parse a float from a C string.
float ToFloat(const char* source);
/// Parse an integer from a string.
int ToInt(const String& source);
/// Parse an integer from a C string.
int ToInt(const char* source);
/// Parse an unsigned integer from a string.
unsigned ToUInt(const String& source);
/// Parse an unsigned integer from a C string.
unsigned ToUInt(const char* source);
/// Parse a Color from a string.
Color ToColor(const String& source);
/// Parse a Color from a C string.
Color ToColor(const char* source);
/// Parse an IntRect from a string.
IntRect ToIntRect(const String& source);
/// Parse an IntRect from a C string.
IntRect ToIntRect(const char* source);
/// Parse an IntVector2 from a string.
IntVector2 ToIntVector2(const String& source);
/// Parse an IntVector2 from a C string.
IntVector2 ToIntVector2(const char* source);
/// Parse a Quaternion from a string. If only 3 components specified, convert Euler angles (degrees) to quaternion.
Quaternion ToQuaternion(const String& source);
/// Parse a Quaternion from a C string. If only 3 components specified, convert Euler angles (degrees) to quaternion.
Quaternion ToQuaternion(const char* source);
/// Parse a Rect from a string.
Rect ToRect(const String& source);
/// Parse a Rect from a C string.
Rect ToRect(const char* source);
/// Parse a Vector2 from a string.
Vector2 ToVector2(const String& source);
/// Parse a Vector2 from a C string.
Vector2 ToVector2(const char* source);
/// Parse a Vector3 from a string.
Vector3 ToVector3(const String& source);
/// Parse a Vector3 from a C string.
Vector3 ToVector3(const char* source);
/// Parse a Vector4 from a string.
Vector4 ToVector4(const String& source, bool allowMissingCoords = false);
/// Parse a Vector4 from a C string.
Vector4 ToVector4(const char* source, bool allowMissingCoords = false);
/// Convert a pointer to string (returns hexadecimal.)
String ToString(void* value);
/// Convert an unsigned integer to string as hexadecimal.
String ToStringHex(unsigned value);
/// Convert a byte buffer to a string.
void BufferToString(String& dest, const void* data, unsigned size);
/// Convert a string to a byte buffer.
void StringToBuffer(PODVector<unsigned char>& dest, const String& source);
/// Convert a C string to a byte buffer.
void StringToBuffer(PODVector<unsigned char>& dest, const char* source);
/// Return an index to a string list corresponding to the given string, or a default value if not found. The string list must be empty-terminated.
unsigned GetStringListIndex(const String& value, const String* strings, unsigned defaultIndex, bool caseSensitive = false);
/// Return an index to a string list corresponding to the given C string, or a default value if not found. The string list must be empty-terminated.
unsigned GetStringListIndex(const char* value, const String* strings, unsigned defaultIndex, bool caseSensitive = false);
/// Return an index to a C string list corresponding to the given C string, or a default value if not found. The string list must be empty-terminated.
unsigned GetStringListIndex(const char* value, const char** strings, unsigned defaultIndex, bool caseSensitive = false);
/// Return a formatted string.
String ToString(const char* formatString, ...);
/// Return whether a char is an alphabet letter.
bool IsAlpha(unsigned ch);
/// Return whether a char is a digit.
bool IsDigit(unsigned ch);

}
