/* libinfinity - a GObject-based infinote implementation
 * Copyright (C) 2007, 2008, 2009 Armin Burgmeier <armin@arbur.net>
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

#include <libinfinity/adopted/inf-adopted-undo-grouping.h>
#include <libinfinity/inf-marshal.h>
#include <libinfinity/inf-signals.h>

/**
 * SECTION:inf-adopted-undo-grouping
 * @title: InfAdoptedUndoGrouping
 * @short_description: Grouping of requests to be undone simultaneously
 * @include: libinfinity/adopted/inf-adopted-undo-grouping.h
 * @see_also: #InfAdoptedAlgorithm
 * @stability: Unstable
 *
 * #InfAdoptedUndoGrouping groups related requests together so that they can
 * be undone at the same time. For example, Undo in a text editor is normally
 * expected to operate on written words, not characters. Therefore, multiple
 * requests need to be undone at once.
 *
 * The undo grouping helps with this. Everytime it needs to decide whether two
 * requests should be grouped it emits
 * #InfAdoptedUndoGrouping::group-requests. If the signal handler returns
 * %TRUE then the two requests will be undone at the same time, otherwise not.
 *
 * It is also possible to explicitely group a bunch of requests that would
 * not be grouped otherwise, by calling inf_adopted_undo_grouping_start_group()
 * and inf_adopted_undo_grouping_end_group() before and after issuing the
 * requests, respectively.
 *
 * The default signal handler always returns %FALSE. However, this behaviour
 * can be changed in derived classes.
 */

typedef struct _InfAdoptedUndoGroupingItem InfAdoptedUndoGroupingItem;
struct _InfAdoptedUndoGroupingItem {
  InfAdoptedRequest* request;
  gboolean in_group;
};

typedef enum __InfAdoptedUndoGroupingFlags {
  /* allow grouping with items before explicit group */
  INF_ADOPTED_UNDO_GROUPING_ALLOW_WITH_PREV   = 1 << 0,
  /* allow grouping with items after explicit group */
  INF_ADOPTED_UNDO_GROUPING_ALLOW_WITH_NEXT   = 1 << 1,
  /* whether the next item is the first item inside an explicit group */
  INF_ADOPTED_UNDO_GROUPING_FIRST_IN_GROUP    = 1 << 2,
  /* whether the next item is the first item after an explicit group */
  INF_ADOPTED_UNDO_GROUPING_FIRST_AFTER_GROUP = 1 << 3
} InfAdoptedUndoGroupingFlags;

typedef struct _InfAdoptedUndoGroupingPrivate InfAdoptedUndoGroupingPrivate;
struct _InfAdoptedUndoGroupingPrivate {
  InfAdoptedAlgorithm* algorithm;
  InfAdoptedUser* user;

  InfAdoptedUndoGroupingItem* items;
  guint n_items;
  guint n_alloc;
  guint first_item;
  guint item_pos; /* relative to first_item */

  guint group_ref;
  guint group_flags;
};

enum {
  PROP_0,

  /* construct only */
  PROP_ALGORITHM,
  PROP_USER
};

enum {
  GROUP_REQUESTS,

  LAST_SIGNAL
};

#define INF_ADOPTED_UNDO_GROUPING_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE((obj), INF_ADOPTED_TYPE_UNDO_GROUPING, InfAdoptedUndoGroupingPrivate))

static GObjectClass* parent_class;
static guint undo_grouping_signals[LAST_SIGNAL];

