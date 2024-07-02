// Copyright Microsoft and CHERIoT Contributors.
// SPDX-License-Identifier: MIT

// Contributed by Configured Things Ltd

#include <compartment.h>
#include <cstdlib>
#include <debug.hh>
#include <fail-simulator-on-error.h>
#include <thread.h>
#include <tick_macros.h>

// Expose debugging features unconditionally for this compartment.
using Debug = ConditionalDebug<true, "Config Source">;

#include "data.h"
#include "sleep.h"

// Compartment can set config values config1 and config2
#include "config_broker.h"
#define CONFIG1 "config1"
DEFINE_WRITE_CONFIG_CAPABILITY(CONFIG1, sizeof(Data))
#define CONFIG2 "config2"
DEFINE_WRITE_CONFIG_CAPABILITY(CONFIG2, sizeof(Data))

// Helper to set some dummy config
void gen_config(SObj sealedCap, int count, const char *token)
{
	Data *d  = static_cast<Data *>(malloc(sizeof(Data)));
	d->count = count;
	strlcpy(d->token, token, sizeof(token));

	Debug::log("thread {} Set {}", thread_id_get(), sealedCap);
	if (set_config(sealedCap, static_cast<void *>(d), sizeof(Data)) < 0)
	{
		Debug::log("Failed to set value for {}", sealedCap);
	};

	// Change the value after we've passed it to the broker to
	// show it doesn't have to trust us to not change it.
	const char *meep  = "MeepMeep!";
	strlcpy(d->token, meep, sizeof(meep));
	free(d);
};

void gen_bad_config(SObj sealedCap)
{
	Debug::log("thread {} Sending bad data for {}", thread_id_get(), sealedCap);
	void *d = malloc(4);
	set_config(sealedCap, d, 4);
	free(d);
};

// Hack function to generate some configuration  data
void __cheri_compartment("config_source") init()
{
	gen_config(WRITE_CONFIG_CAPABILITY(CONFIG1), 0, "Wile-E");
	gen_config(WRITE_CONFIG_CAPABILITY(CONFIG2), 0, "Coyote");

	int loop = 1;
	while (true)
	{
		sleep(1500);
		gen_config(WRITE_CONFIG_CAPABILITY(CONFIG1), loop++, "Wile-E");

		sleep(1500);
		gen_config(WRITE_CONFIG_CAPABILITY(CONFIG2), loop++, "Coyote");

		// Check we're not leaking data;
		// Debug::log("heap quota available: {}",
		//   heap_quota_remaining(MALLOC_CAPABILITY));

		// Give the compartments a chance to print their
		// config values from timers
		sleep(3000);
	}
};

void __cheri_compartment("config_source") bad_dog()
{
	while (true)
	{
		sleep(12000);
		gen_bad_config(WRITE_CONFIG_CAPABILITY(CONFIG1));
	}
};
