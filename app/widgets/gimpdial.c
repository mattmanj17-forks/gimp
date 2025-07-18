/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * gimpdial.c
 * Copyright (C) 2014 Michael Natterer <mitch@gimp.org>
 *
 * Based on code from the color-rotate plug-in
 * Copyright (C) 1997-1999 Sven Anders (anderss@fmi.uni-passau.de)
 *                         Based on code from Pavel Grinfeld (pavel@ml.com)
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

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpmath/gimpmath.h"
#include "libgimpcolor/gimpcolor.h"
#include "libgimpwidgets/gimpwidgets.h"

#include "widgets-types.h"

#include "core/gimp-cairo.h"

#include "gimpdial.h"


#define SEGMENT_FRACTION 0.3

/* round n to the nearest multiple of m */
#define SNAP(n, m) (RINT((n) / (m)) * (m))

enum
{
  PROP_0,
  PROP_DRAW_BETA,
  PROP_ALPHA,
  PROP_BETA,
  PROP_CLOCKWISE_ANGLES,
  PROP_CLOCKWISE_DELTA
};

typedef enum
{
  DIAL_TARGET_NONE  = 0,
  DIAL_TARGET_ALPHA = 1 << 0,
  DIAL_TARGET_BETA  = 1 << 1,
  DIAL_TARGET_BOTH  = DIAL_TARGET_ALPHA | DIAL_TARGET_BETA
} DialTarget;


typedef struct _GimpDialPrivate GimpDialPrivate;

struct _GimpDialPrivate
{
  gdouble     alpha;
  gdouble     beta;
  gboolean    clockwise_angles;
  gboolean    clockwise_delta;
  gboolean    draw_beta;

  DialTarget  target;
  gdouble     last_angle;
};

#define GET_PRIVATE(obj)                                                \
  ((GimpDialPrivate *) gimp_dial_get_instance_private ((GimpDial *) obj))


static void        gimp_dial_set_property         (GObject            *object,
                                                   guint               property_id,
                                                   const GValue       *value,
                                                   GParamSpec         *pspec);
static void        gimp_dial_get_property         (GObject            *object,
                                                   guint               property_id,
                                                   GValue             *value,
                                                   GParamSpec         *pspec);

static gboolean    gimp_dial_draw                 (GtkWidget          *widget,
                                                   cairo_t            *cr);
static gboolean    gimp_dial_button_press_event   (GtkWidget          *widget,
                                                   GdkEventButton     *bevent);
static gboolean    gimp_dial_motion_notify_event  (GtkWidget          *widget,
                                                   GdkEventMotion     *mevent);

static void        gimp_dial_reset_target         (GimpCircle         *circle);

static void        gimp_dial_set_target           (GimpDial           *dial,
                                                   DialTarget          target);

static void        gimp_dial_draw_arrows          (cairo_t            *cr,
                                                   gint                size,
                                                   gdouble             alpha,
                                                   gdouble             beta,
                                                   gboolean            clockwise_delta,
                                                   DialTarget          highlight,
                                                   gboolean            draw_beta);

static gdouble     gimp_dial_normalize_angle      (gdouble             angle);
static gdouble     gimp_dial_get_angle_distance   (gdouble             alpha,
                                                   gdouble             beta);


G_DEFINE_TYPE_WITH_PRIVATE (GimpDial, gimp_dial, GIMP_TYPE_CIRCLE)

#define parent_class gimp_dial_parent_class