static void
inf_adopted_undo_grouping_add_request(InfAdoptedUndoGrouping* grouping,
                                      InfAdoptedRequest* request)
{
  InfAdoptedUndoGroupingPrivate* priv;
  guint max;
  InfAdoptedUndoGroupingItem* item;
  InfAdoptedUndoGroupingItem* prev_item;

  InfAdoptedUndoGroupingFlags flags;
  gboolean first_after_group;
  gboolean first_in_group;
  gboolean allow_with_next;
  gboolean allow_with_prev;

  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);

  switch(inf_adopted_request_get_request_type(request))
  {
  case INF_ADOPTED_REQUEST_DO:
    if(priv->first_item + priv->item_pos == priv->n_alloc)
    {
      /* The maximum number of requests that we ever need to hold is half of
       * the algorithm's max total log size, since undoing one of the requests
       * in the log takes another request. We add +1 because we add the new
       * request before removing the old one. */
      g_object_get(
        G_OBJECT(priv->algorithm),
        "max-total-log-size", &max,
        NULL
      );

      if(max != G_MAXUINT)
      {
        max = (max/2) + 1;

        /* Don't start to wrap around as long as we have not reached the max
         * buffer size. */
        if(priv->n_alloc < max)
        {
          priv->n_alloc = MIN(priv->n_alloc * 2, max);
          priv->n_alloc = MAX(priv->n_alloc, MIN(16, max));

          priv->items = g_realloc(
            priv->items,
            priv->n_alloc * sizeof(InfAdoptedUndoGroupingItem)
          );
        }
      }
      else
      {
        priv->n_alloc = MAX(priv->n_alloc * 2, 16);

        priv->items = g_realloc(
          priv->items,
          priv->n_alloc * sizeof(InfAdoptedUndoGroupingItem)
        );
      }
    }

    /* Cut redo possibilities */
    priv->n_items = priv->item_pos;
    g_assert(priv->n_items < priv->n_alloc);
    item = &priv->items[(priv->first_item + priv->item_pos) % priv->n_alloc];

    /* We don't ref request, it is kept alive by the request log anyway */
    item->request = request;

    if(priv->item_pos > 0)
    {
      flags = priv->group_flags;

      first_after_group =
        (flags & INF_ADOPTED_UNDO_GROUPING_FIRST_AFTER_GROUP) != 0;
      first_in_group =
        (flags & INF_ADOPTED_UNDO_GROUPING_FIRST_IN_GROUP) != 0;
      allow_with_next =
        (flags & INF_ADOPTED_UNDO_GROUPING_ALLOW_WITH_NEXT) != 0;
      allow_with_prev =
        (flags & INF_ADOPTED_UNDO_GROUPING_ALLOW_WITH_PREV) != 0;

      if(first_after_group && !allow_with_next)
      {
        item->in_group = FALSE;
      }
      else if(priv->group_ref > 0 && first_in_group && !allow_with_prev)
      {
        item->in_group = FALSE;
      }
      else if(priv->group_ref > 0 && !first_in_group)
      {
        item->in_group = TRUE;
      }
      else
      {
        prev_item = &priv->items[
          (priv->first_item + priv->item_pos - 1) % priv->n_alloc
        ];

        g_signal_emit(
          G_OBJECT(grouping),
          undo_grouping_signals[GROUP_REQUESTS],
          0,
          prev_item->request,
          request,
          &item->in_group
        );
      }
    }
    else
    {
      /* No previous request, so start group */
      item->in_group = FALSE;
    }

    priv->group_flags &= ~(INF_ADOPTED_UNDO_GROUPING_FIRST_IN_GROUP |
                           INF_ADOPTED_UNDO_GROUPING_FIRST_AFTER_GROUP);

    ++priv->n_items;
    ++priv->item_pos;
    break;
  case INF_ADOPTED_REQUEST_UNDO:
    g_assert(priv->item_pos > 0);
    --priv->item_pos;
    break;
  case INF_ADOPTED_REQUEST_REDO:
    g_assert(priv->item_pos < priv->n_items);
    ++priv->item_pos;
    break;
  }
}

