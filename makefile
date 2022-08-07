BUILDDIR := $(abspath build)
CLIENT := $(BUILDDIR)/client
SERVER := $(BUILDDIR)/server

all: $(BUILDDIR) $(SERVER) $(CLIENT)

$(SERVER): src/server.c src/draw.c
	$(CC) -o $@ $^ -g -O2 -lraylib -lm

$(CLIENT): src/client.c src/draw.c
	$(CC) -o $@ $^ -g -O2 -lraylib -lm

$(BUILDDIR):
	mkdir -p $(BUILDDIR)
