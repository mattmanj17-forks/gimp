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

#include "config.h"

#include <string.h>

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpconfig/gimpconfig.h"
#include "libgimpwidgets/gimpwidgets.h"

#include "actions-types.h"

#include "operations/gimp-operation-config.h"
#include "operations/gimpoperationsettings.h"

#include "core/gimp.h"
#include "core/gimp-filter-history.h"
#include "core/gimpimage.h"
#include "core/gimpprogress.h"

#include "widgets/gimpaction.h"

#include "actions.h"
#include "filters-commands.h"
#include "gimpgeglprocedure.h"
#include "procedure-commands.h"


/*  local function prototypes  */

static gchar * filters_parse_operation (Gimp          *gimp,
                                        const gchar   *operation_str,
                                        const gchar   *icon_name,
                                        GimpObject   **settings);

/*  public functions  */

void
filters_apply_cmd_callback (GimpAction *action,
                            GVariant   *value,
                            gpointer    data)
{
  GimpImage     *image;
  GList         *drawables;
  gchar         *operation;
  GimpObject    *settings;
  GimpProcedure *procedure;
  GVariant      *variant;

  return_if_no_drawables (image, drawables, data);

  if (g_list_length (drawables) != 1)
    {
      /* We only support running filters on single drawable for now. */
      g_list_free (drawables);
      return;
    }

  operation = filters_parse_operation (image->gimp,
                                       g_variant_get_string (value, NULL),
                                       gimp_action_get_icon_name (action),
                                       &settings);

  procedure = gimp_gegl_procedure_new (image->gimp,
                                       NULL,
                                       GIMP_RUN_NONINTERACTIVE, settings,
                                       operation,
                                       gimp_action_get_name (action),
                                       gimp_action_get_label (action),
                                       gimp_action_get_tooltip (action),
                                       gimp_action_get_icon_name (action),
                                       gimp_action_get_help_id (action));

  g_free (operation);

  if (settings)
    g_object_unref (settings);

  gimp_filter_history_add (image->gimp, procedure);

  variant = g_variant_new_uint64 (GPOINTER_TO_SIZE (procedure));
  g_variant_take_ref (variant);
  filters_history_cmd_callback (NULL, variant, data);

  g_variant_unref (variant);
  g_object_unref (procedure);
  g_list_free (drawables);
}

void
filters_apply_interactive_cmd_callback (GimpAction *action,
                                        GVariant   *value,
                                        gpointer    data)
{
  GimpImage     *image;
  GList         *drawables;
  GimpProcedure *procedure;
  GVariant      *variant;

  return_if_no_drawables (image, drawables, data);

  if (g_list_length (drawables) != 1)
    {
      /* We only support running filters on single drawable for now. */
      g_list_free (drawables);
      return;
    }

  procedure = gimp_gegl_procedure_new (image->gimp,
                                       NULL,
                                       GIMP_RUN_INTERACTIVE, NULL,
                                       g_variant_get_string (value, NULL),
                                       gimp_action_get_name (action),
                                       gimp_action_get_label (action),
                                       gimp_action_get_tooltip (action),
                                       gimp_action_get_icon_name (action),
                                       gimp_action_get_help_id (action));

  gimp_filter_history_add (image->gimp, procedure);

  variant = g_variant_new_uint64 (GPOINTER_TO_SIZE (procedure));
  g_variant_take_ref (variant);
  filters_history_cmd_callback (NULL, variant, data);

  g_variant_unref (variant);
  g_object_unref (procedure);
  g_list_free (drawables);
}

void
filters_repeat_cmd_callback (GimpAction *action,
                             GVariant   *value,
                             gpointer    data)
{
  Gimp          *gimp;
  GimpDisplay   *display;
  GimpProcedure *procedure;
  GimpRunMode    run_mode;

  return_if_no_gimp (gimp, data);
  return_if_no_display (display, data);

  run_mode = (GimpRunMode) g_variant_get_int32 (value);

  procedure = gimp_filter_history_nth (gimp, 0);

  if (procedure)
    filters_run_procedure (gimp, display, procedure, run_mode);
}

void
filters_history_cmd_callback (GimpAction *action,
                              GVariant   *value,
                              gpointer    data)
{
  Gimp          *gimp;
  GimpDisplay   *display;
  GimpProcedure *procedure;
  gsize          hack;
  return_if_no_gimp (gimp, data);
  return_if_no_display (display, data);

  hack = g_variant_get_uint64 (value);

  procedure = GSIZE_TO_POINTER (hack);

  filters_run_procedure (gimp, display, procedure, GIMP_RUN_INTERACTIVE);
}


/*  private functions  */

static gchar *
filters_parse_operation (Gimp         *gimp,
                         const gchar  *operation_str,
                         const gchar  *icon_name,
                         GimpObject  **settings)
{
  const gchar *newline = strchr (operation_str, '\n');

  *settings = NULL;

  if (newline)
    {
      gchar       *operation;
      const gchar *serialized;

      operation  = g_strndup (operation_str, newline - operation_str);
      serialized = newline + 1;

      if (*serialized)
        {
          GError *error = NULL;

          *settings =
            g_object_new (gimp_operation_config_get_type (gimp, operation,
                                                          icon_name,
                                                          GIMP_TYPE_OPERATION_SETTINGS),
                          NULL);

          if (! gimp_config_deserialize_string (GIMP_CONFIG (*settings),
                                                serialized, -1, NULL,
                                                &error))
            {
              g_warning ("filters_parse_operation: deserializing hardcoded "
                         "operation settings failed: %s",
                         error->message);
              g_clear_error (&error);

              g_object_unref (*settings);
              *settings = NULL;
            }
        }

      return operation;
    }

  return g_strdup (operation_str);
}

void
filters_run_procedure (Gimp          *gimp,
                       GimpDisplay   *display,
                       GimpProcedure *procedure,
                       GimpRunMode    run_mode)
{
  GimpObject     *settings = NULL;
  GimpValueArray *args;

  if (GIMP_IS_GEGL_PROCEDURE (procedure))
    {
      GimpGeglProcedure *gegl_procedure = GIMP_GEGL_PROCEDURE (procedure);

      if (gegl_procedure->default_run_mode == GIMP_RUN_NONINTERACTIVE)
        run_mode = GIMP_RUN_NONINTERACTIVE;

      settings = gegl_procedure->default_settings;
    }

  args = procedure_commands_get_display_args (procedure, display, settings);

  if (args)
    {
      gboolean success = FALSE;

      if (run_mode == GIMP_RUN_NONINTERACTIVE)
        {
          success =
            procedure_commands_run_procedure (procedure, gimp,
                                              GIMP_PROGRESS (display),
                                              args);
        }
      else
        {
          success =
            procedure_commands_run_procedure_async (procedure, gimp,
                                                    GIMP_PROGRESS (display),
                                                    run_mode, args,
                                                    display);
        }

      if (success &&
          (! GIMP_IS_GEGL_PROCEDURE (procedure) ||
           /* XXX GimpGeglProcedure made for filter-editing are
            * short-lived and should not be added to history. I'm not
            * sure it makes sense UX-wise anyway ("used" filters are for
            * first calls, not edits).
            * If ever we decide to change this UX logic, some code logic
            * changes are needed too. Cf. crash report #12576 and my
            * (Jehan's) technical comment in there..
            */
           ! gimp_gegl_procedure_is_editing_filter (GIMP_GEGL_PROCEDURE (procedure))))
        gimp_filter_history_add (gimp, procedure);

      gimp_value_array_unref (args);
    }
}
