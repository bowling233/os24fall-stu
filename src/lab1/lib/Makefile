GCC 		= riscv64-linux-gnu-gcc
C_SRC       = $(sort $(wildcard *.c))
OBJ		    = $(patsubst %.c,%.o,$(C_SRC))
INC_PATH	= -I../include -I../arch/riscv/include
CFLAG		= ${INC_PATH} -Wall -g

all:$(OBJ)
	
%.o:%.c
	${GCC} ${CFLAG} -c $<
clean:
	$(shell rm *.o 2>/dev/null)
