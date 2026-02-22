/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "shell_ncui.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "celix_array_list.h"
#include "celix_bundle.h"
#include "celix_bundle_context.h"
#include "celix_dependency_manager.h"
#include "celix_dm_info.h"
#include "celix_filter.h"
#include "celix_framework.h"
#include "celix_threads.h"
#include "celix_constants.h"
#include "celix_shell.h"
#include "celix_shell_constants.h"
#include "celix_stdlib_cleanup.h"
#include "celix_utils.h"

#define MAX_LINE_LEN 1024
#define MAX_TERMINAL_HISTORY 256
#define MAX_LOG_HISTORY 512

typedef enum ncui_screen {
    SCREEN_MAIN,
    SCREEN_BUNDLES,
    SCREEN_COMPONENTS,
    SCREEN_CACHE,
    SCREEN_HELP,
    SCREEN_LOGGING,
    SCREEN_TERMINAL,
    SCREEN_QUERY_INPUT,
    SCREEN_QUERY_RESULTS
} ncui_screen_e;

typedef enum item_type {
    ITEM_BUNDLE,
    ITEM_SERVICES_ROOT,
    ITEM_TRACKERS_ROOT,
    ITEM_COMPONENTS_ROOT,
    ITEM_SERVICE,
    ITEM_TRACKER,
    ITEM_COMPONENT,
    ITEM_CACHE_ROOT,
    ITEM_CACHE_PATH,
    ITEM_CACHE_ENTRY,
    ITEM_DETAIL
} item_type_e;

typedef struct view_item {
    item_type_e type;
    long bundleId;
    char* key;
    char* line;
    int indent;
    bool expandable;
} view_item_t;

typedef struct bundle_info {
    long id;
    char* symName;
    char* name;
    char* group;
} bundle_info_t;

typedef enum ncui_color_role {
    NCUI_COLOR_DEFAULT,
    NCUI_COLOR_TITLE,
    NCUI_COLOR_HEADER,
    NCUI_COLOR_FOOTER,
    NCUI_COLOR_BUNDLE,
    NCUI_COLOR_SECTION,
    NCUI_COLOR_SERVICE,
    NCUI_COLOR_TRACKER,
    NCUI_COLOR_COMPONENT,
    NCUI_COLOR_DETAIL,
    NCUI_COLOR_LOG,
    NCUI_COLOR_ERROR
} ncui_color_role_e;

typedef struct ansi_style {
    int fg;
    int bg;
    bool bold;
    bool underline;
    bool reverse;
} ansi_style_t;

typedef struct terminal_span {
    char* text;
    ansi_style_t style;
} terminal_span_t;

typedef struct terminal_line {
    celix_array_list_t* spans; //terminal_span_t*
} terminal_line_t;

struct shell_ncui {
    celix_bundle_context_t* ctx;
    celix_thread_t thread;
    celix_thread_mutex_t mutex;
    celix_shell_t* shell;

    int stopPipe[2];
    int logPipe[2];
    int savedStdout;
    int savedStderr;
    bool running;

    ncui_screen_e screen;
    int selected;
    int top;

    celix_array_list_t* items; //view_item_t*
    celix_array_list_t* expanded; //char*

    char terminalInput[MAX_LINE_LEN];
    celix_array_list_t* terminalHistory; //terminal_line_t*
    celix_array_list_t* terminalCommandHistory; //char*
    int terminalCommandHistoryIndex;
    char terminalInputDraft[MAX_LINE_LEN];

    celix_array_list_t* logHistory; //char*
    char logPartial[MAX_LINE_LEN];
    size_t logPartialLen;

    char queryInput[MAX_LINE_LEN];
    bool ansiEnabled;
    bool colorsSupported;
    long long lastCPressTimeMs;
    long cacheBundleId;
};

static const char* const BUNDLES_SCREEN_TITLE = "ASF Celix: Bundles";
static const char* const GENERIC_SCREEN_FOOTER = "ENTER expand/collapse (CTRL-ENTER all) | b bundles | c components | m main | t terminal | l logs | q query | ? help";
static const char* const BUNDLES_SCREEN_FOOTER_PREFIX = "a cache | s start/stop | u uninstall";

static const char* const COMPONENTS_SCREEN_TITLE = "ASF Celix: Components";
static const char* const COMPONENTS_SCREEN_FOOTER_PREFIX = "";

static const char* const CACHE_SCREEN_TITLE = "ASF Celix: Bundle Cache";
static const char* const CACHE_SCREEN_FOOTER_PREFIX = "";

static const char* const QUERY_SCREEN_TITLE = "ASF Celix - Query Result (ESC to exit)";
static const char* const QUERY_SCREEN_FOOTER_PREFIX = "q new query";

static const char* const LOG_SCREEN_TITLE = "ASF Celix - Logging (b bundles)";
static const char* const MAIN_SCREEN_TITLE = "ASF Celix - Main";
static const char* const MAIN_SCREEN_FOOTER_PREFIX = "";
static const char* const MAIN_LOGO[] = {
        "                                                                      ",
        "      db      `7MM\"\"\"Mq.   db       .g8\"\"\"bgd `7MMF'  `7MMF'`7MM\"\"\"YMM  ",
        "     ;MM:       MM   `MM. ;MM:    .dP'     `M   MM      MM    MM    `7  ",
        "    ,V^MM.      MM   ,M9 ,V^MM.   dM'       `   MM      MM    MM   d    ",
        "   ,M  `MM      MMmmdM9 ,M  `MM   MM            MMmmmmmmMM    MMmmMM    ",
        "   AbmmmqMA     MM      AbmmmqMA  MM.           MM      MM    MM   Y  , ",
        "  A'     VML    MM     A'     VML `Mb.     ,'   MM      MM    MM     ,M ",
        ".AMA.   .AMMA..JMML. .AMA.   .AMMA. `\"bmmmd'  .JMML.  .JMML..JMMmmmmMMM ",
        "                              ,,    ,,               ",
        "                              .g8\"\"\"bgd        `7MM    db               ",
        "                            .dP'     `M          MM                     ",
        "                            dM'       ` .gP\"Ya   MM  `7MM  `7M'   `MF'  ",
        "                            MM         ,M'   Yb  MM    MM    `VA ,V'    ",
        "                            MM.        8M\"\"\"\"\"  MM    MM      XMX      ",
        "                            `Mb.     ,'YM.    ,  MM    MM    ,V' VA.    ",
        "                              `\"bmmmd'  `Mbmmd'.JMML..JMML..AM.   .MA.  ",
        NULL
};

#ifndef KEY_SENTER
#define KEY_SENTER -1
#endif

static const char* const BUNDLES_HEADER_FMT = "%-5s %-26s %-24s %-14s";
static const char* const BUNDLES_HEADER_ID = "ID";
static const char* const BUNDLES_HEADER_SYMBOLIC_NAME = "Symbolic Name";
static const char* const BUNDLES_HEADER_NAME = "Name";
static const char* const BUNDLES_HEADER_GROUP = "Group";

static const char* const COMPONENTS_HEADER_FMT = "%-5s %-24s %-12s %-6s";
static const char* const COMPONENTS_HEADER_BUNDLE = "Bnd";
static const char* const COMPONENTS_HEADER_NAME = "Component";
static const char* const COMPONENTS_HEADER_STATE = "State";
static const char* const COMPONENTS_HEADER_ACTIVE = "Active";


static void destroyViewItem(void* p) {
    view_item_t* item = p;
    if (item != NULL) {
        free(item->key);
        free(item->line);
        free(item);
    }
}

static void destroyBundleInfo(void* p) {
    bundle_info_t* info = p;
    if (info != NULL) {
        free(info->symName);
        free(info->name);
        free(info->group);
        free(info);
    }
}

static bool isExpanded(shell_ncui_t* ui, const char* key) {
    for (int i = 0; i < celix_arrayList_size(ui->expanded); ++i) {
        char* val = celix_arrayList_get(ui->expanded, i);
        if (strcmp(val, key) == 0) {
            return true;
        }
    }
    return false;
}

static void toggleExpanded(shell_ncui_t* ui, const char* key) {
    for (int i = 0; i < celix_arrayList_size(ui->expanded); ++i) {
        char* val = celix_arrayList_get(ui->expanded, i);
        if (strcmp(val, key) == 0) {
            celix_arrayList_removeAt(ui->expanded, i);
            return;
        }
    }
    celix_arrayList_add(ui->expanded, celix_utils_strdup(key));
}

static void removeExpanded(shell_ncui_t* ui, const char* key) {
    for (int i = 0; i < celix_arrayList_size(ui->expanded); ++i) {
        char* val = celix_arrayList_get(ui->expanded, i);
        if (strcmp(val, key) == 0) {
            celix_arrayList_removeAt(ui->expanded, i);
            return;
        }
    }
}

static long long currentTimeMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static bool isListScreen(const shell_ncui_t* ui) {
    return ui->screen == SCREEN_BUNDLES || ui->screen == SCREEN_COMPONENTS || ui->screen == SCREEN_QUERY_RESULTS || ui->screen == SCREEN_CACHE;
}

static void clearAllViews(shell_ncui_t* ui) {
    celix_arrayList_clear(ui->items);
    celix_arrayList_clear(ui->expanded);
    celix_arrayList_clear(ui->terminalHistory);
    celix_arrayList_clear(ui->logHistory);
    ui->terminalInput[0] = '\0';
    ui->terminalInputDraft[0] = '\0';
    ui->terminalCommandHistoryIndex = -1;
    ui->queryInput[0] = '\0';
    ui->logPartialLen = 0;
    ui->logPartial[0] = '\0';
    ui->selected = 0;
    ui->top = 0;
}

