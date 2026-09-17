#pragma once
#include "cJSON.h"
namespace book_transfer {
// Serialized, bounded SD book transfer only. No flash or arbitrary filesystem API.
bool Handle(const char* command, cJSON* request, cJSON* reply);
}
