/* Static libinfinity/inf-marshal.c for MSVC builds */

#include <libinfinity/inf-marshal.h>

#include	<glib-object.h>


#ifdef G_ENABLE_DEBUG
#define g_marshal_value_peek_boolean(v)  g_value_get_boolean (v)
#define g_marshal_value_peek_char(v)     g_value_get_char (v)
#define g_marshal_value_peek_uchar(v)    g_value_get_uchar (v)
#define g_marshal_value_peek_int(v)      g_value_get_int (v)
#define g_marshal_value_peek_uint(v)     g_value_get_uint (v)
#define g_marshal_value_peek_long(v)     g_value_get_long (v)
#define g_marshal_value_peek_ulong(v)    g_value_get_ulong (v)
#define g_marshal_value_peek_int64(v)    g_value_get_int64 (v)
#define g_marshal_value_peek_uint64(v)   g_value_get_uint64 (v)
#define g_marshal_value_peek_enum(v)     g_value_get_enum (v)
#define g_marshal_value_peek_flags(v)    g_value_get_flags (v)
#define g_marshal_value_peek_float(v)    g_value_get_float (v)
#define g_marshal_value_peek_double(v)   g_value_get_double (v)
#define g_marshal_value_peek_string(v)   (char*) g_value_get_string (v)
#define g_marshal_value_peek_param(v)    g_value_get_param (v)
#define g_marshal_value_peek_boxed(v)    g_value_get_boxed (v)
#define g_marshal_value_peek_pointer(v)  g_value_get_pointer (v)
#define g_marshal_value_peek_object(v)   g_value_get_object (v)
#else /* !G_ENABLE_DEBUG */
/* WARNING: This code accesses GValues directly, which is UNSUPPORTED API.
 *          Do not access GValues directly in your code. Instead, use the
 *          g_value_get_*() functions
 */
#define g_marshal_value_peek_boolean(v)  (v)->data[0].v_int
#define g_marshal_value_peek_char(v)     (v)->data[0].v_int
#define g_marshal_value_peek_uchar(v)    (v)->data[0].v_uint
#define g_marshal_value_peek_int(v)      (v)->data[0].v_int
#define g_marshal_value_peek_uint(v)     (v)->data[0].v_uint
#define g_marshal_value_peek_long(v)     (v)->data[0].v_long
#define g_marshal_value_peek_ulong(v)    (v)->data[0].v_ulong
#define g_marshal_value_peek_int64(v)    (v)->data[0].v_int64
#define g_marshal_value_peek_uint64(v)   (v)->data[0].v_uint64
#define g_marshal_value_peek_enum(v)     (v)->data[0].v_long
#define g_marshal_value_peek_flags(v)    (v)->data[0].v_ulong
#define g_marshal_value_peek_float(v)    (v)->data[0].v_float
#define g_marshal_value_peek_double(v)   (v)->data[0].v_double
#define g_marshal_value_peek_string(v)   (v)->data[0].v_pointer
#define g_marshal_value_peek_param(v)    (v)->data[0].v_pointer
#define g_marshal_value_peek_boxed(v)    (v)->data[0].v_pointer
#define g_marshal_value_peek_pointer(v)  (v)->data[0].v_pointer
#define g_marshal_value_peek_object(v)   (v)->data[0].v_pointer
#endif /* !G_ENABLE_DEBUG */


/* VOID:VOID (inf-marshal.in:1) */

/* VOID:BOXED (inf-marshal.in:2) */

/* VOID:BOXED,DOUBLE,DOUBLE (inf-marshal.in:3) */
void
inf_marshal_VOID__BOXED_DOUBLE_DOUBLE (GClosure     *closure,
                                       GValue       *return_value G_GNUC_UNUSED,
                                       guint         n_param_values,
                                       const GValue *param_values,
                                       gpointer      invocation_hint G_GNUC_UNUSED,
                                       gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__BOXED_DOUBLE_DOUBLE) (gpointer     data1,
                                                          gpointer     arg_1,
                                                          gdouble      arg_2,
                                                          gdouble      arg_3,
                                                          gpointer     data2);
  register GMarshalFunc_VOID__BOXED_DOUBLE_DOUBLE callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 4);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_VOID__BOXED_DOUBLE_DOUBLE*) (marshal_data ? &marshal_data : &cc->callback);

  callback (data1,
            g_marshal_value_peek_boxed (param_values + 1),
            g_marshal_value_peek_double (param_values + 2),
            g_marshal_value_peek_double (param_values + 3),
            data2);
}