static void toggleExpandForCurrentScreen(shell_ncui_t* ui) {
    if (!isListScreen(ui)) {
        return;
    }

    bool hasExpandable = false;
    bool allExpanded = true;
    for (int i = 0; i < celix_arrayList_size(ui->items); ++i) {
        view_item_t* item = celix_arrayList_get(ui->items, i);
        if (!item->expandable || item->key == NULL) {
            continue;
        }
        hasExpandable = true;
        if (!isExpanded(ui, item->key)) {
            allExpanded = false;
            break;
        }
    }
    if (!hasExpandable) {
        return;
    }

    for (int i = 0; i < celix_arrayList_size(ui->items); ++i) {
        view_item_t* item = celix_arrayList_get(ui->items, i);
        if (!item->expandable || item->key == NULL) {
            continue;
        }
        if (allExpanded) {
            removeExpanded(ui, item->key);
        } else if (!isExpanded(ui, item->key)) {
            celix_arrayList_add(ui->expanded, celix_utils_strdup(item->key));
        }
    }
}

static void ansiStyleReset(ansi_style_t* style) {
    style->fg = COLOR_WHITE;
    style->bg = COLOR_BLACK;
    style->bold = false;
    style->underline = false;
    style->reverse = false;
}

static int ansiPairFor(int fg, int bg) {
    if (fg < 0 || bg < 0) {
        return 0;
    }
    return (fg + 1) + (bg * 8);
}

static void ansiStyleApplyCode(ansi_style_t* style, int code) {
    if (code == 0) {
        ansiStyleReset(style);
    } else if (code == 1) {
        style->bold = true;
    } else if (code == 22) {
        style->bold = false;
    } else if (code == 4) {
        style->underline = true;
    } else if (code == 24) {
        style->underline = false;
    } else if (code == 7) {
        style->reverse = true;
    } else if (code == 27) {
        style->reverse = false;
    } else if (code >= 30 && code <= 37) {
        style->fg = code - 30;
    } else if (code == 39) {
        style->fg = COLOR_WHITE;
    } else if (code >= 40 && code <= 47) {
        style->bg = code - 40;
    } else if (code == 49) {
        style->bg = COLOR_BLACK;
    } else if (code >= 90 && code <= 97) {
        style->fg = code - 90;
        style->bold = true;
    } else if (code >= 100 && code <= 107) {
        style->bg = code - 100;
        style->bold = true;
    }
}

static short colorRolePair(ncui_color_role_e role) {
    switch (role) {
        case NCUI_COLOR_TITLE: return (short)ansiPairFor(COLOR_CYAN, COLOR_BLACK);
        case NCUI_COLOR_HEADER: return (short)ansiPairFor(COLOR_YELLOW, COLOR_BLACK);
        case NCUI_COLOR_FOOTER: return (short)ansiPairFor(COLOR_BLUE, COLOR_BLACK);
        case NCUI_COLOR_BUNDLE: return (short)ansiPairFor(COLOR_CYAN, COLOR_BLACK);
        case NCUI_COLOR_SECTION: return (short)ansiPairFor(COLOR_YELLOW, COLOR_BLACK);
        case NCUI_COLOR_SERVICE: return (short)ansiPairFor(COLOR_GREEN, COLOR_BLACK);
        case NCUI_COLOR_TRACKER: return (short)ansiPairFor(COLOR_MAGENTA, COLOR_BLACK);
        case NCUI_COLOR_COMPONENT: return (short)ansiPairFor(COLOR_CYAN, COLOR_BLACK);
        case NCUI_COLOR_DETAIL: return (short)ansiPairFor(COLOR_WHITE, COLOR_BLACK);
        case NCUI_COLOR_LOG: return (short)ansiPairFor(COLOR_WHITE, COLOR_BLACK);
        case NCUI_COLOR_ERROR: return (short)ansiPairFor(COLOR_RED, COLOR_BLACK);
        case NCUI_COLOR_DEFAULT:
        default:
            return (short)ansiPairFor(COLOR_WHITE, COLOR_BLACK);
    }
}

static void applyColorRole(const shell_ncui_t* ui, ncui_color_role_e role, bool selected) {
    attrset(A_NORMAL);
    if (ui->colorsSupported) {
        short pair = colorRolePair(role);
        if (pair > 0) {
            attron(COLOR_PAIR(pair));
        }
    }
    if (role == NCUI_COLOR_TITLE || role == NCUI_COLOR_HEADER || role == NCUI_COLOR_BUNDLE || role == NCUI_COLOR_SECTION) {
        attron(A_BOLD);
    }
    if (selected) {
        attron(A_REVERSE);
    }
}

static void applyAnsiStyle(const shell_ncui_t* ui, const ansi_style_t* style, bool selected) {
    attrset(A_NORMAL);
    if (ui->colorsSupported) {
        int pair = ansiPairFor(style->fg, style->bg);
        if (pair > 0) {
            attron(COLOR_PAIR(pair));
        }
    }
    if (style->bold) {
        attron(A_BOLD);
    }
    if (style->underline) {
        attron(A_UNDERLINE);
    }
    if (style->reverse) {
        attron(A_REVERSE);
    }
    if (selected) {
        attron(A_REVERSE);
    }
}

static ncui_color_role_e colorRoleForItem(const view_item_t* item) {
    switch (item->type) {
        case ITEM_BUNDLE: return NCUI_COLOR_BUNDLE;
        case ITEM_SERVICES_ROOT:
        case ITEM_TRACKERS_ROOT:
        case ITEM_COMPONENTS_ROOT:
        case ITEM_CACHE_ROOT:
        case ITEM_CACHE_PATH:
            return NCUI_COLOR_SECTION;
        case ITEM_SERVICE: return NCUI_COLOR_SERVICE;
        case ITEM_TRACKER: return NCUI_COLOR_TRACKER;
        case ITEM_COMPONENT: return NCUI_COLOR_COMPONENT;
        case ITEM_CACHE_ENTRY:
        case ITEM_DETAIL:
        default:
            return NCUI_COLOR_DETAIL;
    }
}

static void drawTextWithRole(const shell_ncui_t* ui, int y, int x, int width, const char* text, ncui_color_role_e role, bool selected) {
    if (width <= 0 || text == NULL) {
        return;
    }
    applyColorRole(ui, role, selected);
    mvaddnstr(y, x, text, width);
    attrset(A_NORMAL);
}

static void destroyTerminalSpan(void* data) {
    terminal_span_t* span = data;
    if (span != NULL) {
        free(span->text);
        free(span);
    }
}

static void destroyTerminalLine(void* data) {
    terminal_line_t* line = data;
    if (line != NULL) {
        celix_arrayList_destroy(line->spans);
        free(line);
    }
}

static terminal_line_t* createTerminalLine(const char* rawLine, bool parseAnsi) {
    terminal_line_t* line = calloc(1, sizeof(*line));
    if (line == NULL) {
        return NULL;
    }
    celix_array_list_create_options_t opts = CELIX_EMPTY_ARRAY_LIST_CREATE_OPTIONS;
    opts.simpleRemovedCallback = destroyTerminalSpan;
    line->spans = celix_arrayList_createWithOptions(&opts);
    if (line->spans == NULL) {
        free(line);
        return NULL;
    }

    ansi_style_t style;
    ansiStyleReset(&style);
    char segment[MAX_LINE_LEN];
    size_t segLen = 0;
    const char* src = rawLine != NULL ? rawLine : "";

    for (size_t i = 0; src[i] != '\0'; ) {
        if (parseAnsi && src[i] == '\x1b' && src[i + 1] == '[') {
            if (segLen > 0) {
                segment[segLen] = '\0';
                terminal_span_t* span = calloc(1, sizeof(*span));
                if (span != NULL) {
                    span->text = celix_utils_strdup(segment);
                    span->style = style;
                    celix_arrayList_add(line->spans, span);
                }
                segLen = 0;
            }
            i += 2;
            char codeBuf[64];
            size_t codePos = 0;
            while (src[i] != '\0' && src[i] != 'm' && codePos + 1 < sizeof(codeBuf)) {
                codeBuf[codePos++] = src[i++];
            }
            codeBuf[codePos] = '\0';
            if (src[i] == 'm') {
                i += 1;
            }
            if (codePos == 0) {
                ansiStyleReset(&style);
            } else {
                char* save = NULL;
                char* tok = strtok_r(codeBuf, ";", &save);
                while (tok != NULL) {
                    ansiStyleApplyCode(&style, atoi(tok));
                    tok = strtok_r(NULL, ";", &save);
                }
            }
            continue;
        }
        if (segLen + 1 < sizeof(segment)) {
            segment[segLen++] = src[i];
        }
        i += 1;
    }

    if (segLen > 0 || celix_arrayList_size(line->spans) == 0) {
        segment[segLen] = '\0';
        terminal_span_t* span = calloc(1, sizeof(*span));
        if (span != NULL) {
            span->text = celix_utils_strdup(segment);
            span->style = style;
            celix_arrayList_add(line->spans, span);
        }
    }
    return line;
}

static void updateItems(shell_ncui_t* ui) {
    celix_arrayList_clear(ui->items);
}

static void addItem(shell_ncui_t* ui, item_type_e type, long bndId, const char* key, int indent, bool expandable, const char* fmt, ...) {
    celix_autofree char* line = NULL;
    va_list ap;
    va_start(ap, fmt);
    int rc = vasprintf(&line, fmt, ap);
    va_end(ap);
    if (rc < 0 || line == NULL) {
        return;
    }

    view_item_t* item = calloc(1, sizeof(*item));
    if (item == NULL) {
        return;
    }
    item->type = type;
    item->bundleId = bndId;
    item->key = key != NULL ? celix_utils_strdup(key) : NULL;
    item->line = celix_utils_strdup(line);
    item->indent = indent;
    item->expandable = expandable;
    celix_arrayList_add(ui->items, item);
}

static int compareBundle(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    bundle_info_t* ba = a.voidPtrVal;
    bundle_info_t* bb = b.voidPtrVal;
    return (ba->id > bb->id) - (ba->id < bb->id);
}

