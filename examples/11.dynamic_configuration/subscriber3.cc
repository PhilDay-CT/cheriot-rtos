// Copyright Microsoft and CHERIoT Contributors.
// SPDX-License-Identifier: MIT

// Contributed by Configured Things Ltd

#include <compartment.h>
#include <debug.hh>
#include <fail-simulator-on-error.h>
#include <thread.h>

// Define a sealed capability that gives this compartment
// read access to configuration data "config1" and "config2"
#include "config_broker.h"
#define CONFIG1 "config1"
DEFINE_READ_CONFIG_CAPABILITY(CONFIG1)
#define CONFIG2 "config2"
DEFINE_READ_CONFIG_CAPABILITY(CONFIG2)

// Expose debugging features unconditionally for this compartment.
using Debug = ConditionalDebug<true, "Subscriber #3">;

#include "data.h"
#include "sleep.h"
#include "validator.h"

Data *config1 = nullptr;
Data *config2 = nullptr;

//
// Callback to handle config updates.  We
// have separate methods for each config item
//
void __cheri_callback update_config_1(const char *id, void *data)
{
	// Validate the data in a sandpit compartment
	bool valid = false;
	validate(data, &valid);
	if (!valid)
	{
		Debug::log(
		  "thread {} Validation failed for config1 {}", thread_id_get(), data);
		return;
	}

	// If we've been given a different object from
	// last time then release our claim on the old
	// object and add a claim to the object so we
	// still have  access to it outside hee scope
	// of this call
	if ((config1 != data))
	{
		if (config1 != nullptr)
		{
			heap_free(MALLOC_CAPABILITY, config1);
		}
		heap_claim(MALLOC_CAPABILITY, data);
	}

	config1 = static_cast<Data *>(data);
	print_config("Update", "config1", config1);
}

void __cheri_callback update_config_2(const char *id, void *data)
{
	// Validate the data in a sandpit compartment
	bool valid = false;
	validate(data, &valid);
	if (!valid)
	{
		Debug::log(
		  "thread {} Validation failed for config2 {}", thread_id_get(), data);
		return;
	}

	// If we've been given a different object from
	// last time then release our claim on the old
	// object and add a claim to the object so we
	// still have  access to it outside hee scope
	// of this call
	if ((config2 != data))
	{
		if (config2 != nullptr)
		{
			heap_free(MALLOC_CAPABILITY, config2);
		}
		heap_claim(MALLOC_CAPABILITY, data);
	}

	config2 = static_cast<Data *>(data);
	print_config("Update", "config2", config2);
}

//
// Thread entry point.
//
void __cheri_compartment("subscriber3") init()
{
	// Register to get config updates
	Debug::log("thread {} Register for config updates", thread_id_get());
	on_config(READ_CONFIG_CAPABILITY(CONFIG1), update_config_1);
	on_config(READ_CONFIG_CAPABILITY(CONFIG2), update_config_2);

	// Loop printing our config value occasionally
	while (true)
	{
		sleep(4700);
		print_config("Timer ", "config1", config1);
		print_config("Timer ", "config2", config2);

		// Check we're not leaking data;
		// Debug::log("heap quota available: {}",
		//   heap_quota_remaining(MALLOC_CAPABILITY));
	}
}
