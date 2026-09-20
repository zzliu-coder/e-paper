/* Test-only public ABI declarations for linking the installed cJSON library.
 * Firmware uses ESP-IDF's real cJSON.h. No parser implementation is mocked. */
#ifndef FL4_TEST_CJSON_H
#define FL4_TEST_CJSON_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct cJSON {struct cJSON* next;struct cJSON* prev;struct cJSON* child;int type;char* valuestring;int valueint;double valuedouble;char* string;} cJSON;
cJSON* cJSON_ParseWithLengthOpts(const char*,size_t,const char**,int);
void cJSON_Delete(cJSON*);
cJSON* cJSON_GetObjectItemCaseSensitive(const cJSON*,const char*);
cJSON* cJSON_GetArrayItem(const cJSON*,int);
int cJSON_GetArraySize(const cJSON*);
int cJSON_IsNumber(const cJSON*);int cJSON_IsBool(const cJSON*);int cJSON_IsTrue(const cJSON*);
int cJSON_IsString(const cJSON*);int cJSON_IsArray(const cJSON*);int cJSON_IsObject(const cJSON*);
cJSON* cJSON_CreateObject(void);cJSON* cJSON_Duplicate(const cJSON*,int);
cJSON* cJSON_AddNumberToObject(cJSON*,const char*,double);
cJSON* cJSON_AddBoolToObject(cJSON*,const char*,int);
cJSON* cJSON_AddStringToObject(cJSON*,const char*,const char*);
cJSON* cJSON_AddArrayToObject(cJSON*,const char*);
int cJSON_AddItemToArray(cJSON*,cJSON*);int cJSON_AddItemToObject(cJSON*,const char*,cJSON*);
char* cJSON_PrintUnformatted(const cJSON*);void cJSON_free(void*);
#ifdef __cplusplus
}
#endif
#endif