static void collectBundleCb(void* data, const celix_bundle_t* bnd) {
    celix_array_list_t* list = data;
    bundle_info_t* info = calloc(1, sizeof(*info));
    if (info == NULL) {
        return;
    }
    info->id = celix_bundle_getId(bnd);
    info->symName = celix_utils_strdup(celix_bundle_getSymbolicName(bnd));
    info->name = celix_utils_strdup(celix_bundle_getName(bnd));
    info->group = celix_utils_strdup(celix_bundle_getGroup(bnd));
    celix_arrayList_add(list, info);
}



typedef struct bundle_count_data {
    size_t count;
} bundle_count_data_t;

static void countServicesCb(void* data, const celix_bundle_t* bnd) {
    bundle_count_data_t* cd = data;
    celix_array_list_t* svcs = celix_bundle_listRegisteredServices(bnd);
    cd->count = (size_t)celix_arrayList_size(svcs);
    celix_bundle_destroyRegisteredServicesList(svcs);
}

static void countTrackersCb(void* data, const celix_bundle_t* bnd) {
    bundle_count_data_t* cd = data;
    celix_array_list_t* trackers = celix_bundle_listServiceTrackers(bnd);
    cd->count = (size_t)celix_arrayList_size(trackers);
    celix_arrayList_destroy(trackers);
}

static size_t nrOfServicesForBundle(shell_ncui_t* ui, long bndId) {
    bundle_count_data_t data = {0};
    celix_bundleContext_useBundle(ui->ctx, bndId, &data, countServicesCb);
    return data.count;
}

static size_t nrOfTrackersForBundle(shell_ncui_t* ui, long bndId) {
    bundle_count_data_t data = {0};
    celix_bundleContext_useBundle(ui->ctx, bndId, &data, countTrackersCb);
    return data.count;
}

static size_t nrOfComponentsForBundle(shell_ncui_t* ui, long bndId) {
    size_t result = 0;
    celix_dependency_manager_t* mng = celix_bundleContext_getDependencyManager(ui->ctx);
    celix_array_list_t* infos = celix_dependencyManager_createInfos(mng);
    for (int i = 0; i < celix_arrayList_size(infos); ++i) {
        celix_dependency_manager_info_t* info = celix_arrayList_get(infos, i);
        if (info->bndId == bndId) {
            result = (size_t)celix_arrayList_size(info->components);
            break;
        }
    }
    celix_dependencyManager_destroyInfos(mng, infos);
    return result;
}

typedef struct append_data {
    shell_ncui_t* ui;
    int indent;
} append_data_t;

static void appendServicesCb(void* data, const celix_bundle_t* bnd) {
    append_data_t* ad = data;
    shell_ncui_t* ui = ad->ui;
    celix_array_list_t* svcs = celix_bundle_listRegisteredServices(bnd);
    for (int i = 0; i < celix_arrayList_size(svcs); ++i) {
        celix_bundle_service_list_entry_t* entry = celix_arrayList_get(svcs, i);
        char key[128];
        snprintf(key, sizeof(key), "b:%li:svc:%li", celix_bundle_getId(bnd), entry->serviceId);
        addItem(ui, ITEM_SERVICE, celix_bundle_getId(bnd), key, ad->indent, true, "Service %li: %s", entry->serviceId, entry->serviceName);
        if (isExpanded(ui, key)) {
            CELIX_PROPERTIES_ITERATE(entry->serviceProperties, iter) {
                addItem(ui, ITEM_DETAIL, celix_bundle_getId(bnd), NULL, ad->indent + 1, false, "%s = %s", iter.key, iter.entry.value);
            }
        }
    }
    celix_bundle_destroyRegisteredServicesList(svcs);
}

static void appendTrackersCb(void* data, const celix_bundle_t* bnd) {
    append_data_t* ad = data;
    shell_ncui_t* ui = ad->ui;
    celix_array_list_t* trackers = celix_bundle_listServiceTrackers(bnd);
    for (int i = 0; i < celix_arrayList_size(trackers); ++i) {
        celix_bundle_service_tracker_list_entry_t* trk = celix_arrayList_get(trackers, i);
        char key[512];
        snprintf(key, sizeof(key), "b:%li:trk:%s", celix_bundle_getId(bnd), trk->filter);
        addItem(ui, ITEM_TRACKER, celix_bundle_getId(bnd), key, ad->indent, true, "Tracker: %s", trk->serviceName != NULL ? trk->serviceName : trk->filter);
        if (isExpanded(ui, key)) {
            addItem(ui, ITEM_DETAIL, celix_bundle_getId(bnd), NULL, ad->indent + 1, false, "Filter: %s", trk->filter);
            addItem(ui, ITEM_DETAIL, celix_bundle_getId(bnd), NULL, ad->indent + 1, false, "Tracked services: %lu", (long unsigned int)trk->nrOfTrackedServices);
        }
    }
    celix_arrayList_destroy(trackers);
}

typedef struct cache_entry {
    char* name;
    char* path;
    bool dir;
    off_t size;
} cache_entry_t;

static void destroyCacheEntry(void* data) {
    cache_entry_t* entry = data;
    if (entry != NULL) {
        free(entry->name);
        free(entry->path);
        free(entry);
    }
}

static int compareCacheEntry(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    const cache_entry_t* ea = a.voidPtrVal;
    const cache_entry_t* eb = b.voidPtrVal;
    if (ea->dir != eb->dir) {
        return ea->dir ? -1 : 1;
    }
    return strcmp(ea->name, eb->name);
}

static void appendCacheDir(shell_ncui_t* ui, long bndId, const char* keyPrefix, const char* rootPath, int indent, int depth) {
    if (depth > 3 || rootPath == NULL) {
        return;
    }
    DIR* dir = opendir(rootPath);
    if (dir == NULL) {
        addItem(ui, ITEM_DETAIL, bndId, NULL, indent, false, "[unavailable] %s", strerror(errno));
        return;
    }

    celix_array_list_create_options_t opts = CELIX_EMPTY_ARRAY_LIST_CREATE_OPTIONS;
    opts.simpleRemovedCallback = destroyCacheEntry;
    celix_autoptr(celix_array_list_t) entries = celix_arrayList_createWithOptions(&opts);

    struct dirent* ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        celix_autofree char* childPath = NULL;
        celix_autofree char* key = NULL;
        if (asprintf(&childPath, "%s/%s", rootPath, ent->d_name) < 0 || childPath == NULL) {
            continue;
        }
        if (asprintf(&key, "%s/%s", keyPrefix, ent->d_name) < 0 || key == NULL) {
            continue;
        }

        struct stat st;
        bool isDir = stat(childPath, &st) == 0 && S_ISDIR(st.st_mode);
        cache_entry_t* entry = calloc(1, sizeof(*entry));
        if (entry == NULL) {
            continue;
        }
        entry->name = celix_utils_strdup(ent->d_name);
        entry->path = celix_utils_strdup(childPath);
        entry->dir = isDir;
        entry->size = stat(childPath, &st) == 0 ? st.st_size : 0;
        celix_arrayList_add(entries, entry);
    }
    closedir(dir);

    celix_arrayList_sortEntries(entries, compareCacheEntry);
    for (int i = 0; i < celix_arrayList_size(entries); ++i) {
        cache_entry_t* entry = celix_arrayList_get(entries, i);
        addItem(ui, ITEM_CACHE_ENTRY, bndId, entry->path, indent, entry->dir, "%s %s", entry->dir ? "[D]" : "[F]", entry->name);
        if (entry->dir && isExpanded(ui, entry->path)) {
            appendCacheDir(ui, bndId, entry->path, entry->path, indent + 1, depth + 1);
        } else if (!entry->dir) {
            addItem(ui, ITEM_DETAIL, bndId, NULL, indent + 1, false, "Size: %ld bytes", (long)entry->size);
        }
    }
}

static void appendBundleCacheCb(void* data, const celix_bundle_t* bnd) {
    append_data_t* ad = data;
    shell_ncui_t* ui = ad->ui;
    const long bndId = celix_bundle_getId(bnd);

    celix_autofree char* entryPath = celix_bundle_getEntry(bnd, ".");
    celix_autofree char* dataPath = celix_bundle_getDataFile(bnd, ".");

    if (entryPath != NULL) {
        char key[128];
        snprintf(key, sizeof(key), "b:%li:cache:entry", bndId);
        addItem(ui, ITEM_CACHE_PATH, bndId, key, ad->indent, true, "Entry cache: %s", entryPath);
        if (isExpanded(ui, key)) {
            appendCacheDir(ui, bndId, key, entryPath, ad->indent + 1, 0);
        }
    }

    if (dataPath != NULL) {
        char key[128];
        snprintf(key, sizeof(key), "b:%li:cache:data", bndId);
        addItem(ui, ITEM_CACHE_PATH, bndId, key, ad->indent, true, "Data cache: %s", dataPath);
        if (isExpanded(ui, key)) {
            appendCacheDir(ui, bndId, key, dataPath, ad->indent + 1, 0);
        }
    }

    if (entryPath == NULL && dataPath == NULL) {
        addItem(ui, ITEM_DETAIL, bndId, NULL, ad->indent, false, "No cache path available for this bundle");
    }
}

