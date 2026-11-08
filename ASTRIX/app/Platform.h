#pragma once
// Единая точка подключения Windows: без макросов min/max, которые ломают std::min/std::max.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