/* Remove requests that can no longer be undone from buffer */
static void
inf_adopted_undo_grouping_cleanup(InfAdoptedUndoGrouping* grouping)
{
  InfAdoptedUndoGroupingPrivate* priv;
  InfAdoptedUndoGroupingItem* item;
  guint max_total_log_size;
  guint vdiff;

  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);
  g_assert(priv->user != NULL);

  g_object_get(
    priv->algorithm,
    "max-total-log-size", &max_total_log_size,
    NULL
  );

  if(max_total_log_size != G_MAXUINT)
  {
    while(priv->n_items > 0)
    {
      item = &priv->items[priv->first_item];

      vdiff = inf_adopted_state_vector_vdiff(
        inf_adopted_request_get_vector(item->request),
        inf_adopted_user_get_vector(priv->user)
      );

      if(vdiff + priv->item_pos > max_total_log_size)
      {
        /* Request is too old to be undone, remove from buffer */
        if(priv->item_pos == 0)
        {
          /* Remove all items since we cannot redo the following anymore at
           * this point since the first one to redo is too old. */
          priv->first_item = 0;
          priv->n_items = 0;
          break;
        }
        else
        {
          /* Remove the request being too old */
          priv->first_item = (priv->first_item + 1) % priv->n_alloc;
          --priv->n_items;
          --priv->item_pos;

          /* Reuse buffer if we drop to zero */
          if(priv->n_items == 0)
            priv->first_item = 0;
          else
            priv->items[priv->first_item].in_group = FALSE;
        }
      }
      else
      {
        /* All OK */
        break;
      }
    }
  }
}

static void
inf_adopted_undo_grouping_add_request_cb(InfAdoptedRequestLog* log,
                                         InfAdoptedRequest* request,
                                         gpointer user_data)
{
  InfAdoptedUndoGrouping* grouping;
  grouping = INF_ADOPTED_UNDO_GROUPING(user_data);

  inf_adopted_undo_grouping_add_request(grouping, request);
}

static void
inf_adopted_undo_grouping_execute_request_cb(InfAdoptedAlgorithm* algorithm,
                                             InfAdoptedUser* user,
                                             InfAdoptedRequest* request,
                                             gboolean apply,
                                             gpointer user_data)
{
  InfAdoptedUndoGrouping* grouping;
  InfAdoptedUndoGroupingPrivate* priv;

  /* Note that this signal handler is called _after_ the request has been
   * executed.  If the execution causes requests in the request log to be
   * removed, then this will still happen after the signal emission though,
   * so all requests in our buffers are still valid at this point. */

  grouping = INF_ADOPTED_UNDO_GROUPING(user_data);
  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);

  /* If the request does not affect the buffer then it did not increase the
   * state vector, in which case we don't need to check again here. */
  if(priv->user != NULL && inf_adopted_request_affects_buffer(request))
    inf_adopted_undo_grouping_cleanup(grouping);
}

static void
inf_adopted_undo_grouping_init_user(InfAdoptedUndoGrouping* grouping)
{
  InfAdoptedUndoGroupingPrivate* priv;
  InfAdoptedRequestLog* log;
  InfAdoptedRequest* request;
  guint max_total_log_size;
  guint end;
  guint i;

  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);

  g_assert(priv->user != NULL);

  g_signal_connect(
    G_OBJECT(inf_adopted_user_get_request_log(priv->user)),
    "add-request",
    G_CALLBACK(inf_adopted_undo_grouping_add_request_cb),
    grouping
  );

  g_object_get(
    priv->algorithm,
    "max-total-log-size", &max_total_log_size,
    NULL
  );

  /* Add initial requests from request log */
  log = inf_adopted_user_get_request_log(priv->user);
  end = inf_adopted_request_log_get_end(log);

  for(i = inf_adopted_request_log_get_begin(log); i < end; ++i)
  {
    request = inf_adopted_request_log_get_request(log, i);
    inf_adopted_undo_grouping_add_request(grouping, request);

    /* TODO: Instead of cleaning up requests that we have added just before,
     * we may find out which ones will not end up in the buffer anyway because
     * they cannot be undone anymore. This would require
     * inf_adopted_algorithm_can_undo_redo() to work for requests that are
     * anywhere in the log and to be public. */
    inf_adopted_undo_grouping_cleanup(grouping);
  }
}