static void appendComponentFullInfo(shell_ncui_t* ui, const celix_dependency_manager_info_t* info, const celix_dm_component_info_t* cmp, int indent) {
    const char* bndName = cmp->bundleSymbolicName != NULL ? cmp->bundleSymbolicName : info->bndSymbolicName;
    addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent, false, "UUID: %s", cmp->id != NULL ? cmp->id : "-");
    addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent, false, "Active: %s", cmp->active ? "true" : "false");
    addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent, false, "State: %s", cmp->state);
    addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent, false, "Bundle: %li (%s)", info->bndId, bndName != NULL ? bndName : "-");
    addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent, false, "Nr of times started: %lu", (long unsigned int)cmp->nrOfTimesStarted);
    addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent, false, "Nr of times resumed: %lu", (long unsigned int)cmp->nrOfTimesResumed);

    addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent, false, "Interfaces (%lu)", (long unsigned int)celix_arrayList_size(cmp->interfaces));
    for (int i = 0; i < celix_arrayList_size(cmp->interfaces); ++i) {
        celix_dm_interface_info_t* intfInfo = celix_arrayList_get(cmp->interfaces, i);
        addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent + 1, false, "Interface %i: %s", i + 1, intfInfo->name != NULL ? intfInfo->name : "-");
        if (intfInfo->properties != NULL) {
            CELIX_PROPERTIES_ITERATE(intfInfo->properties, iter) {
                addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent + 2, false, "%s = %s", iter.key, iter.entry.value);
            }
        }
    }

    addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent, false, "Dependencies (%lu)", (long unsigned int)celix_arrayList_size(cmp->dependency_list));
    for (int i = 0; i < celix_arrayList_size(cmp->dependency_list); ++i) {
        celix_dm_service_dependency_info_t* dep = celix_arrayList_get(cmp->dependency_list, i);
        const char* depName = dep->serviceName != NULL ? dep->serviceName : "(any)";
        addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent + 1, false, "Dependency %i: %s", i + 1, depName);
        addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent + 2, false, "Available: %s", dep->available ? "true" : "false");
        addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent + 2, false, "Required: %s", dep->required ? "true" : "false");
        addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent + 2, false, "Version Range: %s", dep->versionRange != NULL ? dep->versionRange : "N/A");
        addItem(ui, ITEM_DETAIL, info->bndId, NULL, indent + 2, false, "Filter: %s", dep->filter != NULL ? dep->filter : "N/A");
    }
}

static void appendComponents(shell_ncui_t* ui, long bndId, int indent) {
    celix_dependency_manager_t* mng = celix_bundleContext_getDependencyManager(ui->ctx);
    celix_array_list_t* infos = celix_dependencyManager_createInfos(mng);
    for (int i = 0; i < celix_arrayList_size(infos); ++i) {
        celix_dependency_manager_info_t* info = celix_arrayList_get(infos, i);
        if (info->bndId != bndId) {
            continue;
        }
        for (int j = 0; j < celix_arrayList_size(info->components); ++j) {
            celix_dm_component_info_t* cmp = celix_arrayList_get(info->components, j);
            const char* cmpId = cmp->id != NULL ? cmp->id : cmp->name;
            char key[256];
            snprintf(key, sizeof(key), "b:%li:cmp:%s", bndId, cmpId != NULL ? cmpId : "-");
            addItem(ui, ITEM_COMPONENT, bndId, key, indent, true, "Component: %s [%s]", cmp->name, cmp->state);
            if (isExpanded(ui, key)) {
                appendComponentFullInfo(ui, info, cmp, indent + 1);
            }
        }
    }
    celix_dependencyManager_destroyInfos(mng, infos);
}

static void rebuildBundleItems(shell_ncui_t* ui) {
    updateItems(ui);

    celix_array_list_create_options_t opts = CELIX_EMPTY_ARRAY_LIST_CREATE_OPTIONS;
    opts.simpleRemovedCallback = destroyBundleInfo;
    celix_autoptr(celix_array_list_t) bundles = celix_arrayList_createWithOptions(&opts);

    celix_bundleContext_useBundle(ui->ctx, CELIX_FRAMEWORK_BUNDLE_ID, bundles, collectBundleCb);
    celix_bundleContext_useBundles(ui->ctx, bundles, collectBundleCb);
    celix_arrayList_sortEntries(bundles, compareBundle);

    for (int i = 0; i < celix_arrayList_size(bundles); ++i) {
        bundle_info_t* bnd = celix_arrayList_get(bundles, i);
        const char* group = celix_utils_isStringNullOrEmpty(bnd->group) ? "-" : bnd->group;
        char key[64];
        snprintf(key, sizeof(key), "b:%li", bnd->id);
        addItem(ui, ITEM_BUNDLE, bnd->id, key, 0, true, "[%li] %-26s %-24s %-14s", bnd->id, bnd->symName != NULL ? bnd->symName : "-", bnd->name != NULL ? bnd->name : "-", group);
        if (isExpanded(ui, key)) {
            char subKey[64];
            append_data_t ad;

            snprintf(subKey, sizeof(subKey), "b:%li:services", bnd->id);
            addItem(ui, ITEM_SERVICES_ROOT, bnd->id, subKey, 1, true, "Services (%lu)", (long unsigned int)nrOfServicesForBundle(ui, bnd->id));
            if (isExpanded(ui, subKey)) {
                ad.ui = ui;
                ad.indent = 2;
                celix_bundleContext_useBundle(ui->ctx, bnd->id, &ad, appendServicesCb);
            }

            snprintf(subKey, sizeof(subKey), "b:%li:components", bnd->id);
            addItem(ui, ITEM_COMPONENTS_ROOT, bnd->id, subKey, 1, true, "Components (%lu)", (long unsigned int)nrOfComponentsForBundle(ui, bnd->id));
            if (isExpanded(ui, subKey)) {
                appendComponents(ui, bnd->id, 2);
            }

            snprintf(subKey, sizeof(subKey), "b:%li:trackers", bnd->id);
            addItem(ui, ITEM_TRACKERS_ROOT, bnd->id, subKey, 1, true, "Service Trackers (%lu)", (long unsigned int)nrOfTrackersForBundle(ui, bnd->id));
            if (isExpanded(ui, subKey)) {
                ad.ui = ui;
                ad.indent = 2;
                celix_bundleContext_useBundle(ui->ctx, bnd->id, &ad, appendTrackersCb);
            }
        }
    }

    int size = celix_arrayList_size(ui->items);
    if (size == 0) {
        ui->selected = 0;
        ui->top = 0;
    } else if (ui->selected >= size) {
        ui->selected = size - 1;
    }
    if (ui->top > ui->selected) {
        ui->top = ui->selected;
    }
}

static void rebuildCacheItems(shell_ncui_t* ui) {
    updateItems(ui);
    append_data_t ad;
    ad.ui = ui;
    ad.indent = 0;
    celix_bundleContext_useBundle(ui->ctx, ui->cacheBundleId, &ad, appendBundleCacheCb);

    if (celix_arrayList_size(ui->items) == 0) {
        addItem(ui, ITEM_DETAIL, ui->cacheBundleId, NULL, 0, false, "No cache info available for bundle %li", ui->cacheBundleId);
    }

    int size = celix_arrayList_size(ui->items);
    if (size == 0) {
        ui->selected = 0;
        ui->top = 0;
    } else if (ui->selected >= size) {
        ui->selected = size - 1;
    }
    if (ui->top > ui->selected) {
        ui->top = ui->selected;
    }
}

static void rebuildComponentsItems(shell_ncui_t* ui) {
    updateItems(ui);

    celix_array_list_create_options_t bndOpts = CELIX_EMPTY_ARRAY_LIST_CREATE_OPTIONS;
    bndOpts.simpleRemovedCallback = destroyBundleInfo;
    celix_autoptr(celix_array_list_t) bundles = celix_arrayList_createWithOptions(&bndOpts);

    celix_bundleContext_useBundle(ui->ctx, CELIX_FRAMEWORK_BUNDLE_ID, bundles, collectBundleCb);
    celix_bundleContext_useBundles(ui->ctx, bundles, collectBundleCb);
    celix_arrayList_sortEntries(bundles, compareBundle);

    celix_dependency_manager_t* mng = celix_bundleContext_getDependencyManager(ui->ctx);
    celix_array_list_t* infos = celix_dependencyManager_createInfos(mng);

    for (int i = 0; i < celix_arrayList_size(bundles); ++i) {
        bundle_info_t* bnd = celix_arrayList_get(bundles, i);
        celix_dependency_manager_info_t* bndInfo = NULL;
        for (int j = 0; j < celix_arrayList_size(infos); ++j) {
            celix_dependency_manager_info_t* info = celix_arrayList_get(infos, j);
            if (info->bndId == bnd->id) {
                bndInfo = info;
                break;
            }
        }
        if (bndInfo == NULL || celix_arrayList_size(bndInfo->components) == 0) {
            continue;
        }

        char bndKey[64];
        snprintf(bndKey, sizeof(bndKey), "c:b:%li", bnd->id);
        addItem(ui, ITEM_BUNDLE, bnd->id, bndKey, 0, false, "[%li] %s (%i components)", bnd->id, bnd->symName != NULL ? bnd->symName : "-", celix_arrayList_size(bndInfo->components));

        for (int j = 0; j < celix_arrayList_size(bndInfo->components); ++j) {
            celix_dm_component_info_t* cmp = celix_arrayList_get(bndInfo->components, j);
            const char* cmpId = cmp->id != NULL ? cmp->id : cmp->name;
            char cmpKey[256];
            snprintf(cmpKey, sizeof(cmpKey), "c:b:%li:cmp:%s", bndInfo->bndId, cmpId != NULL ? cmpId : "-");
            addItem(ui, ITEM_COMPONENT, bndInfo->bndId, cmpKey, 1, true, "%-24s %-12s %-6s", cmp->name, cmp->state, cmp->active ? "true" : "false");
            if (isExpanded(ui, cmpKey)) {
                appendComponentFullInfo(ui, bndInfo, cmp, 2);
            }
        }
    }
    celix_dependencyManager_destroyInfos(mng, infos);

    int size = celix_arrayList_size(ui->items);
    if (size == 0) {
        ui->selected = 0;
        ui->top = 0;
    } else if (ui->selected >= size) {
        ui->selected = size - 1;
    }
    if (ui->top > ui->selected) {
        ui->top = ui->selected;
    }
}

