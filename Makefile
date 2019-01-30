CC = gcc
CFLAGS += -g -O2 -Wall -W
#-Werror
LDFLAGS += -libverbs -lvl -lpthread -lmlx5
OBJECTS = main.o resources.o test.o get_clock.o
TARGETS = post_send_test

all: $(TARGETS)

post_send_test: $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

main.o: main.c types.h test.h resources.h
	$(CC) -c $(CFLAGS) $<

resources.o: resources.c resources.h types.h
	$(CC) -c $(CFLAGS) $<

test.o: test.c test.h types.h resources.h get_clock.h
	$(CC) -c $(CFLAGS) $<

get_clock.o: get_clock.c get_clock.h
	$(CC) -c $(CFLAGS) $<

clean:
	rm -f $(OBJECTS) $(TARGETS)

# TEST EXECUTABLES
TEST_EXE = post_send_test

