#ifndef IRSSI_E2E_FORMATS_H
#define IRSSI_E2E_FORMATS_H

#include <irssi/src/fe-common/core/formats.h>

/* must be in sync with e2e_formats[] */
enum {
	TXT_E2E_MODULE_NAME,

	TXT_E2E_MESSAGE,
	TXT_E2E_DEBUG,
	TXT_E2E_PENDING,
	TXT_E2E_SB_OFF,
	TXT_E2E_SB_ON,
	TXT_E2E_SB_WAITING,
	TXT_E2E_SB_BLOCKED,
	TXT_E2E_SB_ERROR,
	TXT_E2E_SB_PENDING,
	TXT_E2E_PENDING_QUERY
};

extern FORMAT_REC e2e_formats[];

#endif