static void drawList(shell_ncui_t* ui, const char* title, const char* footer, const char* headerFmt, const char* header1, const char* header2, const char* header3, const char* header4) {
    erase();
    drawTextWithRole(ui, 0, 0, COLS - 1, title, NCUI_COLOR_TITLE, false);

    if (headerFmt != NULL) {
        char headerLine[MAX_LINE_LEN];
        snprintf(headerLine, sizeof(headerLine), headerFmt, header1, header2, header3, header4);
        drawTextWithRole(ui, 1, 0, COLS - 1, headerLine, NCUI_COLOR_HEADER, false);
    }

    int listStartY = headerFmt != NULL ? 2 : 1;
    int rows = LINES - listStartY - 1;
    if (ui->selected < ui->top) {
        ui->top = ui->selected;
    } else if (ui->selected >= ui->top + rows) {
        ui->top = ui->selected - rows + 1;
    }

    for (int i = 0; i < rows; ++i) {
        int idx = ui->top + i;
        if (idx >= celix_arrayList_size(ui->items)) {
            break;
        }
        view_item_t* item = celix_arrayList_get(ui->items, idx);
        int x = item->indent * 2;
        char marker[2] = " ";
        if (item->expandable) {
            marker[0] = isExpanded(ui, item->key) ? '-' : '+';
        }
        char line[MAX_LINE_LEN * 2];
        snprintf(line, sizeof(line), "%s %s", marker, item->line);
        drawTextWithRole(ui, i + listStartY, x, COLS - x - 1, line, colorRoleForItem(item), idx == ui->selected);
    }

    drawTextWithRole(ui, LINES - 1, 0, COLS - 1, footer, NCUI_COLOR_FOOTER, false);
    refresh();
}

static void createFooterLine(const char* screenSpecificPrefix, char* buffer, size_t bufferSize) {
    if (celix_utils_isStringNullOrEmpty(screenSpecificPrefix)) {
        snprintf(buffer, bufferSize, "%s", GENERIC_SCREEN_FOOTER);
    } else {
        snprintf(buffer, bufferSize, "%s | %s", screenSpecificPrefix, GENERIC_SCREEN_FOOTER);
    }
}

static void appendTerminal(shell_ncui_t* ui, const char* line) {
    terminal_line_t* terminalLine = createTerminalLine(line, ui->ansiEnabled);
    if (terminalLine == NULL) {
        return;
    }
    celix_arrayList_add(ui->terminalHistory, terminalLine);
    if (celix_arrayList_size(ui->terminalHistory) > MAX_TERMINAL_HISTORY) {
        celix_arrayList_removeAt(ui->terminalHistory, 0);
    }
}

static void appendTerminalCommand(shell_ncui_t* ui, const char* cmd) {
    if (celix_utils_isStringNullOrEmpty(cmd)) {
        return;
    }
    if (celix_arrayList_size(ui->terminalCommandHistory) > 0) {
        const char* last = celix_arrayList_get(ui->terminalCommandHistory, celix_arrayList_size(ui->terminalCommandHistory) - 1);
        if (strcmp(last, cmd) == 0) {
            return;
        }
    }
    celix_arrayList_add(ui->terminalCommandHistory, celix_utils_strdup(cmd));
    if (celix_arrayList_size(ui->terminalCommandHistory) > MAX_TERMINAL_HISTORY) {
        celix_arrayList_removeAt(ui->terminalCommandHistory, 0);
    }
}

static void setTerminalInput(shell_ncui_t* ui, const char* val) {
    snprintf(ui->terminalInput, sizeof(ui->terminalInput), "%s", val != NULL ? val : "");
}

static void terminalNavigateHistory(shell_ncui_t* ui, int dir) {
    int size = celix_arrayList_size(ui->terminalCommandHistory);
    if (size == 0) {
        return;
    }

    if (dir < 0) {
        if (ui->terminalCommandHistoryIndex < 0) {
            snprintf(ui->terminalInputDraft, sizeof(ui->terminalInputDraft), "%s", ui->terminalInput);
            ui->terminalCommandHistoryIndex = size - 1;
        } else if (ui->terminalCommandHistoryIndex > 0) {
            ui->terminalCommandHistoryIndex -= 1;
        }
    } else {
        if (ui->terminalCommandHistoryIndex < 0) {
            return;
        }
        if (ui->terminalCommandHistoryIndex + 1 >= size) {
            ui->terminalCommandHistoryIndex = -1;
            setTerminalInput(ui, ui->terminalInputDraft);
            return;
        }
        ui->terminalCommandHistoryIndex += 1;
    }

    if (ui->terminalCommandHistoryIndex >= 0 && ui->terminalCommandHistoryIndex < size) {
        setTerminalInput(ui, celix_arrayList_get(ui->terminalCommandHistory, ui->terminalCommandHistoryIndex));
    }
}

static void completeTerminalInput(shell_ncui_t* ui) {
    char* firstSpace = strchr(ui->terminalInput, ' ');
    if (firstSpace != NULL) {
        return; //simple command completion for the command token only
    }

    celixThreadMutex_lock(&ui->mutex);
    celix_shell_t* shell = ui->shell;
    celixThreadMutex_unlock(&ui->mutex);
    if (shell == NULL || shell->getCommands == NULL) {
        return;
    }

    celix_array_list_t* commands = NULL;
    if (shell->getCommands(shell->handle, &commands) != CELIX_SUCCESS || commands == NULL) {
        return;
    }

    const char* prefix = ui->terminalInput;
    size_t prefixLen = strlen(prefix);
    int matches = 0;
    const char* firstMatch = NULL;
    char common[MAX_LINE_LEN] = {0};

    for (int i = 0; i < celix_arrayList_size(commands); ++i) {
        const char* cmd = celix_arrayList_get(commands, i);
        if (cmd == NULL || strncasecmp(cmd, prefix, prefixLen) != 0) {
            continue;
        }
        if (matches == 0) {
            snprintf(common, sizeof(common), "%s", cmd);
            firstMatch = cmd;
        } else {
            size_t j = 0;
            while (common[j] != '\0' && cmd[j] != '\0' && common[j] == cmd[j]) {
                j += 1;
            }
            common[j] = '\0';
        }
        matches += 1;
    }

    if (matches == 1 && firstMatch != NULL) {
        snprintf(ui->terminalInput, sizeof(ui->terminalInput), "%s ", firstMatch);
    } else if (matches > 1) {
        if (strlen(common) > prefixLen) {
            snprintf(ui->terminalInput, sizeof(ui->terminalInput), "%s", common);
        } else {
            appendTerminal(ui, "[NCUI] multiple completion matches. press TAB after typing more characters.");
        }
    }

    for (int i = 0; i < celix_arrayList_size(commands); ++i) {
        free(celix_arrayList_get(commands, i));
    }
    celix_arrayList_destroy(commands);
}

static void appendLog(shell_ncui_t* ui, const char* line) {
    celix_arrayList_add(ui->logHistory, celix_utils_strdup(line));
    if (celix_arrayList_size(ui->logHistory) > MAX_LOG_HISTORY) {
        celix_arrayList_removeAt(ui->logHistory, 0);
    }
}

static void drainLogPipe(shell_ncui_t* ui) {
    if (ui->logPipe[0] < 0) {
        return;
    }

    char buffer[512];
    while (true) {
        ssize_t n = read(ui->logPipe[0], buffer, sizeof(buffer));
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return;
            }
            return;
        }

        size_t start = 0;
        for (size_t i = 0; i < (size_t)n; ++i) {
            if (buffer[i] == '\n') {
                size_t segLen = i - start;
                char line[MAX_LINE_LEN];
                size_t pos = 0;
                if (ui->logPartialLen > 0) {
                    size_t toCopy = ui->logPartialLen < (MAX_LINE_LEN - 1) ? ui->logPartialLen : (MAX_LINE_LEN - 1);
                    memcpy(line, ui->logPartial, toCopy);
                    pos = toCopy;
                }
                if (pos < (MAX_LINE_LEN - 1) && segLen > 0) {
                    size_t remaining = (MAX_LINE_LEN - 1) - pos;
                    size_t segCopy = segLen < remaining ? segLen : remaining;
                    memcpy(line + pos, buffer + start, segCopy);
                    pos += segCopy;
                }
                line[pos] = '\0';
                appendLog(ui, line);
                ui->logPartialLen = 0;
                ui->logPartial[0] = '\0';
                start = i + 1;
            }
        }

        if (start < (size_t)n) {
            size_t segLen = (size_t)n - start;
            size_t available = (MAX_LINE_LEN - 1) - ui->logPartialLen;
            size_t segCopy = segLen < available ? segLen : available;
            if (segCopy > 0) {
                memcpy(ui->logPartial + ui->logPartialLen, buffer + start, segCopy);
                ui->logPartialLen += segCopy;
                ui->logPartial[ui->logPartialLen] = '\0';
            }
        }
    }
}

static void execTerminalCommand(shell_ncui_t* ui, const char* cmd) {
    appendTerminalCommand(ui, cmd);
    appendTerminal(ui, cmd);
    celixThreadMutex_lock(&ui->mutex);
    celix_shell_t* shell = ui->shell;
    celixThreadMutex_unlock(&ui->mutex);

    if (shell == NULL) {
        appendTerminal(ui, "[NCUI] shell service unavailable");
        return;
    }

    char* outBuf = NULL;
    size_t outSize = 0;
    FILE* out = open_memstream(&outBuf, &outSize);
    if (out == NULL) {
        return;
    }
    shell->executeCommand(shell->handle, cmd, out, out);
    fclose(out);

    if (outBuf != NULL && outBuf[0] != '\0') {
        char* save = NULL;
        char* line = strtok_r(outBuf, "\n", &save);
        while (line != NULL) {
            appendTerminal(ui, line);
            line = strtok_r(NULL, "\n", &save);
        }
    }
    free(outBuf);
}

typedef struct query_options {
    bool isFilter;
    char* textQuery;
    celix_filter_t* filter;
} query_options_t;

