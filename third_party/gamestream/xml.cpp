/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include "xml.h"

#include <expat.h>
#include <string.h>

#include <exception>
#include <string>

#include "client.h"
#include "debug.hpp"
#include "errors.h"

#define STATUS_OK 200

struct xml_query
{
    char* memory;
    size_t size;
    int start;
    void* data;
};

struct xml_string_query
{
    const char* target;
    int depth;
    bool capturing;
    std::string* out;
};

static void XMLCALL _xml_string_start(void* userData, const char* name, const char** atts)
{
    xml_string_query* q = (xml_string_query*)userData;
    if (strcmp(name, q->target) == 0)
    {
        if (q->depth == 0)
        {
            q->capturing = true;
        }
        q->depth++;
    }
}

static void XMLCALL _xml_string_end(void* userData, const char* name)
{
    xml_string_query* q = (xml_string_query*)userData;
    if (strcmp(name, q->target) == 0)
    {
        if (q->depth > 0)
        {
            q->depth--;
            if (q->depth == 0)
            {
                q->capturing = false;
            }
        }
    }
}

static void XMLCALL _xml_start_applist_element(void* userData, const char* name,
    const char** atts)
{
    struct xml_query* search = (struct xml_query*)userData;
    if (strcmp("App", name) == 0)
    {
        PAPP_LIST app = (PAPP_LIST)malloc(sizeof(APP_LIST));
        if (app == NULL)
        {
            return;
        }

        app->id      = 0;
        app->name    = NULL;
        app->next    = (PAPP_LIST)search->data;
        search->data = app;
    }
    else if (strcmp("ID", name) == 0 || strcmp("AppTitle", name) == 0)
    {
        search->memory = (char*)malloc(1);
        search->size   = 0;
        search->start  = 1;
    }
}

static void XMLCALL _xml_end_applist_element(void* userData, const char* name)
{
    struct xml_query* search = (struct xml_query*)userData;
    if (search->start)
    {
        PAPP_LIST list = (PAPP_LIST)search->data;
        if (list == NULL)
            return;

        if (strcmp("ID", name) == 0)
        {
            list->id = search->memory ? atoi(search->memory) : 0;
            free(search->memory);
            search->memory = NULL;
        }
        else if (strcmp("AppTitle", name) == 0)
        {
            // Expat's buffer is only valid during the callback.
            if (search->memory)
            {
                list->name = strdup(search->memory);
                free(search->memory);
                search->memory = NULL;
            }
            else
            {
                list->name = NULL;
            }
        }
        search->start = 0;
    }
}

static void XMLCALL _xml_start_status_element(void* userData, const char* name,
    const char** atts)
{
    if (strcmp("root", name) == 0)
    {
        int* status = (int*)userData;
        for (int i = 0; atts[i]; i += 2)
        {
            if (strcmp("status_code", atts[i]) == 0)
            {
                *status = atts[i + 1] ? atoi(atts[i + 1]) : 0;
            }
            else if (*status != STATUS_OK && strcmp("status_message", atts[i]) == 0)
            {
                if (atts[i + 1])
                {
                    char* msg_copy = strdup(atts[i + 1]);
                    if (msg_copy)
                    {
                        gs_set_error(msg_copy);
                    }
                }
            }
        }
    }
}

static void XMLCALL _xml_end_status_element(void* userData, const char* name) { }

static void XMLCALL _xml_write_data_legacy(void* userData, const XML_Char* s, int len)
{
    struct xml_query* search = (struct xml_query*)userData;
    if (search->start > 0)
    {
        const size_t MAX_XML_VALUE = 8192;
        if (search->size + (size_t)len + 1 > MAX_XML_VALUE)
        {
            vita_log::error("[xml] XML value exceeds 8 KB; refusing to append");
            return;
        }
        char* newMem = (char*)realloc(search->memory, search->size + len + 1);
        if (!newMem)
        {
            vita_log::error("[xml] realloc failed (size=%lu, len=%d)", (unsigned long)search->size, len);
            return;
        }
        search->memory = newMem;
        if (len > 0)
        {
            memcpy(search->memory + search->size, s, len);
            search->size += len;
        }
        search->memory[search->size] = '\0';
    }
}

