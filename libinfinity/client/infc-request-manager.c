/* infcinote - Collaborative notetaking application
 * Copyright (C) 2007 Armin Burgmeier
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <libinfinity/client/infc-request-manager.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-marshal.h>

typedef struct _InfcRequestManagerForeachData InfcRequestManagerForeachData;
struct _InfcRequestManagerForeachData {
  InfcRequestManagerForeachFunc func;
  gpointer user_data;
  const gchar* name;
};

typedef struct _InfcRequestManagerPrivate InfcRequestManagerPrivate;
struct _InfcRequestManagerPrivate {
  GHashTable* requests;
};

enum {
  REQUEST_ADD,
  REQUEST_REMOVE,

  LAST_SIGNAL
};

#define INFC_REQUEST_MANAGER_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_REQUEST_MANAGER, InfcRequestManagerPrivate))

static GObjectClass* parent_class;
static guint request_manager_signals[LAST_SIGNAL];

static void
infc_request_manager_foreach_request_func(gpointer key,
                                          gpointer value,
                                          gpointer user_data)
{
  InfcRequest* request;
  InfcRequestManagerForeachData* foreach_data;

  request = INFC_REQUEST(value);
  foreach_data = (InfcRequestManagerForeachData*)user_data;

  if(foreach_data->name == NULL ||
     strcmp(foreach_data->name, infc_request_get_name(request)) == 0)
  {
    foreach_data->func(request, foreach_data->user_data);
  }
}

static void
infc_request_manager_init(GTypeInstance* instance,
                          gpointer g_class)
{
  InfcRequestManager* request_manager;
  InfcRequestManagerPrivate* priv;

  request_manager = INFC_REQUEST_MANAGER(instance);
  priv = INFC_REQUEST_MANAGER_PRIVATE(request_manager);

  priv->requests = g_hash_table_new_full(
    NULL,
    NULL,
    NULL,
    (GDestroyNotify)g_object_unref
  );
}

static void
infc_request_manager_dispose(GObject* object)
{
  InfcRequestManager* request_manager;
  InfcRequestManagerPrivate* priv;

  request_manager = INFC_REQUEST_MANAGER(object);
  priv = INFC_REQUEST_MANAGER_PRIVATE(request_manager);

  g_hash_table_destroy(priv->requests);
  priv->requests = NULL;

  if(parent_class->dispose != NULL)
    parent_class->dispose(object);
}

static void
infc_request_manager_request_add(InfcRequestManager* manager,
                                 InfcRequest* request)
{
  InfcRequestManagerPrivate* priv;
  gpointer seq;

  priv = INFC_REQUEST_MANAGER_PRIVATE(manager);
  seq = GUINT_TO_POINTER(infc_request_get_seq(request));

  g_assert(g_hash_table_lookup(priv->requests, seq) == NULL);
  g_hash_table_insert(priv->requests, seq, request);

  g_object_ref(G_OBJECT(request));
}

static void
infc_request_manager_request_remove(InfcRequestManager* manager,
                                    InfcRequest* request)
{
  InfcRequestManagerPrivate* priv;
  gpointer seq;

  priv = INFC_REQUEST_MANAGER_PRIVATE(manager);
  seq = GUINT_TO_POINTER(infc_request_get_seq(request));

  g_object_unref(G_OBJECT(request));

  g_assert(g_hash_table_lookup(priv->requests, seq) != NULL);
  g_hash_table_remove(priv->requests, seq);
}

static void
infc_request_manager_class_init(gpointer g_class,
                                gpointer class_data)
{
  GObjectClass* object_class;
  InfcRequestManagerClass* request_manager_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_manager_class = INFC_REQUEST_MANAGER_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcRequestManagerPrivate));

  object_class->dispose = infc_request_manager_dispose;

  request_manager_class->request_add = infc_request_manager_request_add;
  request_manager_class->request_remove = infc_request_manager_request_remove;

  request_manager_signals[REQUEST_ADD] = g_signal_new(
    "request-add",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcRequestManagerClass, request_add),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INFC_TYPE_REQUEST
  );

  request_manager_signals[REQUEST_REMOVE] = g_signal_new(
    "request-remove",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcRequestManagerClass, request_remove),
    NULL, NULL,
    inf_marshal_VOID__OBJECT,
    G_TYPE_NONE,
    1,
    INFC_TYPE_REQUEST
  );
}

GType
infc_request_manager_get_type(void)
{
  static GType request_manager_type = 0;

  if(!request_manager_type)
  {
    static const GTypeInfo request_manager_type_info = {
      sizeof(InfcRequestManagerClass),  /* class_size */
      NULL,                             /* base_init */
      NULL,                             /* base_finalize */
      infc_request_manager_class_init,  /* class_init */
      NULL,                             /* class_finalize */
      NULL,                             /* class_data */
      sizeof(InfcRequestManager),       /* instance_size */
      0,                                /* n_preallocs */
      infc_request_manager_init,        /* instance_init */
      NULL                              /* value_table */
    };

    request_manager_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfcRequestManager",
      &request_manager_type_info,
      0
    );
  }

  return request_manager_type;
}

