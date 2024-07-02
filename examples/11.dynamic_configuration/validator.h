// Copyright Microsoft and CHERIoT Contributors.
// SPDX-License-Identifier: MIT

// Contributed by Configured Things Ltd

#include "cdefs.h"
#include <compartment.h>

/**
 * Run a validator data in sandpit compartment
 *
 * Returns 0 if valid, -1 if not 
 */
int __cheri_compartment("validator") validate(void *data);