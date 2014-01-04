/* infcinote - Collaborative notetaking application
 * Copyright (C) 2007-2014 Armin Burgmeier <armin@arbur.net>
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

/**
 * SECTION:infc-certificate-request
 * @title: InfcCertificateRequest
 * @short_description: Asynchronous request related to a certificate.
 * @include: libinfinity/client/infc-certificate-request.h
 * @see_also: #InfcBrowser, #InfcRequest
 * @stability: Unstable
 *
 * #InfcCertificateRequest represents an asynchronous operation which is related to
 * requesting a certificate from the server. The request finishes
 * when the server has sent a reply and will emit the
 * #InfcCertificateRequest::finished signal with the created certificate.
 */

#include <libinfinity/client/infc-certificate-request.h>
#include <libinfinity/client/infc-request.h>
#include <libinfinity/common/inf-request.h>
#include <libinfinity/inf-marshal.h>

typedef struct _InfcCertificateRequestPrivate InfcCertificateRequestPrivate;
struct _InfcCertificateRequestPrivate {
  guint seq;
};

enum {
  FINISHED,

  LAST_SIGNAL
};

enum {
  PROP_0,

  PROP_TYPE,
  PROP_SEQ
};

#define INFC_CERTIFICATE_REQUEST_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INFC_TYPE_CERTIFICATE_REQUEST, InfcCertificateRequestPrivate))

static GObjectClass* parent_class;
static guint certificate_request_signals[LAST_SIGNAL];

static void
infc_certificate_request_init(GTypeInstance* instance,
                       gpointer g_class)
{
  InfcCertificateRequest* request;
  InfcCertificateRequestPrivate* priv;

  request = INFC_CERTIFICATE_REQUEST(instance);
  priv = INFC_CERTIFICATE_REQUEST_PRIVATE(request);

  priv->seq = 0;
}

static void
infc_certificate_request_finalize(GObject* object)
{
  InfcCertificateRequest* request;
  InfcCertificateRequestPrivate* priv;

  request = INFC_CERTIFICATE_REQUEST(object);
  priv = INFC_CERTIFICATE_REQUEST_PRIVATE(request);

  if(G_OBJECT_CLASS(parent_class)->finalize != NULL)
    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
infc_certificate_request_set_property(GObject* object,
                               guint prop_id,
                               const GValue* value,
                               GParamSpec* pspec)
{
  InfcCertificateRequest* request;
  InfcCertificateRequestPrivate* priv;

  request = INFC_CERTIFICATE_REQUEST(object);
  priv = INFC_CERTIFICATE_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    /* this can only have request-certificate type, otherwise it would not be a
     * InfcCertificateRequest. */
    g_assert(strcmp(g_value_get_string(value), "request-certificate") == 0);
    break;
  case PROP_SEQ:
    g_assert(priv->seq == 0); /* construct only */
    priv->seq = g_value_get_uint(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_certificate_request_get_property(GObject* object,
                               guint prop_id,
                               GValue* value,
                               GParamSpec* pspec)
{
  InfcCertificateRequest* request;
  InfcCertificateRequestPrivate* priv;

  request = INFC_CERTIFICATE_REQUEST(object);
  priv = INFC_CERTIFICATE_REQUEST_PRIVATE(request);

  switch(prop_id)
  {
  case PROP_TYPE:
    g_value_set_static_string(value, "request-certificate");
    break;
  case PROP_SEQ:
    g_value_set_uint(value, priv->seq);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
infc_certificate_request_request_fail(InfRequest* request,
                                      const GError* error)
{
  g_signal_emit(
    request,
    certificate_request_signals[FINISHED],
    0,
    NULL,
    error
  );
}

static void
infc_certificate_request_class_init(gpointer g_class,
                             gpointer class_data)
{
  GObjectClass* object_class;
  InfcCertificateRequestClass* request_class;

  object_class = G_OBJECT_CLASS(g_class);
  request_class = INFC_CERTIFICATE_REQUEST_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfcCertificateRequestPrivate));

  object_class->finalize = infc_certificate_request_finalize;
  object_class->set_property = infc_certificate_request_set_property;
  object_class->get_property = infc_certificate_request_get_property;

  request_class->finished = NULL;

  /**
   * InfcCertificateRequest::finished:
   * @request: The #InfcUserRequest object emitting the signal.
   * @error: Reason of request failure in the case of an error.
   *
   * This signal is emitted when the chat request finishes. If it finishes
   * successfully, @error will be %NULL, othewise it will be non-%NULL and
   * contains error information.
   */
  certificate_request_signals[FINISHED] = g_signal_new(
    "finished",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfcCertificateRequestClass, finished),
    NULL, NULL,
    inf_marshal_VOID__BOXED_POINTER,
    G_TYPE_NONE,
    2,
    INF_TYPE_CERTIFICATE_CHAIN,
    G_TYPE_POINTER /* GError* */
  );

  g_object_class_override_property(object_class, PROP_TYPE, "type");
  g_object_class_override_property(object_class, PROP_SEQ, "seq");
}

static void
infc_certificate_request_request_init(gpointer g_iface,
                                      gpointer iface_data)
{
  InfRequestIface* iface;
  iface = (InfRequestIface*)g_iface;

  iface->fail = infc_certificate_request_request_fail;
}

static void
infc_certificate_request_infc_request_init(gpointer g_iface,
                                           gpointer iface_data)
{
  InfcRequestIface* iface;
  iface = (InfcRequestIface*)g_iface;
}

GType
infc_certificate_request_get_type(void)
{
  static GType certificate_request_type = 0;

  if(!certificate_request_type)
  {
    static const GTypeInfo certificate_request_type_info = {
      sizeof(InfcCertificateRequestClass),  /* class_size */
      NULL,                          /* base_init */
      NULL,                          /* base_finalize */
      infc_certificate_request_class_init,  /* class_init */
      NULL,                          /* class_finalize */
      NULL,                          /* class_data */
      sizeof(InfcCertificateRequest),       /* instance_size */
      0,                             /* n_preallocs */
      infc_certificate_request_init,        /* instance_init */
      NULL                           /* value_table */
    };

    static const GInterfaceInfo request_info = {
      infc_certificate_request_request_init,
      NULL,
      NULL
    };

    static const GInterfaceInfo infc_request_info = {
      infc_certificate_request_infc_request_init,
      NULL,
      NULL
    };

    certificate_request_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfcCertificateRequest",
      &certificate_request_type_info,
      0
    );

    g_type_add_interface_static(
      certificate_request_type,
      INF_TYPE_REQUEST,
      &request_info
    );

    g_type_add_interface_static(
      certificate_request_type,
      INFC_TYPE_REQUEST,
      &infc_request_info
    );
  }

  return certificate_request_type;
}

/**
 * infc_certificate_request_finished:
 * @request: A #InfcCertificateRequest.
 * @cert: The created certificate, or %NULL.
 * @error: A #GError explaining the reason for request failure, or %NULL.
 *
 * Emits the #InfcCertificateRequest::finished signal on @request. @cert is the
 * created certificate, or %NULL if the request failed. The certificate chain
 * includes all the issuers up to the CA. @error should be %NULL if the
 * request finished successfully, otherwise it should contain appropriate
 * error information.
 **/
void
infc_certificate_request_finished(InfcCertificateRequest* request,
                                  InfCertificateChain* cert,
                                  const GError* error)
{
  g_signal_emit(
    G_OBJECT(request),
    certificate_request_signals[FINISHED],
    0,
    cert,
    error
  );
}

/* vim:set et sw=2 ts=2: */
