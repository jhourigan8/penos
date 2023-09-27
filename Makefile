######################################################################
#
#                       Author: Hannah Pan
#                       Date:   01/31/2021
#
# The autograder will run the following command to build the program:
#
#       make -B
#
######################################################################

# name of the program to build
#
PROG=pennos

PROMPT='"$$ "'

# Remove -DNDEBUG during development if assert(3) is used
#
override CPPFLAGS += -DNDEBUG -DPROMPT=$(PROMPT)

CC = clang

# Replace -O1 with -g for a debug version during development
#
CFLAGS = -Wall -Werror -g

SRCS = kernel/scheduler.c kernel/shell_functions.c kernel/queue.c fs/syscalls.c fs/filesys.c fs/table.c pennos.c error.c
OBJS = $(SRCS:.c=.o)

.PHONY : clean

$(PROG) : $(OBJS)
	$(CC) -o $@ $^

clean :
	$(RM) $(OBJS) $(PROG)