static void
inf_adopted_undo_grouping_deinit_user(InfAdoptedUndoGrouping* grouping)
{
  InfAdoptedUndoGroupingPrivate* priv;
  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);

  g_assert(priv->user != NULL);

  inf_signal_handlers_disconnect_by_func(
    G_OBJECT(inf_adopted_user_get_request_log(priv->user)),
    G_CALLBACK(inf_adopted_undo_grouping_add_request_cb),
    grouping
  );

  g_object_unref(priv->user);
  priv->user = NULL;

  g_free(priv->items);
  priv->items = NULL;
  priv->n_items = 0;
  priv->n_alloc = 0;
  priv->first_item = 0;
  priv->item_pos = 0;

  g_object_notify(G_OBJECT(grouping), "user");
}

static void
inf_adopted_undo_grouping_init(GTypeInstance* instance,
                               gpointer g_class)
{
  InfAdoptedUndoGrouping* grouping;
  InfAdoptedUndoGroupingPrivate* priv;

  grouping = INF_ADOPTED_UNDO_GROUPING(instance);
  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);

  priv->algorithm = NULL;
  priv->user = NULL;

  priv->items = NULL;
  priv->n_items = 0;
  priv->n_alloc = 0;
  priv->first_item = 0;
  priv->item_pos = 0;

  priv->group_ref = 0;
  priv->group_flags = 0;
}

static void
inf_adopted_undo_grouping_dispose(GObject* object)
{
  InfAdoptedUndoGrouping* grouping;
  InfAdoptedUndoGroupingPrivate* priv;

  grouping = INF_ADOPTED_UNDO_GROUPING(object);
  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);

  inf_adopted_undo_grouping_set_algorithm(grouping, NULL, NULL);

  G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void
