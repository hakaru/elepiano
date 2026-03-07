/* setBfree stubs — src/main.c の代替（JACK 依存を回避）
 * mainConfig / mainDoc / SampleRateD のみ提供
 */
#include "cfgParser.h"
#include "main.h"
#include <strings.h>

double SampleRateD = 48000.0;

int
mainConfig (ConfigContext* cfg)
{
	if (strcasecmp (cfg->name, "midi.driver") == 0)
		return 1;
	if (strcasecmp (cfg->name, "midi.port") == 0)
		return 1;
	if (strcasecmp (cfg->name, "jack.connect") == 0)
		return 1;
	return 0;
}

const ConfigDoc*
mainDoc ()
{
	return NULL;
}