/* VOID:OBJECT (inf-marshal.in:4) */

/* VOID:UINT,UINT,OBJECT (inf-marshal.in:5) */
void
inf_marshal_VOID__UINT_UINT_OBJECT (GClosure     *closure,
                                    GValue       *return_value G_GNUC_UNUSED,
                                    guint         n_param_values,
                                    const GValue *param_values,
                                    gpointer      invocation_hint G_GNUC_UNUSED,
                                    gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__UINT_UINT_OBJECT) (gpointer     data1,
                                                       guint        arg_1,
                                                       guint        arg_2,
                                                       gpointer     arg_3,
                                                       gpointer     data2);
  register GMarshalFunc_VOID__UINT_UINT_OBJECT callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 4);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_VOID__UINT_UINT_OBJECT*) (marshal_data ? &marshal_data : &cc->callback);

  callback (data1,
            g_marshal_value_peek_uint (param_values + 1),
            g_marshal_value_peek_uint (param_values + 2),
            g_marshal_value_peek_object (param_values + 3),
            data2);
}

/* VOID:POINTER (inf-marshal.in:6) */

/* VOID:OBJECT,DOUBLE (inf-marshal.in:7) */
void
inf_marshal_VOID__OBJECT_DOUBLE (GClosure     *closure,
                                 GValue       *return_value G_GNUC_UNUSED,
                                 guint         n_param_values,
                                 const GValue *param_values,
                                 gpointer      invocation_hint G_GNUC_UNUSED,
                                 gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__OBJECT_DOUBLE) (gpointer     data1,
                                                    gpointer     arg_1,
                                                    gdouble      arg_2,
                                                    gpointer     data2);
  register GMarshalFunc_VOID__OBJECT_DOUBLE callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_VOID__OBJECT_DOUBLE*) (marshal_data ? &marshal_data : &cc->callback);

  callback (data1,
            g_marshal_value_peek_object (param_values + 1),
            g_marshal_value_peek_double (param_values + 2),
            data2);
}

/* VOID:OBJECT,POINTER (inf-marshal.in:8) */
void
inf_marshal_VOID__OBJECT_POINTER (GClosure     *closure,
                                  GValue       *return_value G_GNUC_UNUSED,
                                  guint         n_param_values,
                                  const GValue *param_values,
                                  gpointer      invocation_hint G_GNUC_UNUSED,
                                  gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__OBJECT_POINTER) (gpointer     data1,
                                                     gpointer     arg_1,
                                                     gpointer     arg_2,
                                                     gpointer     data2);
  register GMarshalFunc_VOID__OBJECT_POINTER callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_VOID__OBJECT_POINTER*) (marshal_data ? &marshal_data : &cc->callback);

  callback (data1,
            g_marshal_value_peek_object (param_values + 1),
            g_marshal_value_peek_pointer (param_values + 2),
            data2);
}

/* VOID:POINTER,UINT (inf-marshal.in:9) */
void
inf_marshal_VOID__POINTER_UINT (GClosure     *closure,
                                GValue       *return_value G_GNUC_UNUSED,
                                guint         n_param_values,
                                const GValue *param_values,
                                gpointer      invocation_hint G_GNUC_UNUSED,
                                gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__POINTER_UINT) (gpointer     data1,
                                                   gpointer     arg_1,
                                                   guint        arg_2,
                                                   gpointer     data2);
  register GMarshalFunc_VOID__POINTER_UINT callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_VOID__POINTER_UINT*) (marshal_data ? &marshal_data : &cc->callback);

  callback (data1,
            g_marshal_value_peek_pointer (param_values + 1),
            g_marshal_value_peek_uint (param_values + 2),
            data2);
}

/* VOID:UINT (inf-marshal.in:10) */

/* VOID:UINT,UINT (inf-marshal.in:11) */
void
inf_marshal_VOID__UINT_UINT (GClosure     *closure,
                             GValue       *return_value G_GNUC_UNUSED,
                             guint         n_param_values,
                             const GValue *param_values,
                             gpointer      invocation_hint G_GNUC_UNUSED,
                             gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__UINT_UINT) (gpointer     data1,
                                                guint        arg_1,
                                                guint        arg_2,
                                                gpointer     data2);
  register GMarshalFunc_VOID__UINT_UINT callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_VOID__UINT_UINT*) (marshal_data ? &marshal_data : &cc->callback);

  callback (data1,
            g_marshal_value_peek_uint (param_values + 1),
            g_marshal_value_peek_uint (param_values + 2),
            data2);
}

