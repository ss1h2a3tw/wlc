#include "wlc.h"
#include "context.h"
#include "egl.h"

#include <stdlib.h>
#include <assert.h>

void
wlc_context_terminate(struct wlc_context *context)
{
   assert(context);

   if (context->terminate)
      context->terminate();

   free(context);
}

struct wlc_context*
wlc_context_init(struct wlc_compositor *compositor, struct wlc_backend *backend)
{
   struct wlc_context *context;

   if (!(context = calloc(1, sizeof(struct wlc_context))))
      goto out_of_memory;

   bool (*init[])(struct wlc_compositor*, struct wlc_backend*, struct wlc_context*) = {
      wlc_egl_init,
      NULL
   };

   for (int i = 0; init[i]; ++i)
      if (init[i](compositor, backend, context))
         return context;

   wlc_log(WLC_LOG_WARN, "Could not initialize any context");
   free(context);
   return NULL;

out_of_memory:
   wlc_log(WLC_LOG_WARN, "Out of memory");
   return NULL;
}