static void
gimp_dial_class_init (GimpDialClass *klass)
{
  GObjectClass    *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass  *widget_class = GTK_WIDGET_CLASS (klass);
  GimpCircleClass *circle_class = GIMP_CIRCLE_CLASS (klass);

  object_class->get_property         = gimp_dial_get_property;
  object_class->set_property         = gimp_dial_set_property;

  widget_class->draw                 = gimp_dial_draw;
  widget_class->button_press_event   = gimp_dial_button_press_event;
  widget_class->motion_notify_event  = gimp_dial_motion_notify_event;

  circle_class->reset_target         = gimp_dial_reset_target;

  g_object_class_install_property (object_class, PROP_ALPHA,
                                   g_param_spec_double ("alpha",
                                                        NULL, NULL,
                                                        0.0, 2 * G_PI, 0.0,
                                                        GIMP_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_BETA,
                                   g_param_spec_double ("beta",
                                                        NULL, NULL,
                                                        0.0, 2 * G_PI, G_PI,
                                                        GIMP_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_CLOCKWISE_ANGLES,
                                   g_param_spec_boolean ("clockwise-angles",
                                                         NULL, NULL,
                                                         FALSE,
                                                         GIMP_PARAM_READWRITE |
                                                         G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_CLOCKWISE_DELTA,
                                   g_param_spec_boolean ("clockwise-delta",
                                                         NULL, NULL,
                                                         FALSE,
                                                         GIMP_PARAM_READWRITE |
                                                         G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_DRAW_BETA,
                                   g_param_spec_boolean ("draw-beta",
                                                         NULL, NULL,
                                                         TRUE,
                                                         GIMP_PARAM_READWRITE |
                                                         G_PARAM_CONSTRUCT));
}

static void
gimp_dial_init (GimpDial *dial)
{
}

static void
gimp_dial_set_property (GObject      *object,
                        guint         property_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  GimpDialPrivate *priv = GET_PRIVATE (object);

  switch (property_id)
    {
    case PROP_ALPHA:
      priv->alpha = g_value_get_double (value);
      gtk_widget_queue_draw (GTK_WIDGET (object));
      break;

    case PROP_BETA:
      priv->beta = g_value_get_double (value);
      gtk_widget_queue_draw (GTK_WIDGET (object));
      break;

    case PROP_CLOCKWISE_ANGLES:
      priv->clockwise_angles = g_value_get_boolean (value);
      gtk_widget_queue_draw (GTK_WIDGET (object));
      break;

    case PROP_CLOCKWISE_DELTA:
      priv->clockwise_delta = g_value_get_boolean (value);
      gtk_widget_queue_draw (GTK_WIDGET (object));
      break;

    case PROP_DRAW_BETA:
      priv->draw_beta = g_value_get_boolean (value);
      gtk_widget_queue_draw (GTK_WIDGET (object));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gimp_dial_get_property (GObject    *object,
                        guint       property_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  GimpDialPrivate *priv = GET_PRIVATE (object);

  switch (property_id)
    {
    case PROP_ALPHA:
      g_value_set_double (value, priv->alpha);
      break;

    case PROP_BETA:
      g_value_set_double (value, priv->beta);
      break;

    case PROP_CLOCKWISE_ANGLES:
      g_value_set_boolean (value, priv->clockwise_angles);
      break;

    case PROP_CLOCKWISE_DELTA:
      g_value_set_boolean (value, priv->clockwise_delta);
      break;

    case PROP_DRAW_BETA:
      g_value_set_boolean (value, priv->draw_beta);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static gboolean
gimp_dial_draw (GtkWidget *widget,
                cairo_t   *cr)
{
  GimpDialPrivate *priv = GET_PRIVATE (widget);
  GtkAllocation    allocation;
  gint             size;
  gdouble          alpha = priv->alpha;
  gdouble          beta  = priv->beta;

  GTK_WIDGET_CLASS (parent_class)->draw (widget, cr);

  g_object_get (widget,
                "size", &size,
                NULL);

  gtk_widget_get_allocation (widget, &allocation);

  if (priv->clockwise_angles)
    {
      alpha = -alpha;
      beta  = -beta;
    }

  cairo_save (cr);

  cairo_translate (cr,
                   (allocation.width  - size) / 2.0,
                   (allocation.height - size) / 2.0);

  gimp_dial_draw_arrows (cr, size,
                         alpha, beta,
                         priv->clockwise_delta,
                         priv->target,
                         priv->draw_beta);

  cairo_restore (cr);

  return FALSE;
}

static gboolean
gimp_dial_button_press_event (GtkWidget      *widget,
                              GdkEventButton *bevent)
{
  GimpDialPrivate *priv = GET_PRIVATE (widget);

  if (bevent->type == GDK_BUTTON_PRESS &&
      bevent->button == 1              &&
      priv->target != DIAL_TARGET_NONE)
    {
      gdouble angle;

      GTK_WIDGET_CLASS (parent_class)->button_press_event (widget, bevent);

      angle = _gimp_circle_get_angle_and_distance (GIMP_CIRCLE (widget),
                                                   bevent->x, bevent->y,
                                                   NULL);

      if (priv->clockwise_angles && angle)
        angle = 2.0 * G_PI - angle;

      if (bevent->state & GDK_SHIFT_MASK)
        angle = SNAP (angle, G_PI / 12.0);

      priv->last_angle = angle;

      switch (priv->target)
        {
        case DIAL_TARGET_ALPHA:
          g_object_set (widget, "alpha", angle, NULL);
          break;

        case DIAL_TARGET_BETA:
          g_object_set (widget, "beta", angle, NULL);
          break;

        default:
          break;
        }
    }

  return FALSE;
}

static gboolean
gimp_dial_motion_notify_event (GtkWidget      *widget,
                               GdkEventMotion *mevent)
{
  GimpDialPrivate *priv = GET_PRIVATE (widget);
  gdouble          angle;
  gdouble          distance;

  angle = _gimp_circle_get_angle_and_distance (GIMP_CIRCLE (widget),
                                               mevent->x, mevent->y,
                                               &distance);

  if (priv->clockwise_angles && angle)
    angle = 2.0 * G_PI - angle;

  if (_gimp_circle_has_grab (GIMP_CIRCLE (widget)))
    {
      gdouble delta;

      if (mevent->state & GDK_SHIFT_MASK)
        angle = SNAP (angle, G_PI / 12.0);

      delta = angle - priv->last_angle;
      priv->last_angle = angle;

      if (delta != 0.0)
        {
          gdouble alpha = priv->alpha;

          switch (priv->target)
            {
            case DIAL_TARGET_ALPHA:
              g_object_set (widget, "alpha", angle, NULL);
              break;

            case DIAL_TARGET_BETA:
              g_object_set (widget, "beta", angle, NULL);
              break;

            case DIAL_TARGET_BOTH:
              /* snap both by the alpha value */
              if (mevent->state & GDK_SHIFT_MASK)
                delta = SNAP (alpha + delta, G_PI / 12.0) - alpha;

              g_object_set (widget,
                            "alpha",
                            gimp_dial_normalize_angle (priv->alpha + delta),
                            "beta",
                            gimp_dial_normalize_angle (priv->beta  + delta),
                            NULL);
              break;

            default:
              break;
            }
        }
    }
  else
    {
      DialTarget target;
      gdouble    dist_alpha;
      gdouble    dist_beta;

      dist_alpha = gimp_dial_get_angle_distance (priv->alpha, angle);
      dist_beta  = gimp_dial_get_angle_distance (priv->beta, angle);

      if (priv->draw_beta             &&
          distance > SEGMENT_FRACTION &&
          MIN (dist_alpha, dist_beta) < G_PI / 12)
        {
          if (dist_alpha < dist_beta)
            {
              target = DIAL_TARGET_ALPHA;
            }
          else
            {
              target = DIAL_TARGET_BETA;
            }
        }
      else
        {
          target = DIAL_TARGET_BOTH;
        }

      gimp_dial_set_target (GIMP_DIAL (widget), target);
    }

  gdk_event_request_motions (mevent);

  return FALSE;
}

static void
gimp_dial_reset_target (GimpCircle *circle)
{
  gimp_dial_set_target (GIMP_DIAL (circle), DIAL_TARGET_NONE);
}


/*  public functions  */

GtkWidget *
gimp_dial_new (void)
{
  return g_object_new (GIMP_TYPE_DIAL, NULL);
}


/*  private functions  */

static void
gimp_dial_set_target (GimpDial   *dial,
                      DialTarget  target)
{
  GimpDialPrivate *priv = GET_PRIVATE (dial);

  if (target != priv->target)
    {
      priv->target = target;
      gtk_widget_queue_draw (GTK_WIDGET (dial));
    }
}

static void
gimp_dial_draw_arrow (cairo_t *cr,
                      gdouble  radius,
                      gdouble  angle)
{
#define REL 0.8
#define DEL 0.1

  cairo_move_to (cr, radius, radius);
  cairo_line_to (cr,
                 radius + radius * cos (angle),
                 radius - radius * sin (angle));

  cairo_move_to (cr,
                 radius + radius * cos (angle),
                 radius - radius * sin (angle));
  cairo_line_to (cr,
                 radius + radius * REL * cos (angle - DEL),
                 radius - radius * REL * sin (angle - DEL));

  cairo_move_to (cr,
                 radius + radius * cos (angle),
                 radius - radius * sin (angle));
  cairo_line_to (cr,
                 radius + radius * REL * cos (angle + DEL),
                 radius - radius * REL * sin (angle + DEL));
}

static void
gimp_dial_draw_segment (cairo_t  *cr,
                        gdouble   radius,
                        gdouble   alpha,
                        gdouble   beta,
                        gboolean  clockwise_delta)
{
  gint    direction = clockwise_delta ? -1 : 1;
  gint    segment_dist;
  gint    tick;
  gdouble slice;

  segment_dist = radius * SEGMENT_FRACTION;
  tick         = MIN (10, segment_dist);

  cairo_move_to (cr,
                 radius + segment_dist * cos (beta),
                 radius - segment_dist * sin (beta));
  cairo_line_to (cr,
                 radius + segment_dist * cos (beta) +
                 direction * tick * sin (beta),
                 radius - segment_dist * sin (beta) +
                 direction * tick * cos (beta));

  cairo_new_sub_path (cr);

  if (clockwise_delta)
    slice = -gimp_dial_normalize_angle (alpha - beta);
  else
    slice = gimp_dial_normalize_angle (beta - alpha);

  gimp_cairo_arc (cr, radius, radius, segment_dist,
                  alpha, slice);
}

static void
gimp_dial_draw_arrows (cairo_t    *cr,
                       gint        size,
                       gdouble     alpha,
                       gdouble     beta,
                       gboolean    clockwise_delta,
                       DialTarget  highlight,
                       gboolean    draw_beta)
{
  gdouble radius = size / 2.0 - 2.0; /* half the broad line with and half a px */

  cairo_save (cr);

  cairo_translate (cr, 2.0, 2.0); /* half the broad line width and half a px*/

  cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
  cairo_set_line_join (cr, CAIRO_LINE_JOIN_ROUND);

  if (highlight != DIAL_TARGET_BOTH)
    {
      if (! (highlight & DIAL_TARGET_ALPHA))
        gimp_dial_draw_arrow (cr, radius, alpha);

      if (draw_beta)
        {
          if (! (highlight & DIAL_TARGET_BETA))
            gimp_dial_draw_arrow (cr, radius, beta);

          if ((highlight & DIAL_TARGET_BOTH) != DIAL_TARGET_BOTH)
            gimp_dial_draw_segment (cr, radius, alpha, beta, clockwise_delta);
        }

      cairo_set_line_width (cr, 3.0);
      cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.6);
      cairo_stroke_preserve (cr);

      cairo_set_line_width (cr, 1.0);
      cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.8);
      cairo_stroke (cr);
    }

  if (highlight != DIAL_TARGET_NONE)
    {
      if (highlight & DIAL_TARGET_ALPHA)
        gimp_dial_draw_arrow (cr, radius, alpha);

      if (draw_beta)
        {
          if (highlight & DIAL_TARGET_BETA)
            gimp_dial_draw_arrow (cr, radius, beta);

          if ((highlight & DIAL_TARGET_BOTH) == DIAL_TARGET_BOTH)
            gimp_dial_draw_segment (cr, radius, alpha, beta, clockwise_delta);
        }

      cairo_set_line_width (cr, 3.0);
      cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.6);
      cairo_stroke_preserve (cr);

      cairo_set_line_width (cr, 1.0);
      cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.8);
      cairo_stroke (cr);
    }

  cairo_restore (cr);
}

static gdouble
gimp_dial_normalize_angle (gdouble angle)
{
  if (angle < 0)
    return angle + 2 * G_PI;
  else if (angle > 2 * G_PI)
    return angle - 2 * G_PI;
  else
    return angle;
}

static gdouble
gimp_dial_get_angle_distance (gdouble alpha,
                              gdouble beta)
{
  return ABS (MIN (gimp_dial_normalize_angle (alpha - beta),
                   2 * G_PI - gimp_dial_normalize_angle (alpha - beta)));
}