static void XMLCALL _xml_write_data_string(void* userData, const XML_Char* s, int len)
{
    xml_string_query* q = (xml_string_query*)userData;
    if (q->capturing && len > 0)
    {
        if (q->out->size() + (size_t)len > 8192)
        {
            vita_log::error("[xml_string] XML value exceeds 8 KB; refusing to append");
            return;
        }
        q->out->append(s, len);
    }
}

int xml_search(const Data& data, const std::string node, int* result)
{
    std::string text;
    auto res = xml_search(data, node, &text);
    if (res != GS_OK || text.empty())
    {
        *result = 0;
        return res;
    }
    try
    {
        *result = std::stoi(text);
    }
    catch (const std::exception&)
    {
        *result = 0;
        return GS_INVALID;
    }
    return res;
}

int xml_search(const Data& data, const std::string node, std::string* result)
{
    vita_log::info("[xml_search] Looking for node '%s' (%u bytes)", node.c_str(), (unsigned)data.size());
    result->clear();
    xml_string_query q;
    q.target    = node.c_str();
    q.depth     = 0;
    q.capturing = false;
    q.out       = result;

    XML_Parser parser = XML_ParserCreate("UTF-8");
    if (!parser)
    {
        gs_set_error("XML_ParserCreate fallo");
        return GS_INVALID;
    }
    XML_SetUserData(parser, &q);
    XML_SetElementHandler(parser, _xml_string_start, _xml_string_end);
    XML_SetCharacterDataHandler(parser, _xml_write_data_string);

    if (!XML_Parse(parser, (const char*)data.bytes(), (int)data.size(), 1))
    {
        XML_Error code = XML_GetErrorCode(parser);
        gs_set_error(XML_ErrorString(code));
        vita_log::error("[xml_search] Parse FAIL nodo='%s' error=%d", node.c_str(), (int)code);
        XML_ParserFree(parser);
        return GS_INVALID;
    }
    XML_ParserFree(parser);
    vita_log::info("[xml_search] Node '%s' found (%u bytes)", node.c_str(), (unsigned)result->size());
    return GS_OK;
}

int xml_applist(const Data& data, PAPP_LIST* app_list)
{
    struct xml_query query;
    query.memory = (char*)calloc(1, 1);
    query.size   = 0;
    query.start  = 0;
    query.data   = NULL;

    XML_Parser parser = XML_ParserCreate("UTF-8");
    XML_SetUserData(parser, &query);
    XML_SetElementHandler(parser, _xml_start_applist_element,
        _xml_end_applist_element);
    XML_SetCharacterDataHandler(parser, _xml_write_data_legacy);

    if (!XML_Parse(parser, (const char*)data.bytes(), (int)data.size(), 1))
    {
        XML_Error code = XML_GetErrorCode(parser);
        gs_set_error(XML_ErrorString(code));
        XML_ParserFree(parser);
        return GS_INVALID;
    }

    XML_ParserFree(parser);
    free(query.memory); // Liberar memoria no utilizada
    *app_list = (PAPP_LIST)query.data;
    return GS_OK;
}

int xml_status(const Data& data)
{
    int status        = 0;
    XML_Parser parser = XML_ParserCreate("UTF-8");
    XML_SetUserData(parser, &status);
    XML_SetElementHandler(parser, _xml_start_status_element,
        _xml_end_status_element);

    if (!XML_Parse(parser, (const char*)data.bytes(), (int)data.size(), 1))
    {
        XML_Error code = XML_GetErrorCode(parser);
        gs_set_error(XML_ErrorString(code));
        XML_ParserFree(parser);
        return GS_INVALID;
    }

    XML_ParserFree(parser);
    return status == STATUS_OK ? GS_OK : GS_ERROR;
}