inf_adopted_undo_grouping_finalize(GObject* object)
{
  InfAdoptedUndoGrouping* grouping;
  InfAdoptedUndoGroupingPrivate* priv;

  grouping = INF_ADOPTED_UNDO_GROUPING(object);
  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
inf_adopted_undo_grouping_set_property(GObject* object,
                                       guint prop_id,
                                       const GValue* value,
                                       GParamSpec* pspec)
{
  InfAdoptedUndoGrouping* grouping;
  InfAdoptedUndoGroupingPrivate* priv;

  grouping = INF_ADOPTED_UNDO_GROUPING(object);
  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);

  switch(prop_id)
  {
  case PROP_ALGORITHM:
    inf_adopted_undo_grouping_set_algorithm(
      grouping,
      INF_ADOPTED_ALGORITHM(g_value_get_object(value)),
      NULL
    );

    break;
  case PROP_USER:
    inf_adopted_undo_grouping_set_algorithm(
      grouping,
      priv->algorithm,
      INF_ADOPTED_USER(g_value_get_object(value))
    );

    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
inf_adopted_undo_grouping_get_property(GObject* object,
                                       guint prop_id,
                                       GValue* value,
                                       GParamSpec* pspec)
{
  InfAdoptedUndoGrouping* grouping;
  InfAdoptedUndoGroupingPrivate* priv;

  grouping = INF_ADOPTED_UNDO_GROUPING(object);
  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);

  switch(prop_id)
  {
  case PROP_ALGORITHM:
    g_value_set_object(value, priv->algorithm);
    break;
  case PROP_USER:
    g_value_set_object(value, priv->user);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static gboolean
inf_adopted_undo_grouping_group_requests(InfAdoptedUndoGrouping* grouping,
                                         InfAdoptedRequest* first,
                                         InfAdoptedRequest* second)
{
  return FALSE;
}

static void
inf_adopted_undo_grouping_class_init(gpointer g_class,
                                     gpointer class_data)
{
  GObjectClass* object_class;
  InfAdoptedUndoGroupingClass* undo_grouping_class;

  object_class = G_OBJECT_CLASS(g_class);
  undo_grouping_class = INF_ADOPTED_UNDO_GROUPING_CLASS(g_class);

  parent_class = G_OBJECT_CLASS(g_type_class_peek_parent(g_class));
  g_type_class_add_private(g_class, sizeof(InfAdoptedUndoGroupingPrivate));

  object_class->dispose = inf_adopted_undo_grouping_dispose;
  object_class->finalize = inf_adopted_undo_grouping_finalize;
  object_class->set_property = inf_adopted_undo_grouping_set_property;
  object_class->get_property = inf_adopted_undo_grouping_get_property;
  undo_grouping_class->group_requests =
    inf_adopted_undo_grouping_group_requests;

  g_object_class_install_property(
    object_class,
    PROP_ALGORITHM,
    g_param_spec_object(
      "algorithm",
      "Algorithm",
      "The algorithm for which to group requests",
      INF_ADOPTED_TYPE_ALGORITHM,
      G_PARAM_READWRITE
    )
  );

  g_object_class_install_property(
    object_class,
    PROP_USER,
    g_param_spec_object(
      "user",
      "User",
      "The user for which to group requests",
      INF_ADOPTED_TYPE_USER,
      G_PARAM_READWRITE
    )
  );

  /**
   * InfAdoptedUndoGrouping::group-requests:
   * @grouping: The #InfAdoptedUndoGrouping which is about to group a request.
   * @first: The previous request.
   * @second: The current request.
   *
   * This signal is emitted whenever the #InfAdoptedUndoGrouping needs to
   * decide whether to put two requests into the same undo group or not.
   * A signal handler should return %TRUE if they belong into the same group
   * or %FALSE otherwise. Note however that the two requests may not
   * immediately follow each other because other users may have issued
   * requests inbetween. Check the vector times of the requests to find out,
   * using inf_adopted_request_get_vector().
   */
  undo_grouping_signals[GROUP_REQUESTS] = g_signal_new(
    "group-requests",
    G_OBJECT_CLASS_TYPE(object_class),
    G_SIGNAL_RUN_LAST,
    G_STRUCT_OFFSET(InfAdoptedAlgorithmClass, can_undo_changed),
    g_signal_accumulator_true_handled, NULL,
    inf_marshal_BOOLEAN__OBJECT_OBJECT,
    G_TYPE_BOOLEAN,
    2,
    INF_ADOPTED_TYPE_REQUEST,
    INF_ADOPTED_TYPE_REQUEST
  );
}

GType
inf_adopted_undo_grouping_get_type(void)
{
  static GType undo_grouping_type = 0;

  if(!undo_grouping_type)
  {
    static const GTypeInfo undo_grouping_type_info = {
      sizeof(InfAdoptedUndoGroupingClass),  /* class_size */
      NULL,                                /* base_init */
      NULL,                                /* base_finalize */
      inf_adopted_undo_grouping_class_init, /* class_init */
      NULL,                                /* class_finalize */
      NULL,                                /* class_data */
      sizeof(InfAdoptedUndoGrouping),       /* instance_size */
      0,                                   /* n_preallocs */
      inf_adopted_undo_grouping_init,       /* instance_init */
      NULL                                 /* value_table */
    };

    undo_grouping_type = g_type_register_static(
      G_TYPE_OBJECT,
      "InfAdoptedUndoGrouping",
      &undo_grouping_type_info,
      0
    );
  }

  return undo_grouping_type;
}

/**
 * inf_adopted_undo_grouping_new:
 *
 * Creates a new #InfAdoptedUndoGrouping. To start grouping requests, set a
 * user whose requests to group via inf_adopted_undo_grouping_set_algorithm().
 * Before doing so you might want to connect to
 * #InfAdoptedUndoGrouping::group-requests, so the user's initial requests can
 * be grouped correctly.
 *
 * Returns: A new #InfAdoptedUndoGrouping, to be freed via g_object_unref().
 */
InfAdoptedUndoGrouping*
inf_adopted_undo_grouping_new(void)
{
  GObject* object;
  object = g_object_new(INF_ADOPTED_TYPE_UNDO_GROUPING, NULL);
  return INF_ADOPTED_UNDO_GROUPING(object);
}

/**
 * inf_adopted_undo_grouping_get_algorithm:
 * @grouping: A #InfAdoptedUndoGrouping.
 *
 * Returns the #InfAdoptedAlgorithm for @grouping.
 *
 * Returns: @grouping's algorithm.
 */
InfAdoptedAlgorithm*
inf_adopted_undo_grouping_get_algorithm(InfAdoptedUndoGrouping* grouping)
{
  g_return_val_if_fail(INF_ADOPTED_IS_UNDO_GROUPING(grouping), NULL);
  return INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping)->algorithm;
}

/**
 * inf_adopted_undo_grouping_set_algorithm:
 * @grouping: A #InfAdoptedUndoGrouping.
 * @algorithm: The #InfAdoptedAlgorithm for the document to group requests,
 * or %NULL.
 * @user: The user for which to group requests, or %NULL. Ignored if
 * @algorithm is %NULL.
 *
 * Sets the algorithm and user to group requests for. This function will group
 * all requests in user's request log, and also each new request that is
 * added to it's log. Requests that cannot be undone anymore (because
 * they are too old), will be correctly taken care off.
 */
void
inf_adopted_undo_grouping_set_algorithm(InfAdoptedUndoGrouping* grouping,
                                       InfAdoptedAlgorithm* algorithm,
                                       InfAdoptedUser* user)
{
  InfAdoptedUndoGroupingPrivate* priv;

  g_return_if_fail(INF_ADOPTED_IS_UNDO_GROUPING(grouping));
  g_return_if_fail(algorithm == NULL || INF_ADOPTED_IS_ALGORITHM(algorithm));
  g_return_if_fail(user == NULL || INF_ADOPTED_IS_USER(user));

  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);
  g_object_freeze_notify(G_OBJECT(grouping));

  if(priv->algorithm != algorithm)
  {
    if(priv->algorithm != NULL)
    {
      inf_signal_handlers_disconnect_by_func(
        G_OBJECT(priv->algorithm),
        G_CALLBACK(inf_adopted_undo_grouping_execute_request_cb),
        grouping
      );

      /* The user belonged to the old algorithm */
      if(priv->user != NULL)
        inf_adopted_undo_grouping_deinit_user(grouping);

      g_object_unref(priv->algorithm);
      priv->algorithm = NULL;
    }

    priv->algorithm = algorithm;

    if(algorithm != NULL)
    {
      g_object_ref(algorithm);

      g_signal_connect_after(
        G_OBJECT(priv->algorithm),
        "execute-request",
        G_CALLBACK(inf_adopted_undo_grouping_execute_request_cb),
        grouping
      );
    }

    g_object_notify(G_OBJECT(grouping), "algorithm");
  }

  if(priv->user != user)
  {
    if(priv->user != NULL)
        inf_adopted_undo_grouping_deinit_user(grouping);

    priv->user = user;

    if(user != NULL)
    {
      g_object_ref(user);

      inf_adopted_undo_grouping_init_user(grouping);
    }

    g_object_notify(G_OBJECT(grouping), "user");
  }

  g_object_thaw_notify(G_OBJECT(grouping));
}

/**
 * inf_adopted_undo_grouping_start_group:
 * @grouping: A #InfAdoptedUndoGrouping.
 * @allow_group_with_prev: Whether the new group can be part of the previous
 * group if #InfAdoptedUndoGrouping::group-requests allows.
 *
 * Makes all requests issued after this call belong into the same group,
 * i.e. they will be undone at once. This can make sense for example when the
 * user copy+pastes something into the document which causes multiple requests
 * to be generated. A call to inf_adopted_undo_grouping_end_group() restores
 * the normal behavior.
 */
void
inf_adopted_undo_grouping_start_group(InfAdoptedUndoGrouping* grouping,
                                      gboolean allow_group_with_prev)
{
  InfAdoptedUndoGroupingPrivate* priv;

  g_return_if_fail(INF_ADOPTED_IS_UNDO_GROUPING(grouping));

  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);
  g_return_if_fail(priv->algorithm != NULL);
  g_return_if_fail(priv->user != NULL);

  if(priv->group_ref++ == 0)
  {
    priv->group_flags = INF_ADOPTED_UNDO_GROUPING_FIRST_IN_GROUP;
    if(allow_group_with_prev)
      priv->group_flags |= INF_ADOPTED_UNDO_GROUPING_ALLOW_WITH_PREV;
  }
}

