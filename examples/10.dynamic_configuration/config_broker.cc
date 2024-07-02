// Copyright Microsoft and CHERIoT Contributors.
// SPDX-License-Identifier: MIT

// Contributed by Configured Things Ltd

#include "cdefs.h"
#include "cheri.hh"
#include <compartment.h>
#include <cstdlib>
#include <debug.hh>
#include <fail-simulator-on-error.h>
#include <futex.h>
#include <thread.h>

#include <string.h>
#include <vector>

#include "config_broker.h"

// Import some useful things from the CHERI namespace.
using namespace CHERI;

// Expose debugging features unconditionally for this compartment.
using Debug = ConditionalDebug<DEBUG_CONFIG_BROKER, "Config Broker">;

//
// Data type of config data for a compartment
//
struct CbInfo
{
	uint16_t id;
	__cheri_callback void (*cb)(const char *, void *);
};

struct Config
{
	char                        *name;
	bool                         updated;
	std::vector<struct CbInfo *> cbList;
	void                        *data;
};

//
// count of un-sent updates; used as a futex
//
static uint32_t pending = 0;

//
// Set of config data items.
//
std::vector<struct Config *> configData;

//
// unseal a config capability.
//
ConfigToken *config_capability_unseal(SObj sealedCap)
{
	auto key = STATIC_SEALING_TYPE(ConfigKey);

	ConfigToken *token =
	  token_unseal<ConfigToken>(key, Sealed<ConfigToken>{sealedCap});

	if (token == nullptr)
	{
		Debug::log("invalid config capability {}", sealedCap);
		return nullptr;
	}

	Debug::log("Unsealed id: {} kind: {} size:{} item: {}",
	           token->id,
	           token->kind,
	           token->maxSize,
	           token->configId);

	if (token->id == 0)
	{
		// Assign an ID so we can track the callbacks added
		// from this capability
		static uint16_t nextId = 1;
		token->id              = nextId++;
	}

	return token;
}

//
// Find a Config by name.  If it doesn't already exist
// create one.
//
Config *find_or_create_config(const char *name)
{
	for (auto &c : configData)
	{
		if (strcmp(c->name, name) == 0)
		{
			return c;
		}
	}

	// Allocate a Config object
	Config *c = static_cast<Config *>(malloc(sizeof(Config)));

	// Save the name
	c->name = static_cast<char *>(malloc(strlen(name)));
	strncpy(c->name, name, strlen(name));

	c->updated = false;
	c->data    = nullptr;

	// Add it to the vector
	configData.push_back(c);

	return c;
};

//
// Add a callback to the list for a Config.  Use the
// id to prevent adding more than one callback for
// each client
//
void add_callback(Config               *c,
                  uint16_t              id,
                  __cheri_callback void cb(const char *, void *))
{
	for (auto &cbInfo : c->cbList)
	{
		if (cbInfo->id == id)
		{
			cbInfo->cb = cb;
			return;
		}
	}

	CbInfo *cbInfo = static_cast<CbInfo *>(malloc(sizeof(CbInfo)));
	cbInfo->id     = id;
	cbInfo->cb     = cb;
	c->cbList.push_back(cbInfo);
}

//
// Set a new value for the configuration item described by
// the capability.
//
int __cheri_compartment("config_broker")
  set_config(SObj sealedCap, void *data, size_t size)
{
	ConfigToken *token = config_capability_unseal(sealedCap);
	if (token == nullptr)
	{
		Debug::log("Invalid capability: {}", sealedCap);
		return -1;
	}

	// Check we have a WriteToken
	if (token->kind != WriteToken)
	{
		Debug::log(
		  "Not a write capability for {}: {}", token->configId, sealedCap);
		return -1;
	}

	// Check the size and data are consistent with the token
	// and each other.
	if (size > token->maxSize)
	{
		Debug::log("invalid size {} for capability: {}", size, sealedCap);
		return -1;
	}

	if (size > static_cast<size_t>(Capability{data}.bounds()))
	{
		Debug::log("size {} > data.bounds() {}", size, data);
		return -1;
	}

	// Find or create a config structure
	Config *c = find_or_create_config(token->configId);

	// Allocate heap space for the new value
	void *newData = malloc(size);
	if (newData == nullptr)
	{
		Debug::log("Failed to allocate space for {}", token->configId);
		return -1;
	}

	// If we were paranoid about the incomming data we could make this
	// something that we call into a separate compartment to do 
	memcpy(newData, data, size);

	// Free the old data value.  Any subscribers that received it should
	// have thier own claim on it if needed
	if (c->data)
	{
		free(c->data);
	}

	// Neither we nor the subscribers need to be able to update the
	// value, so just track through a readOnly capabaility
	c->data = newData;
	CHERI::Capability(c->data).permissions() &=
	  {CHERI::Permission::Load, CHERI::Permission::Global};

	// Mark it as having been updated
	c->updated = true;

	// Trigger out thread to process the update 
	pending++;
	futex_wake(&pending, -1);

	return 0;
}

//
// Register to get config data for a compartment.  The callback
// function will be called whenever an item listed in the capability
// changes.  If data is already available then the callback
// will be called immediately.
//
void __cheri_compartment("config_broker")
  on_config(SObj sealedCap, __cheri_callback void cb(const char *, void *))
{
	// Get the calling compartments name from
	// its sealed capability
	ConfigToken *token = config_capability_unseal(sealedCap);

	if (token == nullptr)
	{
		Debug::log("Invalid id");
		return;
	}

	// for (auto i = 0; i < token->count; i++)
	//{
	Debug::log("thread {} on_config called for {} by id {}",
	           thread_id_get(),
	           static_cast<const char *>(token->configId),
	           token->id);

	auto c = find_or_create_config(token->configId);
	add_callback(c, token->id, cb);
	if (c->data)
	{
		cb(token->configId, c->data);
	}
	//}
}

//
// thread enrty point
//
void __cheri_compartment("config_broker") init()
{
	while (true)
	{
		// wait for updates
		futex_wait(&pending, 0);
		Debug::log("thread {} processing {} updates", thread_id_get(), pending);

		pending = 0;

		// Procces any the modified config data
		//
		// Two timing considerations for events that could
		// occur whilst were making the callbacks
		//
		// - If a new callback is registered they get called
		//   directly, so they might get called twice, which is OK
		//
		// - If a new data value is suppled then the rest of the
		//   callbacks will be called with the new value. The item
		//   will be tagged as updated and pending incremented so
		//   we also pick it up on the next loop. Some callbacks
		//   might be called twice with the same value, which is OK.
		//
		for (auto &c : configData)
		{
			if (c->updated)
			{
				c->updated = false;
				// Call all the callbacks
				for (auto &cbInfo : c->cbList)
				{
					cbInfo->cb(c->name, c->data);
				}
			}
		}
	}
}