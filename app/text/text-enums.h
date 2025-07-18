/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once


#define GIMP_TYPE_TEXT_BOX_MODE (gimp_text_box_mode_get_type ())

GType gimp_text_box_mode_get_type (void) G_GNUC_CONST;

typedef enum
{
  GIMP_TEXT_BOX_DYNAMIC, /*< desc="Dynamic" >*/
  GIMP_TEXT_BOX_FIXED    /*< desc="Fixed"   >*/
} GimpTextBoxMode;


#define GIMP_TYPE_TEXT_OUTLINE (gimp_text_outline_get_type ())

GType gimp_text_outline_get_type (void) G_GNUC_CONST;

typedef enum
{
  GIMP_TEXT_OUTLINE_NONE,        /*< desc="Filled"              >*/
  GIMP_TEXT_OUTLINE_STROKE_ONLY, /*< desc="Outlined"            >*/
  GIMP_TEXT_OUTLINE_STROKE_FILL  /*< desc="Outlined and filled" >*/
} GimpTextOutline;


#define GIMP_TYPE_TEXT_OUTLINE_DIRECTION (gimp_text_outline_direction_get_type ())

GType gimp_text_outline_direction_get_type (void) G_GNUC_CONST;

typedef enum
{
  GIMP_TEXT_OUTLINE_DIRECTION_OUTER,   /*< desc="Outer"    >*/
  GIMP_TEXT_OUTLINE_DIRECTION_INNER,   /*< desc="Inner"    >*/
  GIMP_TEXT_OUTLINE_DIRECTION_CENTERED /*< desc="Centered" >*/
} GimpTextOutlineDirection;
