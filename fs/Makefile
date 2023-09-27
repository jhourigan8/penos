######################################################################
#
#                       Author: Hannah Pan
#                       Date:   01/13/2021
#
# The autograder will run the following command to build the program:
#
#       make -B
#
# To build a version that does not call kill(2), it will run:
#
#       make -B CPPFLAGS=-DEC_NOKILL
#
######################################################################


# name of the program to build
#
PROG=pennfat

PROMPT='"$(PROG)\# "'

# Remove -DNDEBUG during development if assert(3) is used
#
override CPPFLAGS += -DNDEBUG -DPROMPT=$(PROMPT)

CC = clang

# Replace -O1 with -g for a debug version during development
#
CFLAGS = -Wall -Werror -O1

SRCS = filesys.c pennfat.c syscalls.c table.c ../error.c
OBJS = $(SRCS:.c=.o)

.PHONY : clean

$(PROG) : $(OBJS)
	$(CC) -o $@ $^

clean :
	$(RM) $(OBJS) $(PROG)