/**
 * inf_adopted_undo_grouping_end_group.
 * @grouping: A #InfAdoptedUndoGrouping.
 * @allow_group_with_next: Whether subsequent requests are allow to be part of
 * this group if #InfAdoptedUndoGrouping::group-requests allows.
 *
 * When inf_adopted_undo_grouping_start_group() was called before, then this
 * function restores the normal behaviour of grouping requests.
 */
void
inf_adopted_undo_grouping_end_group(InfAdoptedUndoGrouping* grouping,
                                    gboolean allow_group_with_next)
{
  InfAdoptedUndoGroupingPrivate* priv;

  g_return_if_fail(INF_ADOPTED_IS_UNDO_GROUPING(grouping));

  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);
  g_return_if_fail(priv->algorithm != NULL);
  g_return_if_fail(priv->user != NULL);

  g_return_if_fail(priv->group_ref > 0);
  if(--priv->group_ref == 0)
  {
    priv->group_flags = INF_ADOPTED_UNDO_GROUPING_FIRST_AFTER_GROUP;
    if(allow_group_with_next)
      priv->group_flags |= INF_ADOPTED_UNDO_GROUPING_ALLOW_WITH_NEXT;
  }
}

/**
 * inf_adopted_undo_grouping_get_undo_size:
 * @grouping: A #InfAdoptedUndoGrouping.
 *
 * Returns the number of requests to undo so that a whole group is being
 * undone.
 *
 * Returns: The number of requests in the current undo group.
 */
