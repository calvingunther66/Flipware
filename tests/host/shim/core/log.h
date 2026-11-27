#pragma once
// Host shim: logging compiles away, so code under test can keep its FURI_LOG_* calls.
#define FURI_LOG_T(...)
#define FURI_LOG_D(...)
#define FURI_LOG_I(...)
#define FURI_LOG_W(...)
#define FURI_LOG_E(...)