/** infc_request_manager_new:
 *
 * Creates a new #InfcRequestManager.
 *
 * Return Value: A newly allocated #InfcRequestManager.
 **/
InfcRequestManager*
infc_request_manager_new(void)
{
  GObject* object;
  object = g_object_new(INFC_TYPE_REQUEST_MANAGER, NULL);
  return INFC_REQUEST_MANAGER(object);
}

/** infc_request_manager_add_request:
 *
 * @manager: A #InfcRequestManager.
 * @request: A #infcRequest.
 *
 * Adds a request to the request manager.
 **/
void
infc_request_manager_add_request(InfcRequestManager* manager,
                                 InfcRequest* request)
{
  g_return_if_fail(INFC_IS_REQUEST_MANAGER(manager));
  g_return_if_fail(INFC_IS_REQUEST(request));

  g_signal_emit(
    G_OBJECT(manager),
    request_manager_signals[REQUEST_ADD],
    0,
    request
  );
}

/** infc_request_manager_remove_request:
 *
 * @manager: A #InfcRequestManager.
 * @request: A #InfcRequest that has previously been added to @manager.
 *
 * Removes a request from the request manager.
 **/
void
infc_request_manager_remove_request(InfcRequestManager* manager,
                                    InfcRequest* request)
{
  g_return_if_fail(INFC_IS_REQUEST_MANAGER(manager));
  g_return_if_fail(INFC_IS_REQUEST(request));

  g_signal_emit(
    G_OBJECT(manager),
    request_manager_signals[REQUEST_REMOVE],
    0,
    request
  );
}

/** infc_request_manager_fail_request:
 *
 * @manager: A #InfcRequestManager.
 * @request: A #InfcRequest that has previously been added to @manager.
 * @error: Error information why the request failed.
 *
 * Emits the "failed" signal on @request and then removes the request from
 * the manager.
 **/
void
infc_request_manager_fail_request(InfcRequestManager* manager,
                                  InfcRequest* request,
                                  GError* error)
{
  g_return_if_fail(INFC_IS_REQUEST_MANAGER(manager));
  g_return_if_fail(INFC_IS_REQUEST(request));
  g_return_if_fail(error != NULL);

  infc_request_failed(request, error);
  infc_request_manager_remove_request(manager, request);
}

/** infc_request_manager_clear:
 *
 * @manager: A #InfcRequestManager.
 *
 * Removes all the requests in @manager.
 **/
void
infc_request_manager_clear(InfcRequestManager* manager)
{
  g_return_if_fail(INFC_IS_REQUEST_MANAGER(manager));
  g_hash_table_remove_all(INFC_REQUEST_MANAGER_PRIVATE(manager)->requests);
}

/** infc_request_manager_get_request_by_seq:
 *
 * @manager: A #InfcRequestManager.
 * @seq: The sequence number to which the request should be retrieved.
 *
 * Returns a previously-added request that has the given seq.
 *
 * Return Value: The request with the given seq, or %NULL if there is no such
 * request.
 **/
InfcRequest*
infc_request_manager_get_request_by_seq(InfcRequestManager* manager,
                                        guint seq)
{
  InfcRequestManagerPrivate* priv;

  g_return_val_if_fail(INFC_IS_REQUEST_MANAGER(manager), NULL);

  priv = INFC_REQUEST_MANAGER_PRIVATE(manager);

  return INFC_REQUEST(
    g_hash_table_lookup(priv->requests, GUINT_TO_POINTER(seq))
  );
}

