/* Static libinfinity/inf-marshal.h for MSVC builds */

#ifndef __inf_marshal_MARSHAL_H__
#define __inf_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* VOID:VOID (inf-marshal.in:1) */
#define inf_marshal_VOID__VOID	g_cclosure_marshal_VOID__VOID

/* VOID:BOXED (inf-marshal.in:2) */
#define inf_marshal_VOID__BOXED	g_cclosure_marshal_VOID__BOXED

/* VOID:BOXED,DOUBLE,DOUBLE (inf-marshal.in:3) */
extern void inf_marshal_VOID__BOXED_DOUBLE_DOUBLE (GClosure     *closure,
                                                   GValue       *return_value,
                                                   guint         n_param_values,
                                                   const GValue *param_values,
                                                   gpointer      invocation_hint,
                                                   gpointer      marshal_data);

/* VOID:OBJECT (inf-marshal.in:4) */
#define inf_marshal_VOID__OBJECT	g_cclosure_marshal_VOID__OBJECT

/* VOID:UINT,UINT,OBJECT (inf-marshal.in:5) */
extern void inf_marshal_VOID__UINT_UINT_OBJECT (GClosure     *closure,
                                                GValue       *return_value,
                                                guint         n_param_values,
                                                const GValue *param_values,
                                                gpointer      invocation_hint,
                                                gpointer      marshal_data);

/* VOID:POINTER (inf-marshal.in:6) */
#define inf_marshal_VOID__POINTER	g_cclosure_marshal_VOID__POINTER

/* VOID:OBJECT,DOUBLE (inf-marshal.in:7) */
extern void inf_marshal_VOID__OBJECT_DOUBLE (GClosure     *closure,
                                             GValue       *return_value,
                                             guint         n_param_values,
                                             const GValue *param_values,
                                             gpointer      invocation_hint,
                                             gpointer      marshal_data);

/* VOID:OBJECT,POINTER (inf-marshal.in:8) */
extern void inf_marshal_VOID__OBJECT_POINTER (GClosure     *closure,
                                              GValue       *return_value,
                                              guint         n_param_values,
                                              const GValue *param_values,
                                              gpointer      invocation_hint,
                                              gpointer      marshal_data);

/* VOID:POINTER,UINT (inf-marshal.in:9) */
extern void inf_marshal_VOID__POINTER_UINT (GClosure     *closure,
                                            GValue       *return_value,
                                            guint         n_param_values,
                                            const GValue *param_values,
                                            gpointer      invocation_hint,
                                            gpointer      marshal_data);

/* VOID:UINT (inf-marshal.in:10) */
#define inf_marshal_VOID__UINT	g_cclosure_marshal_VOID__UINT

/* VOID:UINT,UINT (inf-marshal.in:11) */
extern void inf_marshal_VOID__UINT_UINT (GClosure     *closure,
                                         GValue       *return_value,
                                         guint         n_param_values,
                                         const GValue *param_values,
                                         gpointer      invocation_hint,
                                         gpointer      marshal_data);

/* VOID:OBJECT,BOOLEAN (inf-marshal.in:12) */
extern void inf_marshal_VOID__OBJECT_BOOLEAN (GClosure     *closure,
                                              GValue       *return_value,
                                              guint         n_param_values,
                                              const GValue *param_values,
                                              gpointer      invocation_hint,
                                              gpointer      marshal_data);

/* VOID:UINT,BOXED,OBJECT (inf-marshal.in:13) */
extern void inf_marshal_VOID__UINT_BOXED_OBJECT (GClosure     *closure,
                                                 GValue       *return_value,
                                                 guint         n_param_values,
                                                 const GValue *param_values,
                                                 gpointer      invocation_hint,
                                                 gpointer      marshal_data);

/* VOID:UINT,INT (inf-marshal.in:14) */
extern void inf_marshal_VOID__UINT_INT (GClosure     *closure,
                                        GValue       *return_value,
                                        guint         n_param_values,
                                        const GValue *param_values,
                                        gpointer      invocation_hint,
                                        gpointer      marshal_data);

/* VOID:OBJECT,OBJECT (inf-marshal.in:15) */
extern void inf_marshal_VOID__OBJECT_OBJECT (GClosure     *closure,
                                             GValue       *return_value,
                                             guint         n_param_values,
                                             const GValue *param_values,
                                             gpointer      invocation_hint,
                                             gpointer      marshal_data);

/* VOID:BOXED,OBJECT (inf-marshal.in:16) */
extern void inf_marshal_VOID__BOXED_OBJECT (GClosure     *closure,
                                            GValue       *return_value,
                                            guint         n_param_values,
                                            const GValue *param_values,
                                            gpointer      invocation_hint,
                                            gpointer      marshal_data);

/* VOID:BOXED,BOXED,OBJECT (inf-marshal.in:17) */
extern void inf_marshal_VOID__BOXED_BOXED_OBJECT (GClosure     *closure,
                                                  GValue       *return_value,
                                                  guint         n_param_values,
                                                  const GValue *param_values,
                                                  gpointer      invocation_hint,
                                                  gpointer      marshal_data);

/* VOID:OBJECT,OBJECT,BOOLEAN (inf-marshal.in:18) */
extern void inf_marshal_VOID__OBJECT_OBJECT_BOOLEAN (GClosure     *closure,
                                                     GValue       *return_value,
                                                     guint         n_param_values,
                                                     const GValue *param_values,
                                                     gpointer      invocation_hint,
                                                     gpointer      marshal_data);

/* VOID:DOUBLE (inf-marshal.in:19) */
#define inf_marshal_VOID__DOUBLE	g_cclosure_marshal_VOID__DOUBLE

/* VOID:ENUM (inf-marshal.in:20) */
#define inf_marshal_VOID__ENUM	g_cclosure_marshal_VOID__ENUM

/* BOOLEAN:OBJECT,OBJECT (inf-marshal.in:21) */
extern void inf_marshal_BOOLEAN__OBJECT_OBJECT (GClosure     *closure,
                                                GValue       *return_value,
                                                guint         n_param_values,
                                                const GValue *param_values,
                                                gpointer      invocation_hint,
                                                gpointer      marshal_data);

/* VOID:OBJECT,UINT (inf-marshal.in:22) */
extern void inf_marshal_VOID__OBJECT_UINT (GClosure     *closure,
                                           GValue       *return_value,
                                           guint         n_param_values,
                                           const GValue *param_values,
                                           gpointer      invocation_hint,
                                           gpointer      marshal_data);

G_END_DECLS

#endif /* __inf_marshal_MARSHAL_H__ */

