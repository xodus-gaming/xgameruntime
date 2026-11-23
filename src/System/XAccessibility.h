/*
 * Xbox Game runtime Library
 *  GDK Component: System API -> XAccessibility
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __XGAMERUNTIME_XACCESSIBILITY_H__
#define __XGAMERUNTIME_XACCESSIBILITY_H__

#include <windows.h>
#include <xaccessibility.h>

/* Luminance (0..255) of a COLORREF (0x00BBGGRR). */
static inline UINT32 ColorLuminance( COLORREF color )
{
    UINT32 r = color & 0xFF, g = (color >> 8) & 0xFF, b = (color >> 16) & 0xFF;
    return (299 * r + 587 * g + 114 * b) / 1000;
}

/* Maps the SPI_GETHIGHCONTRAST result and the COLOR_WINDOW / COLOR_WINDOWTEXT colors to a mode. */
static inline XHighContrastMode ClassifyHighContrast( bool callSucceeded, DWORD flags, COLORREF windowColor, COLORREF textColor )
{
    if ( !callSucceeded ) return XHighContrastMode::Other;
    if ( !(flags & HCF_HIGHCONTRASTON) ) return XHighContrastMode::Off;

    UINT32 window = ColorLuminance( windowColor ), text = ColorLuminance( textColor );
    if ( window == text ) return XHighContrastMode::Other;
    return window < 128 ? XHighContrastMode::Dark : XHighContrastMode::Light;
}

#endif