static bool queryMatchService(const query_options_t* opts, const celix_bundle_service_list_entry_t* svc) {
    if (opts->isFilter) {
        return opts->filter != NULL && celix_filter_match(opts->filter, svc->serviceProperties);
    }
    return celix_utils_isStringNullOrEmpty(opts->textQuery) || strcasestr(svc->serviceName, opts->textQuery) != NULL;
}

static bool queryMatchTracker(const query_options_t* opts, const celix_bundle_service_tracker_list_entry_t* trk) {
    if (opts->isFilter) {
        return opts->filter != NULL && strstr(trk->filter, celix_filter_getFilterString(opts->filter)) != NULL;
    }
    const char* text = trk->serviceName != NULL ? trk->serviceName : trk->filter;
    return celix_utils_isStringNullOrEmpty(opts->textQuery) || strcasestr(text, opts->textQuery) != NULL;
}

static bool parseQueryInput(const char* input, query_options_t* opts) {
    const char* start = input;
    while (*start != '\0' && isspace(*start)) {
        start += 1;
    }
    opts->isFilter = start[0] == '(';
    opts->textQuery = celix_utils_strdup(start);
    if (opts->textQuery == NULL) {
        return false;
    }
    if (opts->isFilter) {
        opts->filter = celix_filter_create(start);
        if (opts->filter == NULL) {
            free(opts->textQuery);
            opts->textQuery = NULL;
            return false;
        }
    }
    return true;
}

static void destroyQueryOptions(query_options_t* opts) {
    free(opts->textQuery);
    opts->textQuery = NULL;
    if (opts->filter != NULL) {
        celix_filter_destroy(opts->filter);
        opts->filter = NULL;
    }
}

typedef struct query_bundle_data {
    shell_ncui_t* ui;
    const query_options_t* opts;
} query_bundle_data_t;

static void queryBundleCb(void* data, const celix_bundle_t* bnd) {
    query_bundle_data_t* qd = data;
    shell_ncui_t* ui = qd->ui;
    const query_options_t* opts = qd->opts;

    const long bndId = celix_bundle_getId(bnd);
    const char* bndName = celix_bundle_getSymbolicName(bnd);

    celix_array_list_t* services = celix_bundle_listRegisteredServices(bnd);
    celix_array_list_t* trackers = celix_bundle_listServiceTrackers(bnd);

    size_t nrSvcMatches = 0;
    size_t nrTrkMatches = 0;
    for (int i = 0; i < celix_arrayList_size(services); ++i) {
        celix_bundle_service_list_entry_t* svc = celix_arrayList_get(services, i);
        nrSvcMatches += queryMatchService(opts, svc) ? 1 : 0;
    }
    for (int i = 0; i < celix_arrayList_size(trackers); ++i) {
        celix_bundle_service_tracker_list_entry_t* trk = celix_arrayList_get(trackers, i);
        nrTrkMatches += queryMatchTracker(opts, trk) ? 1 : 0;
    }

    if (nrSvcMatches == 0 && nrTrkMatches == 0) {
        celix_bundle_destroyRegisteredServicesList(services);
        celix_arrayList_destroy(trackers);
        return;
    }

    char bndKey[64];
    snprintf(bndKey, sizeof(bndKey), "q:b:%li", bndId);
    addItem(ui, ITEM_BUNDLE, bndId, bndKey, 0, false, "Bundle %li [%s]", bndId, bndName != NULL ? bndName : "-");

    char svcRootKey[128];
    snprintf(svcRootKey, sizeof(svcRootKey), "q:b:%li:services", bndId);
    addItem(ui, ITEM_SERVICES_ROOT, bndId, svcRootKey, 1, false, "Provided services (%lu)", (long unsigned int)nrSvcMatches);
    for (int i = 0; i < celix_arrayList_size(services); ++i) {
        celix_bundle_service_list_entry_t* svc = celix_arrayList_get(services, i);
        if (!queryMatchService(opts, svc)) {
            continue;
        }
        char key[128];
        snprintf(key, sizeof(key), "q:b:%li:svc:%li", bndId, svc->serviceId);
        addItem(ui, ITEM_SERVICE, bndId, key, 2, true, "Service %li: %s", svc->serviceId, svc->serviceName);
        if (isExpanded(ui, key)) {
            CELIX_PROPERTIES_ITERATE(svc->serviceProperties, it) {
                addItem(ui, ITEM_DETAIL, bndId, NULL, 3, false, "%s = %s", it.key, it.entry.value);
            }
        }
    }

    char trkRootKey[128];
    snprintf(trkRootKey, sizeof(trkRootKey), "q:b:%li:trackers", bndId);
    addItem(ui, ITEM_TRACKERS_ROOT, bndId, trkRootKey, 1, false, "Service trackers (%lu)", (long unsigned int)nrTrkMatches);
    for (int i = 0; i < celix_arrayList_size(trackers); ++i) {
        celix_bundle_service_tracker_list_entry_t* trk = celix_arrayList_get(trackers, i);
        if (!queryMatchTracker(opts, trk)) {
            continue;
        }
        addItem(ui, ITEM_TRACKER, bndId, NULL, 2, false, "Tracker: filter=%s | tracked services=%lu", trk->filter, (long unsigned int)trk->nrOfTrackedServices);
    }

    celix_bundle_destroyRegisteredServicesList(services);
    celix_arrayList_destroy(trackers);
}

static void rebuildQueryItems(shell_ncui_t* ui) {
    updateItems(ui);

    query_options_t opts;
    memset(&opts, 0, sizeof(opts));
    if (!parseQueryInput(ui->queryInput, &opts)) {
        addItem(ui, ITEM_DETAIL, -1, NULL, 0, false, "Invalid query syntax.");
        return;
    }

    query_bundle_data_t data;
    data.ui = ui;
    data.opts = &opts;

    celix_bundleContext_useBundle(ui->ctx, CELIX_FRAMEWORK_BUNDLE_ID, &data, queryBundleCb);
    celix_array_list_t* bndIds = celix_bundleContext_listBundles(ui->ctx);
    for (int i = 0; i < celix_arrayList_size(bndIds); ++i) {
        long bndId = celix_arrayList_getLong(bndIds, i);
        celix_bundleContext_useBundle(ui->ctx, bndId, &data, queryBundleCb);
    }
    celix_arrayList_destroy(bndIds);

    if (celix_arrayList_size(ui->items) == 0) {
        addItem(ui, ITEM_DETAIL, -1, NULL, 0, false, "No query results");
    }
    destroyQueryOptions(&opts);

    int size = celix_arrayList_size(ui->items);
    if (size == 0) {
        ui->selected = 0;
        ui->top = 0;
    } else if (ui->selected >= size) {
        ui->selected = size - 1;
    }
    if (ui->top > ui->selected) {
        ui->top = ui->selected;
    }
}

static void drawHelp(void) {
    erase();
    mvprintw(0, 0, "ASF Celix - Shortcut Overview (b bundles)");
    mvprintw(2, 0, "Arrow keys: select entries");
    mvprintw(3, 0, "ENTER: expand/collapse selected item");
    mvprintw(4, 0, "CTRL-ENTER: expand/collapse all visible list items");
    mvprintw(5, 0, "s: start/stop selected bundle");
    mvprintw(6, 0, "u: uninstall selected bundle");
    mvprintw(7, 0, "a: cache screen for selected bundle");
    mvprintw(8, 0, "t: terminal mode (TAB completion + up/down history)");
    mvprintw(9, 0, "l: logging view");
    mvprintw(10, 0, "q: query mode");
    mvprintw(11, 0, "c: components overview (cc clears all views)");
    mvprintw(12, 0, "b: bundles screen");
    mvprintw(13, 0, "?: help");
    mvprintw(14, 0, "ESC: exit screen");
    refresh();
}

typedef struct overview_data {
    size_t bundleCount;
    size_t serviceCount;
    size_t componentCount;
} overview_data_t;

static void countServicesForOverviewCb(void* data, const celix_bundle_t* bnd) {
    overview_data_t* o = data;
    celix_array_list_t* svcs = celix_bundle_listRegisteredServices(bnd);
    o->serviceCount += (size_t)celix_arrayList_size(svcs);
    celix_bundle_destroyRegisteredServicesList(svcs);
}

static overview_data_t collectOverview(shell_ncui_t* ui) {
    overview_data_t overview = {0};

    overview.bundleCount = 1; //framework bundle
    celix_array_list_t* bndIds = celix_bundleContext_listBundles(ui->ctx);
    overview.bundleCount += (size_t)celix_arrayList_size(bndIds);

    celix_bundleContext_useBundle(ui->ctx, CELIX_FRAMEWORK_BUNDLE_ID, &overview, countServicesForOverviewCb);
    for (int i = 0; i < celix_arrayList_size(bndIds); ++i) {
        long bndId = celix_arrayList_getLong(bndIds, i);
        celix_bundleContext_useBundle(ui->ctx, bndId, &overview, countServicesForOverviewCb);
    }
    celix_arrayList_destroy(bndIds);

    celix_dependency_manager_t* mng = celix_bundleContext_getDependencyManager(ui->ctx);
    celix_array_list_t* infos = celix_dependencyManager_createInfos(mng);
    for (int i = 0; i < celix_arrayList_size(infos); ++i) {
        celix_dependency_manager_info_t* info = celix_arrayList_get(infos, i);
        overview.componentCount += (size_t)celix_arrayList_size(info->components);
    }
    celix_dependencyManager_destroyInfos(mng, infos);
    return overview;
}