/** infc_request_manager_get_request_by_xml:
 *
 * @manager: A #InfcRequestManager.
 * @name: Name of the expected request. May be %NULL to allow all requests.
 * @xml: XML node that is supposed to contain a "seq" attribute.
 * @error: Location to store error information.
 *
 * Looks whether there is a "seq" attribute in @xml. If not, the function
 * returns %NULL (without setting @error). Otherwise, it returns the request
 * with the given seq and name (if any). If the "seq" attribute is set but
 * the actual request is not present (or has another name), the function
 * returns %NULL and @error is set.
 *
 * Return Value: The resulting request, or %NULL if the "seq" attribute was
 * not present or an error occured.
 **/
InfcRequest*
infc_request_manager_get_request_by_xml(InfcRequestManager* manager,
                                        const gchar* name,
                                        xmlNodePtr xml,
                                        GError** error)
{
  InfcRequestManagerPrivate* priv;
  InfcRequest* request;
  xmlChar* seq_attr;
  guint seq;

  g_return_val_if_fail(INFC_IS_REQUEST_MANAGER(manager), NULL);
  g_return_val_if_fail(xml != NULL, NULL);

  priv = INFC_REQUEST_MANAGER_PRIVATE(manager);
  request = NULL;
  seq_attr = xmlGetProp(xml, (const xmlChar*)"seq");

  if(seq_attr != NULL)
  {
    seq = strtoul((const gchar*)seq, NULL, 10);
    xmlFree(seq_attr);

    request = infc_request_manager_get_request_by_seq(manager, seq);
    if(request == NULL)
    {
      g_set_error(
        error,
        inf_request_error_quark(),
        INF_REQUEST_ERROR_INVALID_SEQ,
        "The request contains an unknown sequence number"
      );
    }
    else
    {
      if(name != NULL &&
         strcmp(name, infc_request_get_name(request)) != 0)
      {
        g_set_error(
          error,
          inf_request_error_quark(),
          INF_REQUEST_ERROR_INVALID_SEQ,
          "The request contains a sequence number refering to a request of "
          "type '%s', but a request of type '%s' was expected",
          infc_request_get_name(request),
          name
        );

        request = NULL;
      }
    }
  }

  return request;
}

/** infc_request_manager_foreach_request:
 *
 * @manager: A #InfcRequestManager.
 * @func: The function to be called.
 * @user_data: Arbitrary data to be passed to @func.
 *
 * Calls the given function for each request that has been added to the
 * request manager.
 **/
void
infc_request_manager_foreach_request(InfcRequestManager* manager,
                                     InfcRequestManagerForeachFunc func,
                                     gpointer user_data)
{
  InfcRequestManagerPrivate* priv;
  InfcRequestManagerForeachData data;

  g_return_if_fail(INFC_IS_REQUEST_MANAGER(manager));
  g_return_if_fail(func != NULL);

  priv = INFC_REQUEST_MANAGER_PRIVATE(manager);

  data.func = func;
  data.user_data = user_data;
  data.name = NULL;

  g_hash_table_foreach(
    priv->requests,
    infc_request_manager_foreach_request_func,
    &data
  );
}

/** infc_request_manager_foreach_named_request:
 *
 * @manager: A #InfcRequestManager.
 * @name: The name of the request to look for.
 * @func: The function to be called.
 * @user_data: Arbitrary data to be passed to @func.
 *
 * Calls the given function for each request that has been added to the
 * request manager that has the name @name.
 **/
void
infc_request_manager_foreach_named_request(InfcRequestManager* manager,
                                           const gchar* name,
                                           InfcRequestManagerForeachFunc func,
                                           gpointer user_data)
{
  InfcRequestManagerPrivate* priv;
  InfcRequestManagerForeachData data;

  g_return_if_fail(INFC_IS_REQUEST_MANAGER(manager));
  g_return_if_fail(func != NULL);

  priv = INFC_REQUEST_MANAGER_PRIVATE(manager);

  data.func = func;
  data.user_data = user_data;
  data.name = name;

  g_hash_table_foreach(
    priv->requests,
    infc_request_manager_foreach_request_func,
    &data
  );
}
