#include "util.h"

#include<string.h>
#include<stdio.h>
#include<stdlib.h>

void swapAppListEntries(PAPP_LIST a, PAPP_LIST b) {
    PAPP_LIST tmp = malloc(sizeof(APP_LIST));
    tmp->id = a->id;
    tmp->name = a->name;

    a->id = b->id;
    a->name = b->name;
    b->id = tmp->id;
    b->name = tmp->name;

    free(tmp);
}

void sortAppList(PAPP_LIST list) {
    if (list == NULL) {
        return;
    }

    int swapped = 0;
    PAPP_LIST cur = NULL;
    PAPP_LIST prev = NULL;

    do {
        swapped = 0;
        cur = list;

        while (cur->next != prev) {
            if (strcmp(cur->name, cur->next->name) > 0) {
                swapAppListEntries(cur, cur->next);
                swapped = 1;
            }
            cur = cur->next;
        }
        prev = cur;
    } while (swapped);
}
