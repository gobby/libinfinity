/* infcinote - Collaborative notetaking application
 * Copyright (C) 2007-2011 Armin Burgmeier <armin@arbur.net>
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
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <libinfinity/client/infc-request-manager.h>
#include <libinfinity/common/inf-request.h>
#include <libinfinity/common/inf-xml-util.h>
#include <libinfinity/common/inf-error.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-i18n.h>

#include <gobject/gvaluecollector.h>

#include <string.h>
#include <errno.h>

typedef struct _InfcRequestManagerForeachData InfcRequestManagerForeachData;
struct _InfcRequestManagerForeachData {
  InfcRequestManagerForeachFunc func;
  gpointer user_data;
  const gchar* name;
};

typedef struct _InfcRequestManagerPrivate InfcRequestManagerPrivate;
struct _InfcRequestManagerPrivate {
  GHashTable* requests;
  guint seq_id;
  guint seq_counter;
};

enum {
  PROP_0,

  PROP_SEQUENCE_ID
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
  gchar* type;

  request = INFC_REQUEST(value);
  foreach_data = (InfcRequestManagerForeachData*)user_data;
  g_object_get(G_OBJECT(request), "type", &type, NULL);

  if(foreach_data->name == NULL || strcmp(foreach_data->name, type) == 0)
  {
    foreach_data->func(request, foreach_data->user_data);
  }

  g_free(type);
}

/* TODO: inf_protocol_version_parse() uses a very similar routine. We should
 * avoid the code duplication. */