/* VOID:OBJECT,BOOLEAN (inf-marshal.in:12) */
void
inf_marshal_VOID__OBJECT_BOOLEAN (GClosure     *closure,
                                  GValue       *return_value G_GNUC_UNUSED,
                                  guint         n_param_values,
                                  const GValue *param_values,
                                  gpointer      invocation_hint G_GNUC_UNUSED,
                                  gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__OBJECT_BOOLEAN) (gpointer     data1,
                                                     gpointer     arg_1,
                                                     gboolean     arg_2,
                                                     gpointer     data2);
  register GMarshalFunc_VOID__OBJECT_BOOLEAN callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_VOID__OBJECT_BOOLEAN*) (marshal_data ? &marshal_data : &cc->callback);

  callback (data1,
            g_marshal_value_peek_object (param_values + 1),
            g_marshal_value_peek_boolean (param_values + 2),
            data2);
}

/* VOID:UINT,BOXED,OBJECT (inf-marshal.in:13) */
void
inf_marshal_VOID__UINT_BOXED_OBJECT (GClosure     *closure,
                                     GValue       *return_value G_GNUC_UNUSED,
                                     guint         n_param_values,
                                     const GValue *param_values,
                                     gpointer      invocation_hint G_GNUC_UNUSED,
                                     gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__UINT_BOXED_OBJECT) (gpointer     data1,
                                                        guint        arg_1,
                                                        gpointer     arg_2,
                                                        gpointer     arg_3,
                                                        gpointer     data2);
  register GMarshalFunc_VOID__UINT_BOXED_OBJECT callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 4);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_VOID__UINT_BOXED_OBJECT*) (marshal_data ? &marshal_data : &cc->callback);

  callback (data1,
            g_marshal_value_peek_uint (param_values + 1),
            g_marshal_value_peek_boxed (param_values + 2),
            g_marshal_value_peek_object (param_values + 3),
            data2);
}

/* VOID:UINT,INT (inf-marshal.in:14) */
void
inf_marshal_VOID__UINT_INT (GClosure     *closure,
                            GValue       *return_value G_GNUC_UNUSED,
                            guint         n_param_values,
                            const GValue *param_values,
                            gpointer      invocation_hint G_GNUC_UNUSED,
                            gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__UINT_INT) (gpointer     data1,
                                               guint        arg_1,
                                               gint         arg_2,
                                               gpointer     data2);
  register GMarshalFunc_VOID__UINT_INT callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_VOID__UINT_INT*) (marshal_data ? &marshal_data : &cc->callback);

  callback (data1,
            g_marshal_value_peek_uint (param_values + 1),
            g_marshal_value_peek_int (param_values + 2),
            data2);
}

/* VOID:OBJECT,OBJECT (inf-marshal.in:15) */
void
inf_marshal_VOID__OBJECT_OBJECT (GClosure     *closure,
                                 GValue       *return_value G_GNUC_UNUSED,
                                 guint         n_param_values,
                                 const GValue *param_values,
                                 gpointer      invocation_hint G_GNUC_UNUSED,
                                 gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__OBJECT_OBJECT) (gpointer     data1,
                                                    gpointer     arg_1,
                                                    gpointer     arg_2,
                                                    gpointer     data2);
  register GMarshalFunc_VOID__OBJECT_OBJECT callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_VOID__OBJECT_OBJECT*) (marshal_data ? &marshal_data : &cc->callback);

  callback (data1,
            g_marshal_value_peek_object (param_values + 1),
            g_marshal_value_peek_object (param_values + 2),
            data2);
}

/* VOID:BOXED,OBJECT (inf-marshal.in:16) */
void
inf_marshal_VOID__BOXED_OBJECT (GClosure     *closure,
                                GValue       *return_value G_GNUC_UNUSED,
                                guint         n_param_values,
                                const GValue *param_values,
                                gpointer      invocation_hint G_GNUC_UNUSED,
                                gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__BOXED_OBJECT) (gpointer     data1,
                                                   gpointer     arg_1,
                                                   gpointer     arg_2,
                                                   gpointer     data2);
  register GMarshalFunc_VOID__BOXED_OBJECT callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_VOID__BOXED_OBJECT*) (marshal_data ? &marshal_data : &cc->callback);

  callback (data1,
            g_marshal_value_peek_boxed (param_values + 1),
            g_marshal_value_peek_object (param_values + 2),
            data2);
}