guint
inf_adopted_undo_grouping_get_undo_size(InfAdoptedUndoGrouping* grouping)
{
  InfAdoptedUndoGroupingPrivate* priv;
  guint pos;

  g_return_val_if_fail(INF_ADOPTED_IS_UNDO_GROUPING(grouping), 0);

  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);
  if(priv->item_pos == 0) return 0;

  pos = priv->item_pos;
  do
  {
    g_assert(pos > 0);
    --pos;
  } while(priv->items[(priv->first_item + pos) % priv->n_alloc].in_group);

  return priv->item_pos - pos;
}

/**
 * inf_adopted_undo_grouping_get_redo_size:
 * @grouping: A #InfAdoptedUndoGrouping.
 *
 * Returns the number of requests to redo so that a whole group is being
 * redone.
 *
 * Returns: The number of requests in the current redo group.
 */
guint
inf_adopted_undo_grouping_get_redo_size(InfAdoptedUndoGrouping* grouping)
{
  InfAdoptedUndoGroupingPrivate* priv;
  guint pos;

  g_return_val_if_fail(INF_ADOPTED_IS_UNDO_GROUPING(grouping), 0);

  priv = INF_ADOPTED_UNDO_GROUPING_PRIVATE(grouping);
  if(priv->item_pos == priv->n_items) return 0;

  pos = priv->item_pos;
  do
  {
    g_assert(pos < priv->n_items);
    ++pos;
  } while(pos < priv->n_items &&
          priv->items[(priv->first_item + pos) % priv->n_alloc].in_group);

  return pos - priv->item_pos;
}

/* vim:set et sw=2 ts=2: */
