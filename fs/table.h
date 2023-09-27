#ifndef TABLE 
#define TABLE
/*
Header for Linked List that will be used to implement most tables
*/

// File type macros
#define READ 0
#define WRITE 1
#define APPEND 2

typedef struct table_node {
  // file descriptor
  int file_descriptor;
  // file name
  char* file_name;
  // mode
  int mode;
  // pointer
  int file_pointer;
  //next
  struct table_node *next;
} TNode;

typedef struct table {
  TNode *first;
  TNode *last;
} Table;

/*
    Initiate Table
*/
void init(Table* t);

/*
    Add entry to table. Returns the node of the added entry
*/
TNode* add(Table *t, char* name, int m);

/*
    Remove first entry from the table
*/
TNode* deq(Table *t);

/*
    Deletes entry with the corresponding file descriptor
*/
TNode* delete(Table *t, int fd);

/*
    Finds the first non-used file descriptor
*/
TNode* find_empty(Table *t);

/*
    Get node from file descriptor
*/
TNode* get_fd(Table *t, int fd);
#endif