/* VOID:BOXED,BOXED,OBJECT (inf-marshal.in:17) */
void
inf_marshal_VOID__BOXED_BOXED_OBJECT (GClosure     *closure,
                                      GValue       *return_value G_GNUC_UNUSED,
                                      guint         n_param_values,
                                      const GValue *param_values,
                                      gpointer      invocation_hint G_GNUC_UNUSED,
                                      gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__BOXED_BOXED_OBJECT) (gpointer     data1,
                                                         gpointer     arg_1,
                                                         gpointer     arg_2,
                                                         gpointer     arg_3,
                                                         gpointer     data2);
  register GMarshalFunc_VOID__BOXED_BOXED_OBJECT callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 4);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_VOID__BOXED_BOXED_OBJECT*) (marshal_data ? &marshal_data : &cc->callback);

  callback (data1,
            g_marshal_value_peek_boxed (param_values + 1),
            g_marshal_value_peek_boxed (param_values + 2),
            g_marshal_value_peek_object (param_values + 3),
            data2);
}

/* VOID:OBJECT,OBJECT,BOOLEAN (inf-marshal.in:18) */
void
inf_marshal_VOID__OBJECT_OBJECT_BOOLEAN (GClosure     *closure,
                                         GValue       *return_value G_GNUC_UNUSED,
                                         guint         n_param_values,
                                         const GValue *param_values,
                                         gpointer      invocation_hint G_GNUC_UNUSED,
                                         gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__OBJECT_OBJECT_BOOLEAN) (gpointer     data1,
                                                            gpointer     arg_1,
                                                            gpointer     arg_2,
                                                            gboolean     arg_3,
                                                            gpointer     data2);
  register GMarshalFunc_VOID__OBJECT_OBJECT_BOOLEAN callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 4);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_VOID__OBJECT_OBJECT_BOOLEAN*) (marshal_data ? &marshal_data : &cc->callback);

  callback (data1,
            g_marshal_value_peek_object (param_values + 1),
            g_marshal_value_peek_object (param_values + 2),
            g_marshal_value_peek_boolean (param_values + 3),
            data2);
}

/* VOID:DOUBLE (inf-marshal.in:19) */

/* VOID:ENUM (inf-marshal.in:20) */

/* BOOLEAN:OBJECT,OBJECT (inf-marshal.in:21) */
void
inf_marshal_BOOLEAN__OBJECT_OBJECT (GClosure     *closure,
                                    GValue       *return_value G_GNUC_UNUSED,
                                    guint         n_param_values,
                                    const GValue *param_values,
                                    gpointer      invocation_hint G_GNUC_UNUSED,
                                    gpointer      marshal_data)
{
  typedef gboolean (*GMarshalFunc_BOOLEAN__OBJECT_OBJECT) (gpointer     data1,
                                                           gpointer     arg_1,
                                                           gpointer     arg_2,
                                                           gpointer     data2);
  register GMarshalFunc_BOOLEAN__OBJECT_OBJECT callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;
  gboolean v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_BOOLEAN__OBJECT_OBJECT*) (marshal_data ? &marshal_data : &cc->callback);

  v_return = callback (data1,
                       g_marshal_value_peek_object (param_values + 1),
                       g_marshal_value_peek_object (param_values + 2),
                       data2);

  g_value_set_boolean (return_value, v_return);
}

/* VOID:OBJECT,UINT (inf-marshal.in:22) */
void
inf_marshal_VOID__OBJECT_UINT (GClosure     *closure,
                               GValue       *return_value G_GNUC_UNUSED,
                               guint         n_param_values,
                               const GValue *param_values,
                               gpointer      invocation_hint G_GNUC_UNUSED,
                               gpointer      marshal_data)
{
  typedef void (*GMarshalFunc_VOID__OBJECT_UINT) (gpointer     data1,
                                                  gpointer     arg_1,
                                                  guint        arg_2,
                                                  gpointer     data2);
  register GMarshalFunc_VOID__OBJECT_UINT callback;
  register GCClosure *cc = (GCClosure*) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure))
    {
      data1 = closure->data;
      data2 = g_value_peek_pointer (param_values + 0);
    }
  else
    {
      data1 = g_value_peek_pointer (param_values + 0);
      data2 = closure->data;
    }
  callback = *(GMarshalFunc_VOID__OBJECT_UINT*) (marshal_data ? &marshal_data : &cc->callback);

  callback (data1,
            g_marshal_value_peek_object (param_values + 1),
            g_marshal_value_peek_uint (param_values + 2),
            data2);
}