static void drawMain(shell_ncui_t* ui) {
    erase();
    drawTextWithRole(ui, 0, 0, COLS - 1, MAIN_SCREEN_TITLE, NCUI_COLOR_TITLE, false);
    int y = 2;
    for (int i = 0; MAIN_LOGO[i] != NULL && y < LINES - 1; ++i, ++y) {
        drawTextWithRole(ui, y, 0, COLS - 1, MAIN_LOGO[i], NCUI_COLOR_HEADER, false);
    }
    overview_data_t o = collectOverview(ui);
    celix_framework_t* fw = celix_bundleContext_getFramework(ui->ctx);
    struct timespec startTs = celix_framework_getStartTime(fw);
    double uptime = celix_framework_getUptimeInSeconds(fw);
    char startTimeStr[128] = "n/a";
    if (startTs.tv_sec > 0) {
        time_t secs = startTs.tv_sec;
        struct tm tmVal;
        if (localtime_r(&secs, &tmVal) != NULL) {
            strftime(startTimeStr, sizeof(startTimeStr), "%Y-%m-%d %H:%M:%S %z", &tmVal);
        }
    }

    mvprintw(y + 1, 0, "Shortcuts:");
    mvprintw(y + 2, 0, "b: Bundle screen (%lu bundles)", (long unsigned int)o.bundleCount);
    mvprintw(y + 3, 0, "c: Components screen (%lu components)", (long unsigned int)o.componentCount);
    mvprintw(y + 4, 0, "q: Query screen (%lu registered services)", (long unsigned int)o.serviceCount);
    mvprintw(y + 5, 0, "t: Terminal screen");
    mvprintw(y + 6, 0, "l: Logging screen");
    mvprintw(y + 7, 0, "m: Main screen");
    mvprintw(y + 8, 0, "?: Help");
    mvprintw(y + 10, 0, "Framework startup time: %s", startTimeStr);
    mvprintw(y + 11, 0, "Framework uptime      : %.1f seconds", uptime);
    char footerLine[MAX_LINE_LEN];
    createFooterLine(MAIN_SCREEN_FOOTER_PREFIX, footerLine, sizeof(footerLine));
    drawTextWithRole(ui, LINES - 1, 0, COLS - 1, footerLine, NCUI_COLOR_FOOTER, false);
    refresh();
}

static void drawTerminal(shell_ncui_t* ui) {
    erase();
    drawTextWithRole(ui, 0, 0, COLS - 1, "ASF Celix - Terminal (press ESC to exit)", NCUI_COLOR_TITLE, false);
    int rows = LINES - 2;
    int start = celix_arrayList_size(ui->terminalHistory) > rows ? celix_arrayList_size(ui->terminalHistory) - rows : 0;
    for (int i = 0; i < rows && (start + i) < celix_arrayList_size(ui->terminalHistory); ++i) {
        terminal_line_t* line = celix_arrayList_get(ui->terminalHistory, start + i);
        int x = 0;
        for (int s = 0; s < celix_arrayList_size(line->spans) && x < COLS - 1; ++s) {
            terminal_span_t* span = celix_arrayList_get(line->spans, s);
            if (span == NULL || span->text == NULL) {
                continue;
            }
            applyAnsiStyle(ui, &span->style, false);
            int avail = COLS - 1 - x;
            mvaddnstr(i + 1, x, span->text, avail);
            x += (int)strnlen(span->text, (size_t)avail);
        }
        attrset(A_NORMAL);
    }
    mvprintw(LINES - 1, 0, "> %.*s", COLS - 3, ui->terminalInput);
    refresh();
}

static void drawLogging(shell_ncui_t* ui) {
    erase();
    drawTextWithRole(ui, 0, 0, COLS - 1, LOG_SCREEN_TITLE, NCUI_COLOR_TITLE, false);
    int rows = LINES - 1;
    int start = celix_arrayList_size(ui->logHistory) > rows ? celix_arrayList_size(ui->logHistory) - rows : 0;
    for (int i = 0; i < rows && (start + i) < celix_arrayList_size(ui->logHistory); ++i) {
        char* line = celix_arrayList_get(ui->logHistory, start + i);
        drawTextWithRole(ui, i + 1, 0, COLS - 1, line, NCUI_COLOR_LOG, false);
    }
    refresh();
}

static void setupLogCapture(shell_ncui_t* ui) {
    ui->logPipe[0] = -1;
    ui->logPipe[1] = -1;
    ui->savedStdout = -1;
    ui->savedStderr = -1;

    if (pipe(ui->logPipe) != 0) {
        return;
    }

    ui->savedStdout = dup(STDOUT_FILENO);
    ui->savedStderr = dup(STDERR_FILENO);
    if (ui->savedStdout < 0 || ui->savedStderr < 0) {
        if (ui->savedStdout >= 0) {
            close(ui->savedStdout);
        }
        if (ui->savedStderr >= 0) {
            close(ui->savedStderr);
        }
        close(ui->logPipe[0]);
        close(ui->logPipe[1]);
        ui->logPipe[0] = -1;
        ui->logPipe[1] = -1;
        ui->savedStdout = -1;
        ui->savedStderr = -1;
        return;
    }

    if (dup2(ui->logPipe[1], STDOUT_FILENO) >= 0 && dup2(ui->logPipe[1], STDERR_FILENO) >= 0) {
        int flags = fcntl(ui->logPipe[0], F_GETFL, 0);
        if (flags >= 0) {
            (void)fcntl(ui->logPipe[0], F_SETFL, flags | O_NONBLOCK);
        }
        setvbuf(stdout, NULL, _IOLBF, 0);
        setvbuf(stderr, NULL, _IOLBF, 0);
    }

    close(ui->logPipe[1]);
    ui->logPipe[1] = -1;
}

static void restoreLogCapture(shell_ncui_t* ui) {
    if (ui->savedStdout >= 0) {
        (void)dup2(ui->savedStdout, STDOUT_FILENO);
        close(ui->savedStdout);
        ui->savedStdout = -1;
    }
    if (ui->savedStderr >= 0) {
        (void)dup2(ui->savedStderr, STDERR_FILENO);
        close(ui->savedStderr);
        ui->savedStderr = -1;
    }
    if (ui->logPipe[0] >= 0) {
        close(ui->logPipe[0]);
        ui->logPipe[0] = -1;
    }
    if (ui->logPipe[1] >= 0) {
        close(ui->logPipe[1]);
        ui->logPipe[1] = -1;
    }
}

static void readBundleStateCb(void* data, const celix_bundle_t* bnd) {
    celix_bundle_state_e* state = data;
    *state = celix_bundle_getState(bnd);
}

static void maybeActOnBundle(shell_ncui_t* ui, int key) {
    if (ui->selected >= celix_arrayList_size(ui->items)) {
        return;
    }
    view_item_t* item = celix_arrayList_get(ui->items, ui->selected);
    if (item->bundleId < CELIX_FRAMEWORK_BUNDLE_ID) {
        return;
    }

    if (key == 's') {
        celix_bundle_state_e state = CELIX_BUNDLE_STATE_UNKNOWN;
        celix_bundleContext_useBundle(ui->ctx, item->bundleId, &state, readBundleStateCb);
        if (state == CELIX_BUNDLE_STATE_ACTIVE || state == CELIX_BUNDLE_STATE_STARTING) {
            celix_bundleContext_stopBundle(ui->ctx, item->bundleId);
        } else {
            celix_bundleContext_startBundle(ui->ctx, item->bundleId);
        }
    } else if (key == 'u') {
        celix_bundleContext_uninstallBundle(ui->ctx, item->bundleId);
    }
}

static void initUiColors(shell_ncui_t* ui) {
    ui->colorsSupported = ui->ansiEnabled && has_colors();
    if (!ui->colorsSupported) {
        return;
    }
    start_color();
    use_default_colors();
    for (short bg = 0; bg < 8; ++bg) {
        for (short fg = 0; fg < 8; ++fg) {
            int pair = (fg + 1) + (bg * 8);
            init_pair(pair, fg, bg);
        }
    }
}

