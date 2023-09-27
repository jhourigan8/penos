#include <stdlib.h>
#include <stdio.h>
#include "table.h"
#include "filesys.h"
#include "../error.h"

void init(Table *t) {
    t->first = NULL;
    t->last = NULL;

    // add(t, "stdin", READ);
    // add(t, "stdout", WRITE);
    // add(t, "stderr", WRITE);
}

TNode* add(Table *t, char* name, int m) {
    TNode *node = malloc(sizeof(TNode));
    TNode *temp = find_empty(t);

    if (node) {
        // File Name
        node->file_name = name;
        // Mode
        node->mode = m;

        // Set file pointer 
        File f = get_file(name, true);

        if (m == READ) {
            node->file_pointer = 0;
            if (f.name[0] == 0x00) {
                cur_errno = ERR_NOENT;
                p_perror("file does not exist");
            }
        } else if (m == WRITE) {
            node->file_pointer = 0;
            if (f.name[0] == 0x00) {
                create_file(name, 1);
            }
            truncate_file(name, true);
        } else if (m == APPEND) {
            node->file_pointer = f.size;
        } else {
            free(node);
            return NULL;
        }

        // Set next and file descriptors
        if (!temp) {
            if (t->last) {
                node->next = t->first;
                node->file_descriptor = 3;
                t->first = node;
            } else {
                t->first = node;
                node->file_descriptor = 3;
            }
        } else {
            node->next = temp->next;
            node->file_descriptor = temp->file_descriptor + 1;
            temp->next = node;
        }

        return node;
    } else {
        perror("malloc error");
        return NULL;
    }
}

TNode* deq(Table *t) {
    TNode *node = t->first;
    if (!t->first) {
        node = NULL;
    } else if (!t->first->next) { 
        node = t->first;
        t->first = NULL;
        t->last = NULL;
    } else {
        node = t->first;
        t->first = t->first->next;
    }
    return node;
}

TNode* delete(Table *t, int fd) {
    TNode *node = t->first;
    TNode *node2 = NULL;
    while (node && node->file_descriptor != fd) {
        node2 = node;
        node = node->next;
    }
    
    if (!node2) {
        // TNode is first in queue
        return deq(t);
    } else if (!node) {
        // TNode wasn't found
        return NULL;
    } else {
        // TNode was in the middle of the queue
        node2->next = node->next;
        if (!node->next) {
        t->last = NULL;
        }
        node->next = NULL;
        return node;
    }
}

TNode* find_empty(Table *t) {
    TNode *node = t->first;
    if (!node) { return NULL; }
    int fd = node->file_descriptor;

    if (fd != 3) {
        return NULL;
    }

    while (node) {
        if (!node->next) {
            return node; 
        } 

        node = node->next;
    }
    
    return NULL;
}

TNode* get_fd(Table *t, int fd) {
  TNode *node = t->first;
  while (node && node->file_descriptor != fd) {
    node = node->next;
  }
  
  return node;
}
