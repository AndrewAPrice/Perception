#pragma once
#include "types.h"

/* this is a helper for synchronous IO

	struct CallbackSyncTag tag;
	tag.thread = running_thread;
	tag.response = 0;

	some_sync_call(.., callback_sync_handler, &tag);
		while(!tag.response) sleep_if_not_set(&tag.response);

	then read tag.status
*/
struct CallbackSyncTag {
	size_t response;
	size_t status;
	struct Thread *thread;
};

struct CallbackSyncParamTag {
	size_t response;
	size_t status;
	size_t result;
	struct Thread *thread;
};

extern void callback_sync_handler(size_t status, void *tag);

extern void callback_sync_param_handler(size_t status, size_t result, void *tag);
