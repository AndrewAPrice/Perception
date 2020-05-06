#include "callback.h"
#include "scheduler.h"

void callback_sync_handler(size_t status, void *tag) {
	struct CallbackSyncTag *_tag = (struct CallbackSyncTag *)tag;
	_tag->status = status;
	
	/* wake this thread */
	_tag->response = 1;
	schedule_thread(_tag->thread);
}

void callback_sync_param_handler(size_t status, size_t result, void *tag) {
	struct CallbackSyncParamTag *_tag = (struct CallbackSyncParamTag *)tag;
	_tag->status = status;
	_tag->result = result;
	
	/* wake this thread */
	_tag->response = 1;
	schedule_thread(_tag->thread);
}