static void* shellNcui_run(void* data) {
    shell_ncui_t* ui = data;

    FILE* ttyIn = fopen("/dev/tty", "r");
    FILE* ttyOut = fopen("/dev/tty", "w");
    SCREEN* screen = NULL;
    if (ttyIn != NULL && ttyOut != NULL) {
        screen = newterm(NULL, ttyOut, ttyIn);
        if (screen != NULL) {
            set_term(screen);
        }
    }
    if (screen == NULL) {
        initscr();
    }
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    timeout(200);
    initUiColors(ui);

    while (ui->running) {
        drainLogPipe(ui);
        if (ui->screen == SCREEN_MAIN) {
            drawMain(ui);
        } else if (ui->screen == SCREEN_BUNDLES) {
            char footerLine[MAX_LINE_LEN];
            createFooterLine(BUNDLES_SCREEN_FOOTER_PREFIX, footerLine, sizeof(footerLine));
            rebuildBundleItems(ui);
            drawList(ui, BUNDLES_SCREEN_TITLE, footerLine, BUNDLES_HEADER_FMT, BUNDLES_HEADER_ID, BUNDLES_HEADER_SYMBOLIC_NAME, BUNDLES_HEADER_NAME, BUNDLES_HEADER_GROUP);
        } else if (ui->screen == SCREEN_COMPONENTS) {
            char footerLine[MAX_LINE_LEN];
            createFooterLine(COMPONENTS_SCREEN_FOOTER_PREFIX, footerLine, sizeof(footerLine));
            rebuildComponentsItems(ui);
            drawList(ui, COMPONENTS_SCREEN_TITLE, footerLine, COMPONENTS_HEADER_FMT, COMPONENTS_HEADER_BUNDLE, COMPONENTS_HEADER_NAME, COMPONENTS_HEADER_STATE, COMPONENTS_HEADER_ACTIVE);
        } else if (ui->screen == SCREEN_CACHE) {
            char footerLine[MAX_LINE_LEN];
            createFooterLine(CACHE_SCREEN_FOOTER_PREFIX, footerLine, sizeof(footerLine));
            rebuildCacheItems(ui);
            char cacheTitle[MAX_LINE_LEN];
            snprintf(cacheTitle, sizeof(cacheTitle), "%s [%li]", CACHE_SCREEN_TITLE, ui->cacheBundleId);
            drawList(ui, cacheTitle, footerLine, NULL, NULL, NULL, NULL, NULL);
        } else if (ui->screen == SCREEN_LOGGING) {
            drawLogging(ui);
        } else if (ui->screen == SCREEN_QUERY_RESULTS) {
            char footerLine[MAX_LINE_LEN];
            createFooterLine(QUERY_SCREEN_FOOTER_PREFIX, footerLine, sizeof(footerLine));
            rebuildQueryItems(ui);
            drawList(ui, QUERY_SCREEN_TITLE, footerLine, NULL, NULL, NULL, NULL, NULL);
        } else if (ui->screen == SCREEN_HELP) {
            drawHelp();
        } else if (ui->screen == SCREEN_TERMINAL) {
            drawTerminal(ui);
        } else if (ui->screen == SCREEN_QUERY_INPUT) {
            erase();
            drawTextWithRole(ui, 0, 0, COLS - 1, "ASF Celix - Query (ENTER run, ESC back)", NCUI_COLOR_TITLE, false);
            mvprintw(2, 0, "Query: %s", ui->queryInput);
            mvprintw(4, 0, "Query behavior:");
            mvprintw(5, 0, "- text: case-insensitive substring search on service/tracker name");
            mvprintw(6, 0, "- starts with '(': parse and use as LDAP filter");
            mvprintw(7, 0, "Tip: expand results for full service properties/details.");
            refresh();
        }

        int ch = getch();
        if (ch == ERR) {
            char b = 0;
            ssize_t n = read(ui->stopPipe[0], &b, 1);
            if (n > 0) {
                ui->running = false;
            }
            continue;
        }

        if (ui->screen == SCREEN_HELP) {
            if (ch == 'b' || ch == 27) {
                ui->screen = SCREEN_BUNDLES;
            }
            continue;
        }

        if (ui->screen == SCREEN_LOGGING) {
            if (ch == 'b' || ch == 27) {
                ui->screen = SCREEN_BUNDLES;
            }
            continue;
        }

        if (ui->screen == SCREEN_TERMINAL) {
            if (ch == 'b' || ch == 27) {
                ui->screen = SCREEN_BUNDLES;
            } else if (ch == '\n') {
                if (strlen(ui->terminalInput) > 0) {
                    execTerminalCommand(ui, ui->terminalInput);
                    ui->terminalInput[0] = '\0';
                    ui->terminalInputDraft[0] = '\0';
                    ui->terminalCommandHistoryIndex = -1;
                }
            } else if (ch == KEY_UP) {
                terminalNavigateHistory(ui, -1);
            } else if (ch == KEY_DOWN) {
                terminalNavigateHistory(ui, 1);
            } else if (ch == '\t') {
                completeTerminalInput(ui);
            } else if (ch == KEY_BACKSPACE || ch == 127) {
                size_t len = strlen(ui->terminalInput);
                if (len > 0) {
                    ui->terminalInput[len - 1] = '\0';
                }
            } else if (isprint(ch) && strlen(ui->terminalInput) + 1 < sizeof(ui->terminalInput)) {
                size_t len = strlen(ui->terminalInput);
                ui->terminalInput[len] = (char)ch;
                ui->terminalInput[len + 1] = '\0';
            }
            continue;
        }

        if (ui->screen == SCREEN_QUERY_INPUT) {
            if (ch == 27) {
                ui->screen = SCREEN_BUNDLES;
            } else if (ch == '\n') {
                ui->screen = SCREEN_QUERY_RESULTS;
            } else if (ch == KEY_BACKSPACE || ch == 127) {
                size_t len = strlen(ui->queryInput);
                if (len > 0) {
                    ui->queryInput[len - 1] = '\0';
                }
            } else if (isprint(ch) && strlen(ui->queryInput) + 1 < sizeof(ui->queryInput)) {
                size_t len = strlen(ui->queryInput);
                ui->queryInput[len] = (char)ch;
                ui->queryInput[len + 1] = '\0';
            }
            continue;
        }

        if (ch != 'c') {
            ui->lastCPressTimeMs = 0;
        }

        if (ch == '?') {
            ui->screen = SCREEN_HELP;
        } else if (ch == 'b') {
            ui->screen = SCREEN_BUNDLES;
        } else if (ch == 'm') {
            ui->screen = SCREEN_MAIN;
        } else if (ch == 't') {
            ui->screen = SCREEN_TERMINAL;
        } else if (ch == 'l') {
            ui->screen = SCREEN_LOGGING;
        } else if (ch == 'q') {
            ui->queryInput[0] = '\0';
            ui->screen = SCREEN_QUERY_INPUT;
        } else if (ch == 'c') {
            long long now = currentTimeMs();
            if (ui->lastCPressTimeMs > 0 && now - ui->lastCPressTimeMs < 750) {
                clearAllViews(ui);
                ui->lastCPressTimeMs = 0;
                continue;
            }
            ui->lastCPressTimeMs = now;
            ui->screen = SCREEN_COMPONENTS;
        } else if (ch == 'a' && ui->screen == SCREEN_BUNDLES && ui->selected < celix_arrayList_size(ui->items)) {
            view_item_t* item = celix_arrayList_get(ui->items, ui->selected);
            ui->cacheBundleId = item->bundleId;
            ui->selected = 0;
            ui->top = 0;
            ui->screen = SCREEN_CACHE;
        } else if (ch == 27 && ui->screen == SCREEN_QUERY_RESULTS) {
            ui->screen = SCREEN_BUNDLES;
        } else if (ch == 27 && ui->screen == SCREEN_COMPONENTS) {
            ui->screen = SCREEN_BUNDLES;
        } else if (ch == 27 && ui->screen == SCREEN_CACHE) {
            ui->screen = SCREEN_BUNDLES;
        } else if (ch == KEY_SENTER) { //CTRL-ENTER (if terminal reports modified ENTER)
            toggleExpandForCurrentScreen(ui);
        } else if ((ch == KEY_UP || ch == 'k') && ui->selected > 0) {
            ui->selected -= 1;
        } else if ((ch == KEY_DOWN || ch == 'j') && ui->selected + 1 < celix_arrayList_size(ui->items)) {
            ui->selected += 1;
        } else if (ch == '\n' && ui->selected < celix_arrayList_size(ui->items)) {
            view_item_t* item = celix_arrayList_get(ui->items, ui->selected);
            if (item->expandable && item->key != NULL) {
                toggleExpanded(ui, item->key);
            }
        } else if (ch == 's' || ch == 'u') {
            maybeActOnBundle(ui, ch);
        }
    }

    endwin();
    if (screen != NULL) {
        delscreen(screen);
    }
    if (ttyIn != NULL) {
        fclose(ttyIn);
    }
    if (ttyOut != NULL) {
        fclose(ttyOut);
    }
    return NULL;
}

shell_ncui_t* shellNcui_create(celix_bundle_context_t* ctx) {
    shell_ncui_t* ui = calloc(1, sizeof(*ui));
    if (ui == NULL) {
        return NULL;
    }

    ui->ctx = ctx;
    ui->screen = SCREEN_MAIN;

    celixThreadMutex_create(&ui->mutex, NULL);

    celix_array_list_create_options_t opts = CELIX_EMPTY_ARRAY_LIST_CREATE_OPTIONS;
    opts.simpleRemovedCallback = destroyViewItem;
    ui->items = celix_arrayList_createWithOptions(&opts);

    opts.simpleRemovedCallback = free;
    ui->expanded = celix_arrayList_createWithOptions(&opts);
    opts.simpleRemovedCallback = destroyTerminalLine;
    ui->terminalHistory = celix_arrayList_createWithOptions(&opts);
    opts.simpleRemovedCallback = free;
    ui->terminalCommandHistory = celix_arrayList_createWithOptions(&opts);
    ui->logHistory = celix_arrayList_createWithOptions(&opts);
    ui->terminalCommandHistoryIndex = -1;
    ui->ansiEnabled = celix_bundleContext_getPropertyAsBool(ctx, CELIX_SHELL_USE_ANSI_COLORS, CELIX_SHELL_USE_ANSI_COLORS_DEFAULT_VALUE);

    ui->logPipe[0] = -1;
    ui->logPipe[1] = -1;
    ui->savedStdout = -1;
    ui->savedStderr = -1;
    ui->cacheBundleId = CELIX_FRAMEWORK_BUNDLE_ID;

    return ui;
}

void shellNcui_destroy(shell_ncui_t* ui) {
    if (ui != NULL) {
        celix_arrayList_destroy(ui->items);
        celix_arrayList_destroy(ui->expanded);
        celix_arrayList_destroy(ui->terminalHistory);
        celix_arrayList_destroy(ui->terminalCommandHistory);
        celix_arrayList_destroy(ui->logHistory);
        celixThreadMutex_destroy(&ui->mutex);
        free(ui);
    }
}

celix_status_t shellNcui_start(shell_ncui_t* ui) {
    if (pipe(ui->stopPipe) != 0) {
        return CELIX_FILE_IO_EXCEPTION;
    }

    int flags = fcntl(ui->stopPipe[0], F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(ui->stopPipe[0], F_SETFL, flags | O_NONBLOCK);
    }

    setupLogCapture(ui);
    ui->running = true;
    celixThread_create(&ui->thread, NULL, shellNcui_run, ui);
    return CELIX_SUCCESS;
}

celix_status_t shellNcui_stop(shell_ncui_t* ui) {
    (void)write(ui->stopPipe[1], "s", 1);
    celixThread_join(ui->thread, NULL);
    close(ui->stopPipe[0]);
    close(ui->stopPipe[1]);
    restoreLogCapture(ui);
    return CELIX_SUCCESS;
}

celix_status_t shellNcui_setShell(shell_ncui_t* ui, celix_shell_t* shellSvc) {
    celixThreadMutex_lock(&ui->mutex);
    ui->shell = shellSvc;
    celixThreadMutex_unlock(&ui->mutex);
    return CELIX_SUCCESS;
}
