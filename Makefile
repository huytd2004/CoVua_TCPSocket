CC = gcc
CFLAGS = -Wall -pthread -O2
LDFLAGS = -lssl -lcrypto -lm

TARGET = chess_server
SOURCES = main.c client_handler.c auth_manager.c match_manager.c game_manager.c game_manager_handlers.c elo_manager.c matchmaking.c game_control.c match_history.c cJSON.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c server.h cJSON.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(TARGET) $(OBJECTS)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run