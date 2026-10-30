#pragma once
/* Reconcile regular-file read-ahead with inherited/shared fd offsets. */
int __aquaos_stdio_sync_inputs(void);