static gboolean
infc_request_manager_parse_seq(const gchar* seq,
                               guint* seq_id,
                               guint* seq_num,
                               GError** error)
{
  gchar* endptr;
  unsigned long sid;
  unsigned long snum;

  errno = 0;
  sid = strtoul(seq, &endptr, 10);
  if(errno == ERANGE || sid > (unsigned long)G_MAXUINT)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_SEQ,
      "%s",
      _("Sequence identifier causes overflow")
    );

    return FALSE;
  }

  if(*endptr != '/')
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_SEQ,
      "%s",
      _("Sequence components are not separated by '/'")
    );

    return FALSE;
  }

  errno = 0;
  snum = strtoul(endptr + 1, &endptr, 10);
  if(errno == ERANGE || snum > (unsigned long)G_MAXUINT)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_SEQ,
      "%s",
      _("Sequence number causes overflow")
    );

    return FALSE;
  }

  if(*endptr != '\0')
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_SEQ,
      "%s",
      _("Trailing characters after sequence number")
    );

    return FALSE;
  }

  if(seq_id) *seq_id = sid;
  if(seq_num) *seq_num = snum;
  return TRUE;
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

  priv->seq_id = 0;
  priv->seq_counter = 0;
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
infc_request_manager_set_property(GObject* object,
                                  guint prop_id,
                                  const GValue* value,
                                  GParamSpec* pspec)
{
  InfcRequestManager* request_manager;
  InfcRequestManagerPrivate* priv;

  request_manager = INFC_REQUEST_MANAGER(object);
  priv = INFC_REQUEST_MANAGER_PRIVATE(request_manager);

  switch(prop_id)
  {
  case PROP_SEQUENCE_ID:
    priv->seq_id = g_value_get_uint(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_request_manager_get_property(GObject* object,
                                  guint prop_id,
                                  GValue* value,
                                  GParamSpec* pspec)
{
  InfcRequestManager* request_manager;
  InfcRequestManagerPrivate* priv;

  request_manager = INFC_REQUEST_MANAGER(object);
  priv = INFC_REQUEST_MANAGER_PRIVATE(request_manager);

  switch(prop_id)
  {
  case PROP_SEQUENCE_ID:
    g_value_set_uint(value, priv->seq_id);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_request_manager_request_add(InfcRequestManager* manager,
                                 InfcRequest* request)
{
  InfcRequestManagerPrivate* priv;
  guint seq;
  gpointer seq_ptr;

  priv = INFC_REQUEST_MANAGER_PRIVATE(manager);
  g_object_get(G_OBJECT(request), "seq", &seq, NULL);
  seq_ptr = GUINT_TO_POINTER(seq);

  g_assert(g_hash_table_lookup(priv->requests, seq_ptr) == NULL);
  g_hash_table_insert(priv->requests, seq_ptr, request);

  g_object_ref(G_OBJECT(request));
}

static void
infc_request_manager_request_remove(InfcRequestManager* manager,
                                    InfcRequest* request)
{
  InfcRequestManagerPrivate* priv;
  guint seq;
  gpointer seq_ptr;

  priv = INFC_REQUEST_MANAGER_PRIVATE(manager);
  g_object_get(G_OBJECT(request), "seq", &seq, NULL);
  seq_ptr = GUINT_TO_POINTER(seq);

  g_assert(g_hash_table_lookup(priv->requests, seq_ptr) != NULL);
  g_hash_table_remove(priv->requests, seq_ptr);
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
  object_class->set_property = infc_request_manager_set_property;
  object_class->get_property = infc_request_manager_get_property;

  request_manager_class->request_add = infc_request_manager_request_add;
  request_manager_class->request_remove = infc_request_manager_request_remove;

  g_object_class_install_property(
    object_class,
    PROP_SEQUENCE_ID,
    g_param_spec_uint(
      "sequence-id",
      "Sequence ID",
      "The local sequence identifier",
      0,
      G_MAXUINT,
      0,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY
    )
  );

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

/**
 * infc_request_manager_new:
 * @seq_id: The local sequence ID.
 *
 * Creates a new #InfcRequestManager.
 *
 * Return Value: A newly allocated #InfcRequestManager.
 **/
InfcRequestManager*
infc_request_manager_new(guint seq_id)
{
  GObject* object;
  object =
    g_object_new(INFC_TYPE_REQUEST_MANAGER, "sequence-id", seq_id, NULL);
  return INFC_REQUEST_MANAGER(object);
}

/**
 * infc_request_manager_add_request:
 * @manager: A #InfcRequestManager.
 * @request_type: The type of request to add, such as %INFC_TYPE_NODE_REQUEST.
 * @request_name: The name of the request, such as "explore-node" or
 * "subscribe-session".
 * @first_property_name: The first property name apart from name and seq to
 * set for the new request.
 * @...: The value of the first property, followed optionally by more
 * name/value pairs, followed by %NULL.
 *
 * Adds a request to the request manager.
 *
 * Return Value: The generated #InfcRequest (actually of type @request_type).
 **/
InfcRequest*
infc_request_manager_add_request(InfcRequestManager* manager,
                                 GType request_type,
                                 const gchar* request_name,
                                 const gchar* first_property_name,
                                 ...)
{
  InfcRequest* request;

  va_list arglist;
  va_start(arglist, first_property_name);

  request = infc_request_manager_add_request_valist(
    manager,
    request_type,
    request_name,
    first_property_name,
    arglist
  );

  va_end(arglist);
  return request;
}

/**
 * infc_request_manager_add_request_valist:
 * @manager: A #InfcRequestManager.
 * @request_type: The type of request to add, such as %INFC_TYPE_NODE_REQUEST.
 * @request_name: The name of the request, such as &quot;explore-node&quot; or
 * &quot;subscribe-session&quot;
 * @first_property_name: The first property name apart from name and seq to
 * set for the new request.
 * @arglist: The value of the first property, followed optionally by more
 * name/value pairs, followed by %NULL.
 *
 * Adds a request to the request manager.
 *
 * Return Value: The generated #InfcRequest (actually of type @request_type).
 */
InfcRequest*
infc_request_manager_add_request_valist(InfcRequestManager* manager,
                                        GType request_type,
                                        const gchar* request_name,
                                        const gchar* first_property_name,
                                        va_list arglist)
{
  InfcRequestManagerPrivate* priv;
  GObjectClass* request_class;
  InfcRequest* request;
  GParameter* params;
  const gchar* prop_name;
  gsize param_size;
  gsize param_alloc;
  guint seq;
  gchar* error;
  GParamSpec* pspec;
  guint i;

  g_return_val_if_fail(INFC_IS_REQUEST_MANAGER(manager), NULL);
  g_return_val_if_fail(request_name != NULL, NULL);

  request_class = g_type_class_ref(request_type);
  g_return_val_if_fail(request_class != NULL, NULL);

  priv = INFC_REQUEST_MANAGER_PRIVATE(manager);
  seq = priv->seq_counter;

  g_assert(
    g_hash_table_lookup(priv->requests, GUINT_TO_POINTER(seq)) == NULL
  );

  param_size = 0;
  param_alloc = 16;
  params = g_malloc0(param_alloc * sizeof(GParameter));

  params[param_size].name = "type";
  g_value_init(&params[param_size].value, G_TYPE_STRING);
  g_value_set_static_string(&params[param_size].value, request_name);
  ++ param_size;

  params[param_size].name = "seq";
  g_value_init(&params[param_size].value, G_TYPE_UINT);
  g_value_set_uint(&params[param_size].value, seq);
  ++ param_size;

  prop_name = first_property_name;
  error = NULL;

  while(prop_name != NULL)
  {
    pspec = g_object_class_find_property(request_class, prop_name);
    if(pspec == NULL)
    {
      g_warning(
        "%s: object class `%s' has no attribute named `%s'",
        G_STRFUNC,
        g_type_name(request_type),
        prop_name
      );

      break;
    }

    if(param_size >= param_alloc)
    {
      param_alloc += 16;
      params = g_realloc(params, param_alloc * sizeof(GParameter));
    }

    params[param_size].name = prop_name;
    g_value_init(&params[param_size].value, G_PARAM_SPEC_VALUE_TYPE(pspec));
    G_VALUE_COLLECT(&params[param_size].value, arglist, 0, &error);
    if(error != NULL)
    {
      g_warning("%s: %s", G_STRFUNC, error);
      g_value_unset(&params[param_size].value);
      g_free(error);
      break;
    }

    ++ param_size;
    prop_name = va_arg(arglist, const gchar*);
  }

  if(prop_name == NULL)
  {
    request = INFC_REQUEST(g_object_newv(request_type, param_size, params));
    g_hash_table_insert(priv->requests, GUINT_TO_POINTER(seq), request);
    ++ priv->seq_counter;
  }
  else
  {
    /* An error occured. We do not use GError here tough, because this is a
     * most likely a bug in someone's code (if not, it is one in
     * libinfinity's code). */
    request = NULL;
  }

  for(i = 0; i < param_size; ++ i)
    g_value_unset(&params[i].value);
  g_free(params);
  g_type_class_unref(request_class);

  return request;
}

/**
 * infc_request_manager_remove_request:
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

  g_object_ref(G_OBJECT(request));

  g_signal_emit(
    G_OBJECT(manager),
    request_manager_signals[REQUEST_REMOVE],
    0,
    request
  );

  g_object_unref(G_OBJECT(request));
}

/**
 * infc_request_manager_fail_request:
 * @manager: A #InfcRequestManager.
 * @request: A #InfcRequest that has previously been added to @manager.
 * @error: Error information why the request failed.
 *
 * Marks @request as failed using inf_request_fail() and then removes the
 * request from the manager.
 **/
void
infc_request_manager_fail_request(InfcRequestManager* manager,
                                  InfcRequest* request,
                                  GError* error)
{
  g_return_if_fail(INFC_IS_REQUEST_MANAGER(manager));
  g_return_if_fail(INFC_IS_REQUEST(request));
  g_return_if_fail(error != NULL);

  g_object_ref(request);
  infc_request_manager_remove_request(manager, request);
  inf_request_fail(INF_REQUEST(request), error);
  g_object_unref(request);
}

/**
 * infc_request_manager_clear:
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

/**
 * infc_request_manager_get_request_by_seq:
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

/**
 * infc_request_manager_get_request_by_xml:
 * @manager: A #InfcRequestManager.
 * @name: Name of the expected request. May be %NULL to allow all requests.
 * @xml: XML node that is supposed to contain a "seq" attribute.
 * @error: Location to store error information.
 *
 * Looks whether there is a "seq" attribute in @xml. If not, the function
 * returns %NULL (without setting @error). Otherwise, it returns the request
 * with the given seq and name, if the sequence ID matches. If the "seq"
 * attribute is set but the actual request is not present (or has another
 * name), the function returns %NULL and @error is set.
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
  gboolean has_seq;
  guint seq_id;
  guint seq;
  gchar* request_type;

  g_return_val_if_fail(INFC_IS_REQUEST_MANAGER(manager), NULL);
  g_return_val_if_fail(xml != NULL, NULL);

  priv = INFC_REQUEST_MANAGER_PRIVATE(manager);
  request = NULL;

  seq_attr = inf_xml_util_get_attribute(xml, "seq");
  if(!seq_attr) return NULL;

  has_seq = infc_request_manager_parse_seq(
    (const gchar*)seq_attr,
    &seq_id,
    &seq,
    error
  );

  if(!has_seq) return NULL;

  /* Not our seq ID */
  if(seq_id != priv->seq_id) return NULL;

  request = infc_request_manager_get_request_by_seq(manager, seq);
  if(request == NULL)
  {
    g_set_error(
      error,
      inf_request_error_quark(),
      INF_REQUEST_ERROR_INVALID_SEQ,
      "%s",
      _("The request contains an unknown sequence number")
    );
  }
  else
  {
    g_object_get(G_OBJECT(request), "type", &request_type, NULL);
    if(name != NULL && strcmp(name, request_type) != 0)
    {
      g_set_error(
        error,
        inf_request_error_quark(),
        INF_REQUEST_ERROR_INVALID_SEQ,
        _("The request contains a sequence number refering to a request of "
          "type '%s', but a request of type '%s' was expected"),
        request_type,
        name
      );

      request = NULL;
    }

    g_free(request_type);
  }

  return request;
}

/**
 * infc_request_manager_get_request_by_xml_required:
 * @manager: A #InfcRequestManager.
 * @name: Name of the expected request. May be %NULL to allow all requests.
 * @xml: XML node that is supposed to contain a "seq" attribute.
 * @error: Location to store error information.
 *
 * Looks whether there is a "seq" attribute in @xml. If so, it returns the
 * request with the given seq and name, if the sequence ID matches. If the
 * "seq" attribute is not set or the actual request is not present (or has
 * another name), the function returns %NULL and @error is set.
 *
 * Return Value: The resulting request, or %NULL if an error occured.
 **/
InfcRequest*
infc_request_manager_get_request_by_xml_required(InfcRequestManager* manager,
                                                 const gchar* name,
                                                 xmlNodePtr xml,
                                                 GError** error)
{
  InfcRequest* request;
  GError* own_error;

  own_error = NULL;
  request = infc_request_manager_get_request_by_xml(
    manager,
    name,
    xml,
    &own_error
  );

  if(request == NULL)
  {
    if(own_error == NULL)
    {
      g_set_error(
        error,
        inf_request_error_quark(),
        INF_REQUEST_ERROR_INVALID_SEQ,
        "%s",
        _("The request does not contain a sequence number, but one "
          "is required")
      );
    }
    else
    {
      g_propagate_error(error, own_error);
    }
  }

  return request;
}

/**
 * infc_request_manager_foreach_request:
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

/**
 * infc_request_manager_foreach_named_request:
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

/* vim:set et sw=2 ts=2: */
